"""Reference reader of the PSB container (E-mote / M2 "PSB" files: .pimg, .scn, .psb ...).

Used to verify the C++ PSBFile plugin (engine/source/plugins/psbfile.cpp) against real
files. No game data inside. Usage: python tools/psb_ref.py FILE.psb [--json]
Format (verified on version 3 files, v2/v4 handled by the same layout):

  0x00 "PSB\\0"  0x04 u16 version  0x06 u16 encrypt flag (0 = plain; others unsupported)
  0x08 u32 header length  0x0C names  0x10 strings(offsets)  0x14 strings(data)
  0x18 chunk offsets  0x1C chunk lengths  0x20 chunk data  0x24 entries (root object)
  all offsets are absolute file offsets.

Values: type byte then payload
  00 undefined, 01 null, 02 false, 03 true
  04..0C signed int of (type-4) bytes (04 = 0)
  0D..14 unsigned-int array: type-0C = bytes of the count field? see read_array
  15..18 string index (N=type-0x14 bytes), 19..1C resource (chunk) index (N=type-0x18 bytes)
  1D float 0.0, 1E float32, 1F float64
  20 array (offset list + items), 21 object (name index list + offset list + items)
  22..25 extra resource index (N=type-0x21 bytes)
"""
import struct, sys, json


class PSBError(Exception):
    pass


class Undefined:
    """Value of type 0x00 (kept apart from null = None)."""
    def __repr__(self): return "undefined"


UNDEF = Undefined()


class PSB:
    def __init__(self, data):
        if data[:4] == b"mdf\0":  # zlib-compressed PSB: u32 raw size, zlib stream
            import zlib
            data = zlib.decompress(data[8:])
        self.d = data
        if data[:4] != b"PSB\0":
            raise PSBError("not a PSB file")
        self.version, self.encrypt = struct.unpack_from("<HH", data, 4)
        if self.encrypt not in (0,):
            raise PSBError("encrypted PSB header")
        (self.hdr_len, self.off_names, self.off_strings, self.off_strdata, self.off_choffs,
         self.off_chlens, self.off_chdata, self.off_entries) = struct.unpack_from("<8I", data, 8)
        self.strings = self._load_strings()
        self.names = self._load_names()

    # -- primitives
    def uint(self, pos, n):
        return int.from_bytes(self.d[pos:pos + n], "little")

    def read_array(self, pos):
        """Compact uint array. Returns (values, end). type: 0D..10 = count width 1..4,
        followed by one byte 0D..14 = entry width 1..8."""
        t = self.d[pos]
        if not 0x0D <= t <= 0x10:
            raise PSBError("array expected at %#x (type %#x)" % (pos, t))
        cw = t - 0x0C
        count = self.uint(pos + 1, cw)
        ew = self.d[pos + 1 + cw] - 0x0C
        p = pos + 2 + cw
        vals = [self.uint(p + i * ew, ew) for i in range(count)]
        return vals, p + count * ew

    def _load_strings(self):
        offs, _ = self.read_array(self.off_strings)
        out = []
        for o in offs:
            s = self.off_strdata + o
            e = self.d.index(b"\0", s)
            out.append(self.d[s:e].decode("utf-8", "replace"))
        return out

    def _load_names(self):
        """Names are a trie stored as three arrays: charset, tree, leaves (one per name).
        Walking from a leaf: chr = tree[idx]; code = tree[chr]; char = chr - charset[code]."""
        charset, p = self.read_array(self.off_names)
        tree, p = self.read_array(p)
        leaves, p = self.read_array(p)
        names = []
        for idx in leaves:
            chars = []
            c = tree[idx]
            while c != 0:
                chars.append(c - charset[tree[c]])
                idx = c
                c = tree[idx]
                if len(chars) > 4096:
                    raise PSBError("name trie loop")
            names.append(bytes(reversed(chars)).decode("utf-8", "replace"))
        return names

    # -- values
    def value(self, pos):
        t = self.d[pos]
        d = self.d
        if t == 0x00: return UNDEF
        if t == 0x01: return None
        if t == 0x02: return False
        if t == 0x03: return True
        if 0x04 <= t <= 0x0C:
            n = t - 4
            return int.from_bytes(d[pos + 1:pos + 1 + n], "little", signed=True) if n else 0
        if 0x0D <= t <= 0x10:
            vals, _ = self.read_array(pos)
            return vals
        if 0x15 <= t <= 0x18:
            return self.strings[self.uint(pos + 1, t - 0x14)]
        if 0x19 <= t <= 0x1C:
            return ("resource", self.uint(pos + 1, t - 0x18))
        if t == 0x1D: return 0.0
        if t == 0x1E: return struct.unpack_from("<f", d, pos + 1)[0]
        if t == 0x1F: return struct.unpack_from("<d", d, pos + 1)[0]
        if t == 0x20:
            offs, base = self.read_array(pos + 1)
            return [self.value(base + o) for o in offs]
        if t == 0x21:
            nidx, p = self.read_array(pos + 1)
            offs, base = self.read_array(p)
            return {self.names[n]: self.value(base + o) for n, o in zip(nidx, offs)}
        if 0x22 <= t <= 0x25:
            return ("extra", self.uint(pos + 1, t - 0x21))
        raise PSBError("unknown type %#x at %#x" % (t, pos))

    def root(self):
        return self.value(self.off_entries)

    def resource(self, idx):
        offs, _ = self.read_array(self.off_choffs)
        lens, _ = self.read_array(self.off_chlens)
        return self.d[self.off_chdata + offs[idx]:self.off_chdata + offs[idx] + lens[idx]]


def _q(s):
    out = ['"']
    for b in s.encode("utf-8"):
        c = chr(b)
        if c == '"' or c == chr(92): out.append(chr(92) + c)
        elif 0x20 <= b < 0x7f: out.append(c)
        else: out.append(chr(92) + "x%02x" % b)
    out.append('"')
    return "".join(out)


def canon(p, v):
    """Canonical text of a value; the C++ test dumper prints exactly the same."""
    import zlib
    if v is UNDEF: return "undefined"
    if v is None: return "null"
    if v is True: return "true"
    if v is False: return "false"
    if isinstance(v, int): return str(v)
    if isinstance(v, float): return "%.17g" % v
    if isinstance(v, str): return _q(v)
    if isinstance(v, tuple):
        if v[0] == "resource":
            data = p.resource(v[1])
            return "<res %d %d %08x>" % (v[1], len(data), zlib.adler32(data) & 0xffffffff)
        return "<extra %d>" % v[1]
    if isinstance(v, list): return "[" + ",".join(canon(p, x) for x in v) + "]"
    if isinstance(v, dict): return "{" + ",".join(_q(k) + ":" + canon(p, x) for k, x in v.items()) + "}"
    raise PSBError("cannot dump %r" % (v,))


def _jsonable(v):
    if isinstance(v, dict): return {k: _jsonable(x) for k, x in v.items()}
    if isinstance(v, list): return [_jsonable(x) for x in v]
    if isinstance(v, tuple): return "<%s %d>" % v
    return v


if __name__ == "__main__":
    p = PSB(open(sys.argv[1], "rb").read())
    r = p.root()
    if "--canon" in sys.argv:
        print(canon(p, r))
        sys.exit(0)
    print(json.dumps(_jsonable(r), ensure_ascii=False, indent=1)[:4000] if "--json" in sys.argv else
          "version %d names %d strings %d keys %s" % (p.version, len(p.names), len(p.strings),
                                                      list(r)[:8] if isinstance(r, dict) else type(r)))
