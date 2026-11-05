#!/usr/bin/env python3
"""Independent decryptor and inspector for E-mote 3.0 PSB payloads.

The algorithm was recovered from NekoPara Vol.1 x86 binaries (see
docs/emote_psb_research.md).  It is *not* the XP3 archive filter and it is not
E-mote's ``emote.xp3`` outer wrapper: this tool handles the second, inner layer
that the E-mote reader applies to the PSB container itself.

Recovered behaviour
-------------------
``emoteplayer.dll`` 0x1004b920 (vtable 0x100d7c0c slot 1) and
``emotedriver.dll`` 0x100014d0 (vtable 0x10071880 slot 1) run the same loop:

    if (!cur) {                       // next 32-bit word
        t = x ^ (x << 11);
        x = y; y = z; z = w;
        w = w ^ (w >> 19) ^ t ^ (t >> 8);
        cur = w;
    }
    *p ^= cur & 0xFF;
    cur >>= 8;                        // logical shift, not arithmetic

that is xorshift128 seeded with (123456789, 362436069, 521288629, key).  The
fourth word is the decimal string ``"742877301"`` parsed with
``strtol(s, NULL, 10)``; both DLLs contain that string.  The keystream is
little-endian per word, starts immediately (no skip), and a new word is drawn
only when the current word becomes zero after four byte shifts.

The state is initialised immediately before the call at ``emoteplayer.dll``
0x1004bf93 and the buffer is decrypted once, in place, over

    [ u32(buf+0x08) , u32(buf+0x18) )  ==  [ header_len , off_chunk_offsets )

The first offset is the header-length field (0x28 in every observed file), not
the names field at 0x0C; the two are equal in all observed files and this tool
rejects files where they differ instead of guessing.  Header fields and chunk
data are left untouched.

PSB header used here (all offsets absolute)::

    0x00  "PSB\\0"
    0x04  u16 version (observed 2)     0x06 u16 encrypt flag (must be 0)
    0x08  u32 header length            0x0C names trie
    0x10  strings offset array         0x14 strings data
    0x18  chunk offset array           0x1C chunk length array
    0x20  chunk data                   0x24 root entry
    name trie + root entry lie inside the encrypted region (observed).

Safety
------
Inputs are opened read-only.  ``decrypt`` requires an explicit output path,
refuses to write over the input (even with ``--force``), refuses to replace an
existing file without ``--force``, validates the decrypted structure before
writing, and installs the result with an atomic rename.

Usage
-----
    python3 emote_psb.py inspect FILE.psb [--json]
    python3 emote_psb.py decrypt FILE.psb -o OUT.psb [--force]
    python3 emote_psb.py decrypt FILE.psb -o OUT.psb --xp3-xor none
    python3 emote_psb.py decrypt FILE.psb -o OUT.psb --expect-adler 2c74e840

``--xp3-xor`` defaults to ``auto``: a plain ``PSB\\0`` file is used as is, and an
XP3-filtered file is unwrapped by the single-byte XOR implied by its magic.
That filter is KiriKiri's TinyFilter (key derived from the file's ``adlr``), a
separate layer from this cipher.  The output is always the plain PSB.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import sys
import tempfile
import zlib
from collections import namedtuple

MAGIC = b"PSB\0"
DEFAULT_KEY = 742877301
DEFAULT_SEEDS = (123456789, 362436069, 521288629)
HEADER_SIZE = 0x28
MAX_DEPTH = 64
MAX_ITEMS = 1 << 20

Header = namedtuple(
    "Header",
    "version encrypt header_len off_names off_strings off_strdata "
    "off_choffs off_chlens off_chdata off_entries",
)


class PSBError(Exception):
    """Input is not a PSB this tool understands, or failed validation."""


# --------------------------------------------------------------------- cipher
def crypt_region(data: bytes, start: int, end: int, key: int = DEFAULT_KEY) -> bytes:
    """XOR ``data[start:end]`` with the E-mote xorshift128 keystream.

    Symmetric (XOR), returns a new ``bytes`` and never modifies ``data``.
    """
    if not 0 <= start <= end <= len(data):
        raise PSBError("cipher region %#x..%#x outside a %d-byte buffer" % (start, end, len(data)))
    if not 0 <= key <= 0xFFFFFFFF:
        raise PSBError("key must be a 32-bit unsigned integer")

    out = bytearray(data)
    x, y, z = DEFAULT_SEEDS
    w = key
    cur = 0
    for i in range(start, end):
        if cur == 0:
            t = (x ^ ((x << 11) & 0xFFFFFFFF)) & 0xFFFFFFFF
            x, y, z = y, z, w
            w = (w ^ (w >> 19) ^ t ^ (t >> 8)) & 0xFFFFFFFF
            cur = w
        out[i] ^= cur & 0xFF
        cur >>= 8
    return bytes(out)


# --------------------------------------------------------------------- header
def parse_header(data: bytes) -> Header:
    if len(data) < HEADER_SIZE:
        raise PSBError("file is %d bytes, shorter than a PSB header" % len(data))
    if data[:4] != MAGIC:
        raise PSBError("not a PSB file (magic %r)" % (data[:4],))
    version, encrypt = struct.unpack_from("<HH", data, 4)
    fields = struct.unpack_from("<8I", data, 8)
    return Header(version, encrypt, *fields)


def check_header(h: Header, size: int) -> None:
    if h.version not in (2, 3, 4):
        raise PSBError("unsupported PSB version %d (E-mote 3.0 files here are version 2)" % h.version)
    if h.encrypt != 0:
        raise PSBError(
            "PSB 'encrypt' flag is %d; only 0 is understood (the E-mote cipher "
            "is applied by the reader, not signalled by this field)" % h.encrypt
        )
    if h.header_len != h.off_names:
        raise PSBError(
            "unsupported PSB layout: header length %#x != names offset %#x; "
            "the E-mote reader decrypts from the header-length field, and every "
            "observed file has both equal — refusing to guess" % (h.header_len, h.off_names)
        )
    if h.header_len < HEADER_SIZE:
        raise PSBError("header length %#x is smaller than the fixed header" % h.header_len)
    order = (h.off_names, h.off_strings, h.off_strdata, h.off_choffs, h.off_chlens, h.off_chdata)
    if any(a >= b for a, b in zip(order, order[1:])):
        raise PSBError("PSB section offsets are not strictly increasing: %s" % (order,))
    if h.off_chdata > size:
        raise PSBError("chunk data offset %#x is past the end (%#x)" % (h.off_chdata, size))
    if h.off_entries <= 0 or h.off_entries >= size:
        raise PSBError("root entry offset %#x is outside the file (%#x bytes)" % (h.off_entries, size))


# ---------------------------------------------------------------- primitives
def read_array(data: bytes, pos: int, limit: int) -> tuple[list[int], int]:
    """Compact unsigned-int array (PSB types 0x0D..0x10).

    ``limit`` is the first byte the array may not touch.  Returns values and the
    offset just past the array.
    """
    if not 0 <= pos < limit:
        raise PSBError("array offset %#x outside the allowed range" % pos)
    t = data[pos]
    if not 0x0D <= t <= 0x10:
        raise PSBError("array expected at %#x, found type %#x" % (pos, t))
    count_width = t - 0x0C
    if pos + 1 + count_width > limit:
        raise PSBError("truncated array header at %#x" % pos)
    count = int.from_bytes(data[pos + 1:pos + 1 + count_width], "little")
    if count > MAX_ITEMS:
        raise PSBError("array at %#x claims %d entries" % (pos, count))
    entry_width = data[pos + 1 + count_width] - 0x0C
    if not 1 <= entry_width <= 8:
        raise PSBError("bad array entry width %d at %#x" % (entry_width, pos))
    start = pos + 2 + count_width
    end = start + count * entry_width
    if end > limit:
        raise PSBError("array at %#x overruns its section (%#x > %#x)" % (pos, end, limit))
    vals = [int.from_bytes(data[start + i * entry_width:start + (i + 1) * entry_width], "little")
            for i in range(count)]
    return vals, end


def decode_names(data: bytes, h: Header) -> tuple[list[str], int]:
    """Decode the name trie stored at ``off_names``.

    Three arrays: charset bases, tree (child -> parent), leaves (one per name).
    Walking from a leaf: ``c = tree[idx]``; ``char = c - charset[tree[c]]``.
    """
    limit = min(h.off_entries, h.off_strings)
    charset, p = read_array(data, h.off_names, limit)
    tree, p = read_array(data, p, limit)
    leaves, p = read_array(data, p, limit)
    if not charset or not tree:
        raise PSBError("empty name trie")
    names = []
    for idx in leaves:
        if not 0 <= idx < len(tree):
            raise PSBError("name leaf %#x outside the trie" % idx)
        chars = []
        c = tree[idx]
        while c != 0:
            if not 0 <= c < len(tree):
                raise PSBError("name trie node %#x outside the trie" % c)
            parent = tree[c]
            if not 0 <= parent < len(charset):
                raise PSBError("name trie base %#x outside the charset" % parent)
            chars.append(c - charset[parent])
            idx = c
            c = tree[idx]
            if len(chars) > 4096:
                raise PSBError("name trie loop")
        names.append(bytes(reversed(chars)).decode("utf-8", "replace"))
    return names, p


def decode_strings(data: bytes, h: Header) -> list[str]:
    offs, _ = read_array(data, h.off_strings, h.off_strdata)
    out = []
    for o in offs:
        s = h.off_strdata + o
        if not h.off_strdata <= s < h.off_choffs:
            raise PSBError("string offset %#x outside the string data" % o)
        e = data.find(b"\0", s, h.off_choffs)
        if e < 0:
            raise PSBError("unterminated string at %#x" % s)
        out.append(data[s:e].decode("utf-8", "replace"))
    return out


def decode_chunks(data: bytes, h: Header) -> tuple[list[int], list[int]]:
    offs, _ = read_array(data, h.off_choffs, h.off_chlens)
    lens, _ = read_array(data, h.off_chlens, h.off_chdata)
    if len(offs) != len(lens):
        raise PSBError("chunk offset/length arrays differ in size (%d/%d)" % (len(offs), len(lens)))
    for o, n in zip(offs, lens):
        if h.off_chdata + o + n > len(data):
            raise PSBError("chunk %#x+%#x runs past the end of the file" % (o, n))
    return offs, lens


# --------------------------------------------------------------------- values
def read_value(data: bytes, pos: int, names: list[str], strings: list[str],
               nchunks: int, depth: int = 0):
    """Decode one PSB value, bounds-checked, with a recursion limit."""
    if depth > MAX_DEPTH:
        raise PSBError("value nesting deeper than %d" % MAX_DEPTH)
    if not 0 <= pos < len(data):
        raise PSBError("value offset %#x outside the file" % pos)
    t = data[pos]

    if t == 0x00:
        return "undefined"
    if t == 0x01:
        return None
    if t == 0x02:
        return False
    if t == 0x03:
        return True
    if 0x04 <= t <= 0x0C:
        n = t - 4
        if pos + 1 + n > len(data):
            raise PSBError("truncated integer at %#x" % pos)
        return int.from_bytes(data[pos + 1:pos + 1 + n], "little", signed=True) if n else 0
    if 0x0D <= t <= 0x10:
        vals, _ = read_array(data, pos, len(data))
        return vals
    if 0x15 <= t <= 0x18:
        n = t - 0x14
        if pos + 1 + n > len(data):
            raise PSBError("truncated string index at %#x" % pos)
        idx = int.from_bytes(data[pos + 1:pos + 1 + n], "little")
        if idx >= len(strings):
            raise PSBError("string index %d outside %d strings" % (idx, len(strings)))
        return strings[idx]
    if 0x19 <= t <= 0x1C:
        n = t - 0x18
        if pos + 1 + n > len(data):
            raise PSBError("truncated resource index at %#x" % pos)
        idx = int.from_bytes(data[pos + 1:pos + 1 + n], "little")
        if idx >= nchunks:
            raise PSBError("resource index %d outside %d chunks" % (idx, nchunks))
        return ("resource", idx)
    if t == 0x1D:
        return 0.0
    if t == 0x1E:
        if pos + 5 > len(data):
            raise PSBError("truncated float at %#x" % pos)
        return struct.unpack_from("<f", data, pos + 1)[0]
    if t == 0x1F:
        if pos + 9 > len(data):
            raise PSBError("truncated double at %#x" % pos)
        return struct.unpack_from("<d", data, pos + 1)[0]
    if t == 0x20:  # array: offsets, then values at base+offset
        offs, base = read_array(data, pos + 1, len(data))
        return [read_value(data, base + o, names, strings, nchunks, depth + 1) for o in offs]
    if t == 0x21:  # object: name indices, offsets, then values at base+offset
        nidx, p = read_array(data, pos + 1, len(data))
        offs, base = read_array(data, p, len(data))
        if len(nidx) != len(offs):
            raise PSBError("object at %#x has %d names and %d values" % (pos, len(nidx), len(offs)))
        out = {}
        for n, o in zip(nidx, offs):
            if n >= len(names):
                raise PSBError("name index %d outside %d names" % (n, len(names)))
            out[names[n]] = read_value(data, base + o, names, strings, nchunks, depth + 1)
        return out
    if 0x22 <= t <= 0x25:
        n = t - 0x21
        if pos + 1 + n > len(data):
            raise PSBError("truncated extra index at %#x" % pos)
        return ("extra", int.from_bytes(data[pos + 1:pos + 1 + n], "little"))
    raise PSBError("unknown value type %#x at %#x" % (t, pos))


# ------------------------------------------------------------------ XP3 layer
def unwrap_xp3(data: bytes, mode):
    """Remove the archive-layer single-byte XOR.

    ``mode`` is ``"auto"``, ``"none"`` or an integer 0..255.  Returns
    ``(unwrapped_data, key_byte)``.
    """
    if mode == "none":
        return data, 0
    if mode == "auto":
        if data[:4] == MAGIC:
            return data, 0
        if len(data) < 4:
            raise PSBError("file is too short to detect the archive filter")
        k = data[0] ^ MAGIC[0]
        if any(data[i] ^ k != MAGIC[i] for i in range(4)):
            raise PSBError("cannot detect the XP3 filter: magic stays %r (use --xp3-xor none)"
                           % (bytes(b ^ k for b in data[:4]),))
        return bytes(b ^ k for b in data), k
    k = int(mode)
    if not 0 <= k <= 255:
        raise PSBError("--xp3-xor must be 'auto', 'none' or a byte value")
    return bytes(b ^ k for b in data), k


def parse_xp3_mode(text: str):
    if text in ("auto", "none"):
        return text
    return _uint(text)


# --------------------------------------------------------------------- report
def decrypt_psb(raw: bytes, key: int = DEFAULT_KEY, xp3_xor: str = "auto",
                expect_adler: int | None = None) -> tuple[bytes, dict]:
    """Validate and decrypt an in-memory PSB file.  Returns (plain, report)."""
    data, xkey = unwrap_xp3(raw, xp3_xor)
    if expect_adler is not None:
        got = zlib.adler32(data) & 0xFFFFFFFF
        if got != expect_adler:
            raise PSBError("adler32 of the stored file is %08x, expected %08x" % (got, expect_adler))

    h = parse_header(data)
    check_header(h, len(data))
    plain = crypt_region(data, h.header_len, h.off_choffs, key)

    names, trie_end = decode_names(plain, h)
    strings = decode_strings(plain, h)
    coffs, _ = decode_chunks(data, h)
    root = read_value(plain, h.off_entries, names, strings, len(coffs))

    report = {
        "version": h.version,
        "header_len": h.header_len,
        "offsets": {
            "names": h.off_names,
            "strings": h.off_strings,
            "strings_data": h.off_strdata,
            "chunk_offsets": h.off_choffs,
            "chunk_lengths": h.off_chlens,
            "chunk_data": h.off_chdata,
            "entries": h.off_entries,
        },
        "encrypted_region": {
            "start": h.header_len,
            "end": h.off_choffs,
            "size": h.off_choffs - h.header_len,
        },
        "names": len(names),
        "strings": len(strings),
        "resources": len(coffs),
        "trie_end": trie_end,
        "trie_tight": trie_end == h.off_entries,
        "root_type": type(root).__name__,
        "root_keys": list(root) if isinstance(root, dict) else None,
        "xp3_filter": xkey,
        "adler32_file": zlib.adler32(data) & 0xFFFFFFFF,
        "adler32_plain": zlib.adler32(plain) & 0xFFFFFFFF,
        "sha256_file": hashlib.sha256(data).hexdigest(),
        "sha256_plain": hashlib.sha256(plain).hexdigest(),
    }
    return plain, report


# ------------------------------------------------------------------------ I/O
def _uint(text: str) -> int:
    s = text.strip()
    try:
        return int(s, 16) if s.lower().startswith("0x") else int(s, 0)
    except ValueError:
        raise PSBError("not an integer: %r (use decimal or 0x...)" % text)


def _hex_uint(text: str) -> int:
    """adler32 values are shown as hex; accept bare hex as well as 0x-prefixed."""
    s = text.strip().lower()
    try:
        return int(s, 16)
    except ValueError:
        raise PSBError("not a hex value: %r" % text)


def same_file(a: str, b: str) -> bool:
    try:
        return os.path.samefile(a, b)
    except OSError:
        return os.path.realpath(a) == os.path.realpath(b)


def write_atomic(path: str, data: bytes, force: bool) -> None:
    if os.path.exists(path) and not force:
        raise PSBError("output %s already exists (pass --force to replace it)" % path)
    parent = os.path.dirname(os.path.abspath(path)) or "."
    if not os.path.isdir(parent):
        raise PSBError("output directory %s does not exist" % parent)
    fd, tmp = tempfile.mkstemp(dir=parent, prefix=".emote_psb.", suffix=".tmp")
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.chmod(tmp, 0o644)
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def _print_report(path: str, out: str | None, rep: dict) -> None:
    reg = rep["encrypted_region"]
    print("%s: PSB version %d, header %#x" % (path, rep["version"], rep["header_len"]))
    print("  encrypted region  %#x..%#x (%d bytes)" % (reg["start"], reg["end"], reg["size"]))
    print("  names %d, strings %d, resources %d, trie end %#x%s"
          % (rep["names"], rep["strings"], rep["resources"], rep["trie_end"],
             "" if rep["trie_tight"] else " (not at entries)"))
    if rep["root_keys"] is not None:
        print("  root keys: %s" % ", ".join(rep["root_keys"]))
    print("  archive filter   %s" % ("none" if rep["xp3_filter"] == 0 else "0x%02x" % rep["xp3_filter"]))
    print("  adler32 file     %08x" % rep["adler32_file"])
    print("  sha256 plain     %s" % rep["sha256_plain"])
    if out:
        print("  wrote            %s" % out)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        prog="emote_psb.py",
        description="Decrypt or inspect the inner E-mote 3.0 PSB cipher.",
        epilog="The input is never modified; 'decrypt' needs an explicit output path.",
    )
    sub = ap.add_subparsers(dest="cmd", required=True)

    d = sub.add_parser("decrypt", help="decrypt into a new file")
    d.add_argument("input")
    d.add_argument("-o", "--output", required=True)
    d.add_argument("--key", default=str(DEFAULT_KEY), help="32-bit key (default: %d)" % DEFAULT_KEY)
    d.add_argument("--xp3-xor", default="auto", help="archive filter: auto (default), none, or 0xNN")
    d.add_argument("--expect-adler", default=None,
                   help="expected adler32 of the stored file (XP3 index adlr), hex or decimal")
    d.add_argument("--force", action="store_true", help="replace an existing output file")

    i = sub.add_parser("inspect", help="decrypt in memory and print a summary (writes nothing)")
    i.add_argument("input")
    i.add_argument("--key", default=str(DEFAULT_KEY))
    i.add_argument("--xp3-xor", default="auto")
    i.add_argument("--json", action="store_true")

    args = ap.parse_args(argv)
    try:
        key = _uint(args.key)
        if not 0 <= key <= 0xFFFFFFFF:
            raise PSBError("--key must be a 32-bit unsigned integer")
        xp3 = parse_xp3_mode(args.xp3_xor)
        raw_adler = getattr(args, "expect_adler", None)
        expected = _hex_uint(raw_adler) if raw_adler is not None else None

        with open(args.input, "rb") as f:
            raw = f.read()
        plain, rep = decrypt_psb(raw, key=key, xp3_xor=xp3, expect_adler=expected)

        if args.cmd == "decrypt":
            if same_file(args.input, args.output):
                raise PSBError("refusing to write over the input file")
            write_atomic(args.output, plain, args.force)
            _print_report(args.input, args.output, rep)
        else:
            if args.json:
                print(json.dumps(rep, ensure_ascii=False, indent=2))
            else:
                _print_report(args.input, None, rep)
        return 0
    except (PSBError, OSError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
