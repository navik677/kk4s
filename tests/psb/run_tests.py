"""PSB regression test: C++ reader (compat/psb) vs the Python reference (tools/psb_ref.py).

  python tests/psb/run_tests.py PSB_DUMP_EXE [DIR_WITH_REAL_FILES]

Synthetic files are generated here (every value type, nested containers, the name trie,
resources, "mdf" wrapping, truncation/corruption). No game data in the repo; a directory of
real .pimg/.scn/.psb files can be passed to compare those as well.
Build the dumper: g++ -std=c++17 -O2 -I compat tests/psb/psb_dump.cpp compat/psb/psb.cpp -lz -o psb_dump
"""
import glob, os, struct, subprocess, sys, tempfile, zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
import psb_ref


# ---------------------------------------------------------------- writer
def uint_bytes(v, n=None):
    n = n or max(1, (v.bit_length() + 7) // 8)
    return v.to_bytes(n, "little"), n


def enc_array(vals):
    cnt, cw = uint_bytes(len(vals))
    ew = max([uint_bytes(v)[1] for v in vals] or [1])
    out = bytes([0x0C + cw]) + cnt + bytes([0x0C + ew])
    for v in vals:
        out += v.to_bytes(ew, "little")
    return out


class Writer:
    def __init__(self):
        self.strings, self.names, self.resources = [], [], []

    def sidx(self, s):
        if s not in self.strings: self.strings.append(s)
        return self.strings.index(s)

    def nidx(self, s):
        if s not in self.names: self.names.append(s)
        return self.names.index(s)

    def value(self, v):
        if v is psb_ref.UNDEF: return b"\x00"
        if v is None: return b"\x01"
        if v is True: return b"\x03"
        if v is False: return b"\x02"
        if isinstance(v, int):
            if v == 0: return b"\x04"
            n = 1
            while not -(1 << (8 * n - 1)) <= v < (1 << (8 * n - 1)): n += 1
            return bytes([4 + n]) + v.to_bytes(n, "little", signed=True)
        if isinstance(v, float):
            if v == 0.0: return b"\x1d"
            return b"\x1e" + struct.pack("<f", v) if struct.unpack("<f", struct.pack("<f", v))[0] == v else b"\x1f" + struct.pack("<d", v)
        if isinstance(v, str):
            b, n = uint_bytes(self.sidx(v))
            return bytes([0x14 + n]) + b
        if isinstance(v, tuple) and v[0] == "res":
            self.resources.append(v[1])
            b, n = uint_bytes(len(self.resources) - 1)
            return bytes([0x18 + n]) + b
        if isinstance(v, tuple) and v[0] == "ints":  # compact int array (type 0D..10)
            return enc_array(v[1])
        if isinstance(v, list):
            items = [self.value(x) for x in v]
            offs, pos = [], 0
            for it in items: offs.append(pos); pos += len(it)
            return b"\x20" + enc_array(offs) + b"".join(items)
        if isinstance(v, dict):
            items = [self.value(x) for x in v.values()]
            offs, pos = [], 0
            for it in items: offs.append(pos); pos += len(it)
            return b"\x21" + enc_array([self.nidx(k) for k in v]) + enc_array(offs) + b"".join(items)
        raise TypeError(v)

    def name_tables(self):
        """Trie: child id = charset[parent] + char; tree[child] = parent; a 0 char terminates."""
        charset, tree, leaves = {0: 0}, {}, []
        nxt = [256]
        child = {}

        def get(parent, ch):
            if (parent, ch) not in child:
                if parent not in charset:
                    charset[parent] = nxt[0]; nxt[0] += 256
                cid = charset[parent] + ch
                assert cid not in tree and cid != 0
                tree[cid] = parent
                child[(parent, ch)] = cid
            return child[(parent, ch)]

        charset[0] = 256; nxt[0] = 512
        for name in self.names:
            node = 0
            for ch in name.encode("utf-8"): node = get(node, ch)
            leaves.append(get(node, 0))
        size = max(max(tree) if tree else 0, max(charset)) + 1
        cs = [charset.get(i, 0) for i in range(size)]
        tr = [tree.get(i, 0) for i in range(size)]
        return enc_array(cs) + enc_array(tr) + enc_array(leaves)

    def build(self, root, version=3):
        entries = self.value(root)
        names = self.name_tables()
        offs, pos = [], 0
        sdata = b""
        for s in self.strings:
            offs.append(len(sdata)); sdata += s.encode("utf-8") + b"\0"
        strarr = enc_array(offs)
        coffs, clens, cdata = [], [], b""
        for r in self.resources:
            coffs.append(len(cdata)); clens.append(len(r)); cdata += r
        hdr = 0x2C
        o_names = hdr
        o_str = o_names + len(names)
        o_sdata = o_str + len(strarr)
        o_choffs = o_sdata + len(sdata)
        ca, cl = enc_array(coffs), enc_array(clens)
        o_chlens = o_choffs + len(ca)
        o_chdata = o_chlens + len(cl)
        o_entries = o_chdata + len(cdata)
        head = b"PSB\0" + struct.pack("<HH", version, 0) + struct.pack("<8I", hdr, o_names, o_str, o_sdata, o_choffs, o_chlens, o_chdata, o_entries)
        head += struct.pack("<I", 0)
        return head + names + strarr + sdata + ca + cl + cdata + entries


def mdf(data):
    return b"mdf\0" + struct.pack("<I", len(data)) + zlib.compress(data)


def samples():
    s = {}
    w = Writer()
    s["types"] = w.build({
        "undef": psb_ref.UNDEF, "null": None, "yes": True, "no": False, "zero": 0,
        "i8": 100, "neg8": -100, "i16": 30000, "neg16": -30000, "i24": 5000000, "i32": 2000000000,
        "neg32": -2000000000, "i40": 1 << 36, "i64": (1 << 62) + 5, "neg64": -(1 << 62),
        "f0": 0.0, "f32": 0.5, "f64": 3.141592653589793, "negf": -2.25,
        "str": "hello", "empty": "", "utf8": "日本語 тест", "quote": 'a"b\\c\nd',
        "ints": ("ints", [0, 1, 255, 256, 65535, 70000, 1 << 32]), "emptyints": ("ints", []),
        "arr": [1, "two", [3, [4]], {"five": 5}, None], "emptyarr": [], "emptyobj": {},
        "nested": {"a": {"b": {"c": {"d": [1, 2, {"e": "deep"}]}}}},
        "res": ("res", b"TLG0.0\0sds\x1a" + bytes(range(256))), "res2": ("res", b""),
        "many": list(range(300)),
    })
    w = Writer()
    s["keys"] = w.build({"1599.tlg": ("res", b"x" * 10), "width": 640, "height": 480, "layers": [{"name": "a", "a": 1}, {"name": "b", "ab": 2}],
                         "ab": 1, "abc": 2, "b": 3, "": 4, "é": 5})
    w = Writer()
    s["noresources"] = w.build({"k": "v"})
    big = Writer()
    s["mdf"] = mdf(big.build({"lines": [{"text": "line %d" % i, "n": i, "f": i / 4.0} for i in range(500)]}))
    s["v2"] = Writer().build({"x": 1}, version=2)
    return s


def corrupt(data):
    """Truncations and byte flips: the reader must not crash (error or value, both fine)."""
    out = {}
    for n in (4, 20, 43, 44, len(data) // 2, len(data) - 1):
        if 0 < n < len(data): out["trunc%d" % n] = data[:n]
    for i in range(0, min(len(data), 400), 37):
        b = bytearray(data); b[i] ^= 0xFF; out["flip%d" % i] = bytes(b)
    return out


def main():
    exe = sys.argv[1]
    real = sys.argv[2] if len(sys.argv) > 2 else None
    failures = 0
    checked = 0
    with tempfile.TemporaryDirectory() as td:
        def run(name, data, must_match):
            nonlocal failures, checked
            path = os.path.join(td, "t.bin")
            open(path, "wb").write(data)
            r = subprocess.run([exe, path], capture_output=True)
            if r.returncode not in (0, 1):  # 139 etc.: a crash
                print("CRASH %s (exit %d)" % (name, r.returncode)); failures += 1; return
            cpp = r.stdout.decode("utf-8", "replace").rstrip("\r\n")
            if must_match:
                try:
                    p = psb_ref.PSB(data); py = psb_ref.canon(p, p.root())
                except Exception as e:
                    py = None
                if py is None:
                    ok = cpp.startswith("ERROR")
                else:
                    ok = cpp == py
                if not ok:
                    print("MISMATCH %s\n  cpp: %s\n  py : %s" % (name, cpp[:200], (py or "<error>")[:200])); failures += 1
            checked += 1

        for name, data in samples().items():
            run(name, data, True)
            if name in ("types", "keys", "mdf"):
                for cname, cdata in corrupt(data if name != "mdf" else data).items():
                    run(name + "/" + cname, cdata, False)
        if real:
            for f in glob.glob(os.path.join(real, "**", "*"), recursive=True):
                if f.lower().endswith((".pimg", ".scn", ".psb")):
                    run(f, open(f, "rb").read(), True)
    print("psb tests: %d checked, %d failed" % (checked, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
