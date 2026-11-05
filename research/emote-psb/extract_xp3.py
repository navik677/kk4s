#!/usr/bin/env python3
"""Extract files from an XP3 archive by index position (study aid).

NekoPara's archives store hash-like names and an extra custom "eliF" chunk, so
real names are not available here.  This tool only needs the standard File
index (info + segm sub-chunks) and writes each entry to OUT_DIR/<index>_<name>.

Input files are opened read-only.  Output must be a directory outside any game
folder (default: research/emote-psb/work/samples).

Usage:
  python research/emote-psb/extract_xp3.py ARCHIVE OUT_DIR [--max-bytes N] [--filter-substr S]
"""
import argparse
import os
import struct
import sys
import zlib

MAGIC = b"XP3\r\n \n\x1a\x8b\x67\x01"


def u64(b, o):
    return struct.unpack_from("<Q", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def read_index(f):
    if f.read(11) != MAGIC:
        raise SystemExit("not an XP3 archive")
    index_ofs = u64(f.read(8), 0)
    if index_ofs == 0x17:  # XP3 v2 header
        f.seek(0x13)
        vb = f.read(21)
        index_ofs = u64(vb, 13)
    index = b""
    while True:
        f.seek(index_ofs)
        flag = f.read(1)[0]
        if (flag & 0x7F) == 1:
            csize = u64(f.read(8), 0)
            osize = u64(f.read(8), 0)
            index += zlib.decompress(f.read(csize))
        else:
            index += f.read(u64(f.read(8), 0))
        if not (flag & 0x80):
            break
        index_ofs = u64(f.read(8), 0)
    return index


def _segments(sb):
    """This archive family stores a 4-byte prefix before the usual 24-byte
    (offset, packed size, original size) records.  Accept both layouts."""
    for pref in (4, 0):
        if len(sb) >= pref + 24 and (len(sb) - pref) % 24 == 0:
            recs = [(u64(sb, i), u64(sb, i + 8), u64(sb, i + 16))
                    for i in range(pref, len(sb), 24)]
            if all(0 < o and 0 < p for o, p, _ in recs):
                return recs
    return None


def entries(index):
    """Yield (info, segments, real_name).  A custom "eliF" chunk carries the
    real name (adlr + UTF-16LE) for the hash-named File entry that follows."""
    pos = 0
    names = {}
    while pos + 12 <= len(index):
        tag = index[pos:pos + 4]
        size = u64(index, pos + 4)
        body = index[pos + 12:pos + 12 + size]
        if len(body) != size:
            break
        pos += 12 + size
        if tag == b"eliF" and size >= 7:
            adlr = u32(body, 0)
            nlen = struct.unpack_from("<H", body, 4)[0]
            nm = body[6:6 + nlen * 2].decode("utf-16-le", "replace").rstrip("\0")
            names[adlr] = nm
            continue
        if tag != b"File":
            continue
        info = segm = None
        file_adlr = None
        p = 0
        while p + 12 <= len(body):
            st = body[p:p + 4]
            ss = u64(body, p + 4)
            sb = body[p + 12:p + 12 + ss]
            if st == b"info":
                info = {
                    "flags": u32(sb, 0),
                    "osize": u64(sb, 4),
                    "psize": u64(sb, 12),
                    "name": sb[22:22 + struct.unpack_from("<H", sb, 20)[0] * 2].decode("utf-16-le", "replace").rstrip("\0"),
                }
            elif st == b"adlr":
                file_adlr = u32(sb, 0)
            elif st == b"segm":
                segm = _segments(sb)
            p += 12 + ss
        if info and segm:
            info["adlr"] = file_adlr
            yield info, segm, names.get(file_adlr)


def extract(f, segm, zlibbed):
    out = b""
    for off, psize, osize in segm:
        f.seek(off)
        data = f.read(psize)
        if len(data) != psize:
            raise IOError("short segment at %#x" % off)
        if zlibbed:
            data = zlib.decompress(data)
        if len(data) != osize:
            raise IOError("segment size mismatch at %#x" % off)
        out += data
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("archive")
    ap.add_argument("outdir")
    ap.add_argument("--max-bytes", type=int, default=0, help="only entries up to this stored size")
    ap.add_argument("--filter-substr", default=None)
    args = ap.parse_args()

    if os.path.abspath(args.outdir).startswith(os.path.abspath(os.path.dirname(args.archive))):
        raise SystemExit("refusing to extract into the game folder")
    os.makedirs(args.outdir, exist_ok=True)

    with open(args.archive, "rb") as f:
        index = read_index(f)
        n = 0
        for i, (info, segm, real) in enumerate(entries(index)):
            shown = real or info["name"]
            if args.filter_substr and args.filter_substr not in shown:
                continue
            if args.max_bytes and info["psize"] > args.max_bytes:
                continue
            try:
                data = extract(f, segm, bool(info["flags"] & 1))
            except Exception as e:
                print("skip %d %s: %s" % (i, shown, e))
                continue
            safe = "".join(c if c.isalnum() or c in "._-" else "_" for c in shown)[:80] or "unnamed"
            path = os.path.join(args.outdir, "%03d_%s" % (i, safe))
            with open(path, "wb") as o:
                o.write(data)
            print("%s  %d bytes  magic=%r" % (os.path.basename(path), len(data), data[:4]))
            n += 1
        print("%d entries, %d written" % (i + 1, n))


if __name__ == "__main__":
    sys.exit(main())
