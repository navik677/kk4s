"""Static CXDEC scheme extractor (prototype, PC side). See docs/cxdec.md.

Finds the Cx TPM of a KiriKiri game *without running it*, reads the scheme
(mask, offset, branch orders, control block) from the TPM's code, and validates
every candidate against real archive entries (adler32 of the decrypted data must
equal the entry's adlr). Nothing game-specific is hard-coded: candidates come
from the generic shape of the CX generator, and the data decides.

Sources tried, in order:
  1. *.tpm files in the game folder (classic CX, GARbro's signature scan);
  2. "internal module" containers embedded in the EXE/plugin DLLs
     ("internal module\\0", 8 bytes, u32 raw size, u32 zlib size, zlib data).

Usage: python tools/cx_scheme_extract.py GAME_DIR [OUT.cxscheme]
Requires: capstone, pefile (PC tool only; the runtime reads the JSON).
"""
import os, sys, glob, json, re, struct, zlib, hashlib

import capstone, pefile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cxdec_ref

CTL_SIG = b" Encryption control block"
LCG = struct.pack("<I", 1103515245)


# ---------------------------------------------------------------- TPM sources
def internal_modules(blob):
    """Yield the PE images stored in "internal module" containers."""
    for m in re.finditer(re.escape(b"internal module\0"), blob):
        p = m.end()
        for skip in range(0, 33, 4):  # header padding is not fixed; scan a bit
            if p + skip + 10 > len(blob):
                break
            raw, zsz = struct.unpack_from("<II", blob, p + skip)
            z = blob[p + skip + 8:p + skip + 8 + zsz]
            if 0 < zsz < len(blob) and z[:1] == b"\x78":
                try:
                    img = zlib.decompress(z)
                except zlib.error:
                    continue
                if len(img) == raw and img[:2] == b"MZ":
                    yield img
                    break


def find_tpms(game_dir):
    out = []
    for f in glob.glob(os.path.join(game_dir, "*.tpm")) + glob.glob(os.path.join(game_dir, "plugin", "*.tpm")):
        out.append((f, open(f, "rb").read()))
    cands = glob.glob(os.path.join(game_dir, "*.exe")) + glob.glob(os.path.join(game_dir, "plugin", "*.dll"))
    for f in cands:
        blob = open(f, "rb").read()
        for i, img in enumerate(internal_modules(blob)):
            if LCG in img:
                out.append(("%s#internal%d" % (f, i), img))
    return out


# ---------------------------------------------------------------- analysis
class Tpm:
    def __init__(self, img):
        self.pe = pefile.PE(data=img)
        self.mem = self.pe.get_memory_mapped_image()
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.text = [s for s in self.pe.sections if s.Characteristics & 0x20000000]

    def code_at(self, va, n=0x200):
        rva = va - self.base
        return self.md.disasm(self.mem[rva:rva + n], va)

    def in_data(self, va, size):
        rva = va - self.base
        for s in self.pe.sections:
            if s.Characteristics & 0x20000000:
                continue
            if s.VirtualAddress <= rva and rva + size <= s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
                return True
        return False

    def control_blocks(self):
        """Candidates: GARbro signature, else data pointers returned by tiny getters."""
        m = self.mem
        i = m.find(CTL_SIG)
        if i >= 0:
            yield bytes(m[i:i + 4096])  # raw values (GARbro stores ~raw and executes ~)
        for t in self.text:
            seg = m[t.VirtualAddress:t.VirtualAddress + t.Misc_VirtualSize]
            for mm in re.finditer(rb"\xb8(....)\xc3", seg, re.S):
                va = struct.unpack("<I", mm.group(1))[0]
                if self.in_data(va, 4096) and va % 4 == 0:
                    rva = va - self.base
                    yield bytes(m[rva:rva + 4096])

    def mask_offsets(self):
        """`and r32, imm32 ; add r32, imm32` on the same register (base offset)."""
        out = []
        for t in self.text:
            seg = self.mem[t.VirtualAddress:t.VirtualAddress + t.Misc_VirtualSize]
            for mm in re.finditer(rb"\x81([\xe0-\xe7])(....)\x81([\xc0-\xc7])(....)", seg, re.S):
                if (mm.group(1)[0] & 7) != (mm.group(3)[0] & 7):
                    continue
                mask = struct.unpack("<I", mm.group(2))[0]
                off = struct.unpack("<I", mm.group(4))[0]
                if mask < 0x10000 and off < 0x100000:
                    out.append((mask, off))
        return list(dict.fromkeys(out))

    # -- branch orders ------------------------------------------------------
    def _emitted(self, va, stop, limit=40):
        """Byte constants a case block emits (push imm / mov byte [r], imm)."""
        vals = []
        seen = set()
        for _ in range(4):
            jumped = False
            for ins in self.code_at(va, 0x200):
                if ins.address in seen or ins.address == stop or len(seen) > limit:
                    return vals
                seen.add(ins.address)
                if ins.mnemonic == "push" and ins.op_str.startswith("0x") or ins.op_str.isdigit():
                    vals.append(int(ins.op_str, 0) & 0xffffffff)
                elif ins.mnemonic == "mov" and ins.op_str.startswith("byte ptr [") and "], " in ins.op_str:
                    v = ins.op_str.split("], ")[1]
                    if v.startswith("0x") or v.isdigit():
                        vals.append(int(v, 0))
                elif ins.mnemonic == "mov" and ins.op_str.startswith("dword ptr [") and "], 0x" in ins.op_str:
                    vals.append(int(ins.op_str.split("], ")[1], 0))
                if ins.mnemonic == "jmp" and ins.op_str.startswith("0x"):
                    va = int(ins.op_str, 16)
                    jumped = True
                    break
                if ins.mnemonic in ("ret", "jmp"):
                    return vals
            if not jumped:
                return vals
        return vals

    @staticmethod
    def classify(kind, vals):
        s = set(vals)
        if kind == "prolog":
            if 0xbe in s: return 2
            if 0xc7 in s: return 1
            if 0xb8 in s: return 0
        if kind == "odd":
            if 0xd3 in s and 0xe8 in s: return 0
            if 0xd3 in s and 0xe0 in s: return 1
            if 0xaf in s: return 4
            if 0x29 in s: return 5
            if 0xf7 in s: return 3
            if 0x01 in s: return 2
        if kind == "even":
            if 0xaaaaaaaa in s or 0xd1 in s: return 5
            if 0xbe in s or 0x3ff in s: return 4
            if 0x35 in s: return 6
            if 0x05 in s or 0x2d in s: return 7
            if 0x48 in s: return 1
            if 0x40 in s: return 3
            if 0xf7 in s and 0xd0 in s: return 0
            if 0xf7 in s and 0xd8 in s: return 2
        return None

    def _jump_table(self, ins_list, k):
        op = ins_list[k].op_str  # dword ptr [edx*4 + 0x10008dbc]
        tbl = int(op.split("+ ")[1].rstrip("]"), 16)
        return tbl

    def orders(self):
        res = {}
        for t in self.text:
            seg = self.mem[t.VirtualAddress:t.VirtualAddress + t.Misc_VirtualSize]
            ins = list(self.md.disasm(seg, self.base + t.VirtualAddress))
            for k, i in enumerate(ins):
                # jump-table switches: div 6 (odd) and and-7 (even)
                if i.mnemonic == "jmp" and "*4 + 0x" in i.op_str:
                    ctx = ins[max(0, k - 8):k]
                    n = None
                    if any(c.mnemonic == "mov" and c.op_str.endswith(", 6") for c in ctx) and any(c.mnemonic == "div" for c in ctx):
                        n, kind = 6, "odd"
                    elif any(c.mnemonic == "and" and c.op_str.endswith(", 7") for c in ctx) and \
                            any(c.mnemonic == "imul" and "0x41c64e6d" in c.op_str for c in ins[max(0, k - 16):k]):
                        n, kind = 8, "even"
                    if n is None:
                        continue
                    tbl = self._jump_table(ins, k) - self.base
                    stop = next((int(c.op_str, 16) for c in ctx if c.mnemonic == "ja"), None)
                    targets = struct.unpack_from("<%dI" % n, self.mem, tbl)
                    res[kind] = [self.classify(kind, self._emitted(v, stop)) for v in targets]
                # compare-chain switch: mov ecx,3 ... div ecx ; sub edx,0 / dec edx chain
                if i.mnemonic == "div" and any(c.mnemonic == "mov" and c.op_str == "ecx, 3" for c in ins[max(0, k - 12):k]):
                    targets, j = [], k + 1
                    while j < len(ins) and len(targets) < 3:
                        c = ins[j]
                        if c.mnemonic in ("je", "jne") and c.op_str.startswith("0x"):
                            tgt = int(c.op_str, 16)
                            if c.mnemonic == "je":
                                targets.append(tgt)
                            else:  # last case falls through, jne goes to default
                                targets.append(ins[j + 1].address)
                                break
                        j += 1
                    if len(targets) == 3:
                        res["prolog"] = [self.classify("prolog", self._emitted(v, None, 25)) for v in targets]
                    else:
                        # jump-table form
                        for c2 in ins[k:k + 6]:
                            if c2.mnemonic == "jmp" and "*4 + 0x" in c2.op_str:
                                tbl = int(c2.op_str.split("+ ")[1].rstrip("]"), 16) - self.base
                                tg = struct.unpack_from("<3I", self.mem, tbl)
                                res["prolog"] = [self.classify("prolog", self._emitted(v, None, 25)) for v in tg]
        return res


# ---------------------------------------------------------------- validation
def xp3_samples(game_dir, count=12):
    """(adlr, encrypted bytes) of small entries of the first hnfn archive found."""
    for xp3 in sorted(glob.glob(os.path.join(game_dir, "*.xp3"))):
        idx = xp3 + ".index"
        data = open(idx, "rb").read() if os.path.exists(idx) else None
        if data is None or b"hnfn" not in data[:64]:
            continue
        out, pos = [], data.find(b"File")
        arc = open(xp3, "rb")
        while pos >= 0 and pos + 12 <= len(data) and data[pos:pos + 4] == b"File" and len(out) < count:
            size = struct.unpack_from("<Q", data, pos + 4)[0]
            p, end, adlr, segs = pos + 12, pos + 12 + size, None, []
            while p + 12 <= end:
                tag, ss = data[p:p + 4], struct.unpack_from("<Q", data, p + 4)[0]
                if tag == b"adlr":
                    adlr = struct.unpack_from("<I", data, p + 12)[0]
                elif tag == b"segm":
                    for q in range(0, ss, 28):
                        segs.append(struct.unpack_from("<IQQQ", data, p + 12 + q))
                p += 12 + ss
            if adlr is not None and segs and sum(s[3] for s in segs) < 200000:
                buf = b""
                for fl, off, raw, packed in segs:
                    arc.seek(off)
                    chunk = arc.read(packed)
                    buf += zlib.decompress(chunk) if fl & 7 == 1 else chunk
                out.append((adlr, buf))
            pos = end
        if out:
            return out
    return []


def validate(scheme, samples):
    cx = cxdec_ref.Cx(scheme["mask"], scheme["offset"], scheme["prolog"], scheme["odd"], scheme["even"], scheme["cb"])
    ok = 0
    for adlr, enc in samples:
        buf = bytearray(enc)
        cx.decrypt(adlr, 0, buf, 0, len(buf))
        ok += (zlib.adler32(bytes(buf)) & 0xffffffff) == adlr
    return ok


def main():
    game = sys.argv[1]
    samples = xp3_samples(game)
    if not samples:
        sys.exit("no hnfn (Cx) archive with readable entries found")
    for src, img in find_tpms(game):
        tpm = Tpm(img)
        orders = tpm.orders()
        print("TPM:", src, "orders:", orders)
        if not all(k in orders and None not in orders[k] and len(set(orders[k])) == len(orders[k]) for k in ("prolog", "odd", "even")):
            print("  could not read all branch orders")
            continue
        # The protector inserts decoy entries that never decrypt, so do not require
        # every sample; take the best candidate if it explains clearly most of them.
        best = None
        for mask, off in tpm.mask_offsets():
            for cb in tpm.control_blocks():
                sch = dict(mask=mask, offset=off, cb=cb, **{k: orders[k] for k in ("prolog", "odd", "even")})
                n = validate(sch, samples)
                if best is None or n > best[0]:
                    best = (n, sch)
        if best:
            n, sch = best
            mask, off, cb = sch["mask"], sch["offset"], sch["cb"]
            if n >= max(4, 0.8 * len(samples)):
                print("  VALID scheme: mask=%#x offset=%#x cb sha256=%s (%d/%d entries)" % (
                    mask, off, hashlib.sha256(cb).hexdigest()[:16], n, len(samples)))
                if len(sys.argv) > 2:
                    cxdec_ref.write_scheme(sys.argv[2], mask, off, orders["prolog"], orders["odd"],
                                           orders["even"], cb, os.path.basename(src))
                return 0
            print("  best candidate explains only %d/%d entries" % (n, len(samples)))
    print("no scheme validated")
    return 1


if __name__ == "__main__":
    sys.exit(main())
