#!/usr/bin/env python3
"""Disassemble a compiled TJS2 script (*.tjs / startup.tjs bytecode).

The engine accepts TJS2 bytecode wherever it accepts source, and some games
(and repacks that wrap them) ship a compiled `startup.tjs` instead of readable
text. When such a game quits before KAG starts, the bytecode usually says why:
a protection stub can be a few instructions that just call `System.inform`
with a message and return. This prints the constants and the VM code so the
behaviour can be read without a Windows build.

Usage:
    python tools/tjsdis.py <file.tjs>        # or a *.scn compiled script

Format: engine/source/core/tjs2/tjsByteCodeLoader.cpp and the VM loop in
tjsInterCodeExec.cpp (the bytecode is little-endian int16 code words).

This is a diagnostic tool only; it never writes game files.
"""
import struct
import sys


def read(path):
    with open(path, "rb") as f:
        return f.read()


class Reader:
    def __init__(self, d):
        self.d = d
        self.o = 0

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.o)[0]
        self.o += 4
        return v

    def i32(self):
        v = struct.unpack_from("<i", self.d, self.o)[0]
        self.o += 4
        return v

    def i16(self):
        v = struct.unpack_from("<h", self.d, self.o)[0]
        self.o += 2
        return v

    def i64(self):
        v = struct.unpack_from("<q", self.d, self.o)[0]
        self.o += 8
        return v

    def f64(self):
        v = struct.unpack_from("<d", self.d, self.o)[0]
        self.o += 8
        return v

    def skip(self, n):
        self.o += n

    def align4(self, n):
        self.o += ((n + 3) >> 2) << 2


# tTJSVMCodes in engine/source/core/tjs2/tjsInterCodeGen.h, in order.
OPS = "NOP CONST CP CL CCL TT TF CEQ CDEQ CLT CGT SETF SETNF LNOT NF JF JNF JMP".split()
for base in ("INC", "DEC", "LOR", "LAND", "BOR", "BXOR", "BAND", "SAR", "SAL",
             "SR", "ADD", "SUB", "MOD", "DIV", "IDIV", "MUL"):
    OPS += [base, base + "PD", base + "PI", base + "P"]
OPS += ("BNOT TYPEOF TYPEOFD TYPEOFI EVAL EEXP CHKINS ASC CHR NUM CHS INV CHKINV "
        "INT REAL STR OCTET CALL CALLD CALLI NEW GPD SPD SPDE SPDEH GPI SPI SPIE "
        "GPDS SPDS GPIS SPIS SETP GETP DELD DELI SRV RET ENTRY EXTRY THROW CHGTHIS "
        "GLOBAL ADDCI REGMEMBER DEBUGGER CHKIN").split()

LENGTH = {i: 1 for i in range(len(OPS))}
for names, n in (
    ("NOP NF RET EXTRY", 1),
    ("CL TT TF SETF SETNF LNOT INC DEC BNOT ASC CHR NUM CHS INV CHKINV INT REAL "
     "STR OCTET TYPEOF EVAL EEXP SRV GLOBAL THROW", 2),
    ("CONST CP CCL CEQ CDEQ CLT CGT CHGTHIS ADDCI GETP SETP INCP DECP CHKINS "
     "CHKIN ENTRY", 3),
    ("INCPD INCPI DECPD DECPI TYPEOFD TYPEOFI GPD GPDS SPD SPDE SPDEH SPDS GPI "
     "GPIS SPI SPIE SPIS DELD DELI", 4),
):
    for name in names.split():
        LENGTH[OPS.index(name)] = n
for base in ("LOR", "LAND", "BOR", "BXOR", "BAND", "SAR", "SAL", "SR", "ADD",
             "SUB", "MOD", "DIV", "IDIV", "MUL"):
    LENGTH[OPS.index(base + "PD")] = 5
    LENGTH[OPS.index(base + "PI")] = 5
    LENGTH[OPS.index(base + "P")] = 4


def instr_len(code, i):
    op = code[i]
    if op in (OPS.index("CALL"), OPS.index("CALLD"), OPS.index("CALLI"),
              OPS.index("NEW")):
        argc = code[i + 3] if i + 3 < len(code) else 0
        if argc == -1:
            return 4
        if argc == -2:
            return 5 + (code[i + 4] if i + 4 < len(code) else 0) * 2
        return argc + 4
    return LENGTH.get(op, 1)


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[0])
        print("usage: tjsdis.py <file.tjs>")
        return 2
    d = read(sys.argv[1])
    if len(d) < 20 or d[:4] != b"TJS2":
        print("%s: not TJS2 bytecode (files starting with '//' are source; just open them)" % sys.argv[1])
        return 1
    if d[4:8] != b"100\0":
        print("unsupported TJS2 bytecode version %r" % d[4:8])
        return 1
    declared = struct.unpack_from("<I", d, 8)[0]
    if declared != len(d):
        print("warning: header says %d bytes, file is %d" % (declared, len(d)))
    if d[12:16] != b"DATA":
        print("missing DATA chunk")
        return 1

    r = Reader(d)
    r.skip(20)

    n = r.u32()
    bytearr = d[r.o:r.o + n]
    r.align4(n)
    n = r.u32()
    shorts = [r.i16() for _ in range(n)]
    r.o += (n & 1) << 1
    n = r.u32()
    longs = [r.i32() for _ in range(n)]
    n = r.u32()
    longlongs = [r.i64() for _ in range(n)]
    n = r.u32()
    doubles = [r.f64() for _ in range(n)]
    n = r.u32()
    strings = []
    for _ in range(n):
        ln = r.u32()
        strings.append(d[r.o:r.o + 2 * ln].decode("utf-16-le", "replace"))
        r.o += 2 * ln
        r.o += (ln & 1) << 1
    n = r.u32()
    octets = []
    for _ in range(n):
        ln = r.u32()
        octets.append(d[r.o:r.o + ln])
        r.align4(ln)

    print("data area: %d byte(s), shorts %s, ints %s, reals %s"
          % (len(bytearr), shorts, longs, doubles))
    for i, s in enumerate(strings):
        print("  string[%d] = %r" % (i, s))
    for i, o in enumerate(octets):
        print("  octet[%d] = %d byte(s)" % (i, len(o)))
    if r.d[r.o:r.o + 4] != b"OBJS":
        print("missing OBJS chunk at %d" % r.o)
        return 1
    r.skip(4)
    obj_size = r.u32()
    toplevel = r.u32()
    obj_count = r.u32()
    print("objects: %d (top level %d, %d bytes)" % (obj_count, toplevel, obj_size))

    for o in range(obj_count):
        tag = d[r.o:r.o + 4]
        r.skip(4)
        size = r.u32()
        parent = r.i32()
        name = r.i32()
        context_type = r.i32()
        maxvar = r.i32()
        r.skip(4)  # variable reserve count
        maxframe = r.i32()
        r.skip(4 * 6)  # func args, collapse base, setter, getter, superclass getter

        n = r.u32()
        src_pos = []
        for _ in range(n):
            src_pos.append((r.u32(), r.u32()))
        n = r.u32()
        code = [r.i16() for _ in range(n)]
        r.o += (n & 1) << 1
        n = r.u32()
        constants = [(r.i16(), r.i16()) for _ in range(n)]
        n = r.u32()
        r.skip(4 * n)  # superclass getter addresses
        n = r.u32()
        r.skip(8 * n)  # properties

        cname = strings[name] if 0 <= name < len(strings) else str(name)
        print("\n=== object %d %r: type %d, parent %d, %d code word(s), constants %s"
              % (o, cname, context_type, parent, len(code), constants))
        i = 0
        while i < len(code):
            op = code[i]
            name_op = OPS[op] if 0 <= op < len(OPS) else "?%d" % op
            ln = instr_len(code, i)
            args = code[i + 1:i + ln]
            note = ""
            if name_op == "CONST" and len(args) >= 2:
                idx = args[1]
                note = " ; data[%d]" % idx
                if 0 <= idx < len(constants):
                    t, si = constants[idx]
                    if t == 3 and 0 <= si < len(strings):
                        note += " = %r" % strings[si]
                    else:
                        note += " = type %d index %d" % (t, si)
            print("  %4d  %-8s %s%s" % (i, name_op, " ".join(str(a) for a in args), note))
            i += ln
    return 0


if __name__ == "__main__":
    sys.exit(main())
