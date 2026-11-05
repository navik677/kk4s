#!/usr/bin/env python3
"""Small PE32 disassembly helper for the E-mote PSB cipher study.

Reads a PE file, maps file offsets to virtual addresses, finds MSVC-style
function prologues, and disassembles a range with capstone.  Nothing here
writes to the input file.

Usage:
  python research/emote-psb/pe_disasm.py DLL --at 0x1004bf93 [--before 0x200] [--len 0x300]
  python research/emote-psb/pe_disasm.py DLL --func 0x1004bf93 [--len 0x300]
  python research/emote-psb/pe_disasm.py DLL --offset 0x4b399 [--len 0x100]
  python research/emote-psb/pe_disasm.py DLL --xrefs 0x100f3a40
  python research/emote-psb/pe_disasm.py DLL --find-bytes 15cd5b07
"""
import argparse
import sys

import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs


class Image:
    def __init__(self, path):
        self.path = path
        self.pe = pefile.PE(path)
        self.data = open(path, "rb").read()
        self.base = self.pe.OPTIONAL_HEADER.ImageBase

    def va_to_off(self, va):
        return self.pe.get_offset_from_rva(va - self.base)

    def off_to_va(self, off):
        return self.base + self.pe.get_rva_from_offset(off)

    def read_va(self, va, n):
        return self.data[self.va_to_off(va):self.va_to_off(va) + n]

    def sections(self):
        for s in self.pe.sections:
            yield (s.Name.rstrip(b"\0").decode("latin-1"), self.base + s.VirtualAddress,
                   s.Misc_VirtualSize, s.PointerToRawData, s.SizeOfRawData)

    def section_of(self, va):
        for name, sva, vsize, raw, rsize in self.sections():
            if sva <= va < sva + max(vsize, rsize):
                return name, sva, vsize, raw, rsize
        return None

    def prologue_before(self, va, limit=0x4000):
        """Nearest MSVC prologue at or before va (55 8b ec / 6a xx 68 / 8b ff 55)."""
        best = None
        for delta in range(0, limit):
            p = va - delta
            b = self.read_va(p, 4)
            if b[:3] == b"\x55\x8b\xec" or b[:3] == b"\x8b\xff\x55" or (b[0] == 0x6A and b[2] == 0x68):
                best = p
                break
        return best

    def disasm(self, va, length):
        sec = self.section_of(va)
        if not sec:
            raise SystemExit("VA %#x is not inside a section" % va)
        code = self.read_va(va, length)
        md = Cs(CS_ARCH_X86, CS_MODE_32)
        md.detail = True
        out = []
        for ins in md.disasm(code, va):
            out.append(ins)
        return out


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("dll")
    ap.add_argument("--at", type=lambda s: int(s, 0), help="VA to start at")
    ap.add_argument("--func", type=lambda s: int(s, 0), help="VA of a point inside a function")
    ap.add_argument("--offset", type=lambda s: int(s, 0), help="file offset to start at")
    ap.add_argument("--before", type=lambda s: int(s, 0), default=0, help="bytes before --at")
    ap.add_argument("--len", type=lambda s: int(s, 0), default=0x200)
    ap.add_argument("--xrefs", type=lambda s: int(s, 0), help="find code references to this VA")
    ap.add_argument("--find-bytes", help="hex bytes to locate")
    ap.add_argument("--info", action="store_true")
    args = ap.parse_args(argv)

    img = Image(args.dll)
    if args.info:
        print("image base %#x  size %d" % (img.base, len(img.data)))
        for name, va, vsize, raw, rsize in img.sections():
            print("  %-8s va=%08x vsize=%06x raw=%08x rsize=%06x" % (name, va, vsize, raw, rsize))
        return 0

    if args.find_bytes:
        pat = bytes.fromhex(args.find_bytes)
        i = 0
        while True:
            i = img.data.find(pat, i)
            if i < 0:
                break
            print("file %#x  va %#x" % (i, img.off_to_va(i)))
            i += 1
        return 0

    if args.xrefs:
        target = args.xrefs.to_bytes(4, "little")
        i = 0
        while True:
            i = img.data.find(target, i)
            if i < 0:
                break
            print("imm32 referenced at file %#x  va %#x" % (i, img.off_to_va(i)))
            i += 1
        return 0

    if args.offset is not None:
        va = img.off_to_va(args.offset)
    elif args.func is not None:
        va = img.prologue_before(args.func) or args.func - 0x100
    else:
        va = args.at - args.before
    for ins in img.disasm(va, args.len):
        print("%08x  %-24s %s %s" % (ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
