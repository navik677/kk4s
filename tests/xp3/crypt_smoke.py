#!/usr/bin/env python3
"""Keyless XP3 content encryption is discovered and verified by adlr.

Usage: python tests/xp3/crypt_smoke.py path/to/kk4s

For every scheme family compat/kk4s_xp3crypt.cpp knows, a synthetic data.xp3 is
encrypted with that family (the inverse of the decryptor) while each entry's
adlr stays the adler32 of the plain data, as Kirikiri's packer writes it. The
engine must pick the scheme by itself and run startup.tjs from the archive.
Controls: a plain archive must not get a scheme, and an archive encrypted with
something unknown must be reported, not mis-bound.
"""
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import zlib

MAGIC = b"XP3\r\n \n\x1a\x8bg\x01"
DATA_OFFSET = len(MAGIC) + 8


def chunk(tag: bytes, body: bytes) -> bytes:
    return tag + struct.pack("<Q", len(body)) + body


def build_archive(files, encrypt) -> bytes:
    """files: (name, plain bytes); encrypt(adlr, data) -> stored bytes."""
    offset, entries, data = DATA_OFFSET, b"", b""
    for name, body in files:
        adlr = zlib.adler32(body) & 0xFFFFFFFF
        stored = encrypt(adlr, body)
        encoded = name.encode("utf-16le")
        entries += chunk(b"File",
                         chunk(b"info", struct.pack("<IQQH", 0, len(body), len(body), len(encoded) // 2) + encoded)
                         + chunk(b"segm", struct.pack("<IQQQ", 0, offset, len(stored), len(stored)))
                         + chunk(b"adlr", struct.pack("<I", adlr)))
        offset += len(stored)
        data += stored
    header = MAGIC + struct.pack("<Q", DATA_OFFSET + len(data))
    return header + data + b"\x00" + struct.pack("<Q", len(entries)) + entries


def rotl(v, s):
    s &= 7
    return ((v << s) | (v >> (8 - s))) & 0xFF if s else v


def rotr(v, s):
    s &= 7
    return ((v >> s) | (v << (8 - s))) & 0xFF if s else v


def per_byte(fn):
    """fn(adlr, offset, byte) -> stored byte."""
    return lambda h, data: bytes(fn(h, i, b) for i, b in enumerate(data))


def xor_key(key_of_hash, skip=0):
    return lambda h, data: bytes(b ^ key_of_hash(h) if i >= skip else b for i, b in enumerate(data))


def flyingshine(h, data):
    shift = (h & 0xFF) or 0x0F
    key = ((h >> 8) & 0xFF) or 0xF0
    return bytes(rotl(b, shift) ^ key for b in data)


def seiten_enc(h, off, v):
    key = (h ^ off) & 0xFFFFFFFF
    if key & 8:
        v = (v + ((key >> (key & 0x10)) & 0xFF)) & 0xFF
    if key & 4:
        v = (v - (key & 0xFF)) & 0xFF
    if key & 2:
        ecx = key & 0x18
        v ^= ((key >> ecx) | (key >> (ecx & 8))) & 0xFF
    return v


def okiba(h, off, v):
    if off < 0x65:
        return v ^ ((h >> 4) & 0xFF)
    key = ((h & 0xFF0000) << 8) | ((h & 0xFF000000) >> 8) | ((h & 0xFF00) >> 8) | ((h & 0xFF) << 8)
    return v ^ ((key >> (8 * ((off - 0x65) & 3))) & 0xFF)


def dieselmine(h, off, v):
    k = h & 0xFF
    if off < 123:
        return v ^ ((21 * k) & 0xFF)
    if off < 246:
        return (v - ((-32 * k) & 0xFF)) & 0xFF
    if off < 369:
        return v ^ ((43 * k) & 0xFF)
    return (v - ((-54 * k) & 0xFF)) & 0xFF


def puca_key(h, off):
    table, hash_ = [], h
    for _ in range(8):
        hash_ = (hash_ | 0x80000000) if hash_ & 1 else (hash_ & 0x7FFFFFFF)
        table += list(struct.pack("<I", hash_))
        hash_ >>= 1
    idx = off & 0x3FF
    return (~table[idx // 32] ^ table[idx % 32]) & 0xFF


def tiny_key(h):
    x = h ^ 0x1548E29C
    k = ((x >> 24) ^ (x >> 16) ^ (x >> 8) ^ x) & 0xFF
    return k or 0xD7


FAMILIES = {
    "flyingshine": flyingshine,
    "seiten": per_byte(seiten_enc),
    "okiba": per_byte(okiba),
    "dieselmine": per_byte(dieselmine),
    "damegane": per_byte(lambda h, o, v: v ^ ((h & 0xFF) if o & 1 else (o & 0xFF))),
    "nephrite": per_byte(lambda h, o, v: v ^ ((o & 0xFF) if o & 1 else (h & 0xFF))),
    "exa": per_byte(lambda h, o, v: v ^ ((h >> (o % 5)) & 0xFF)),
    "highrunning": per_byte(lambda h, o, v: v ^ (h & 0xFF) if (h & 0xFF) and o % (h & 0xFF) else v),
    "hibiki": per_byte(lambda h, o, v: v ^ (((h >> 5) if (o & 4) or o <= 0x64 else (h >> 8)) & 0xFF)),
    "pinpoint": lambda h, data: bytes(rotr(b, bin(b).count("1")) for b in data),
    "puca": per_byte(lambda h, o, v: v ^ puca_key(h, o)),
    "tinyfilter": xor_key(tiny_key),
    # one XOR byte per entry, k = f(adlr): fitted formulas
    "xor-hash": xor_key(lambda h: h & 0xFF),
    "xor-not-hash-plus-1": xor_key(lambda h: ((h + 1) & 0xFF) ^ 0xFF),
    "xor-hash-shr3": xor_key(lambda h: (h >> 3) & 0xFF),
    "xor-const": xor_key(lambda h: 0x5A),
    "xor-two-shifts": xor_key(lambda h: (h ^ (h >> 8)) & 0xFF),
    "xor-skip5-shr12": xor_key(lambda h: (h >> 12) & 0xFF, skip=5),
}


def game_files(tag: str) -> list:
    rnd = random.Random(tag)
    files = [("startup.tjs", f'Debug.message("XP3CRYPT_PASS {tag}");\n'.encode("ascii"))]
    for i, size in enumerate((40, 300, 1200, 4000, 9000, 20000, 33000)):
        words = " ".join(rnd.choice(("kirikiri", "kag", "tjs", "layer", "text", "save")) for _ in range(size // 5))
        files.append((f"data{i}.txt", words.encode("ascii")[:size]))
    return files


def run(executable: str, base: Path, tag: str, encrypt) -> str:
    game, root = base / f"game-{tag}", base / f"root-{tag}"
    game.mkdir()
    (game / "data.xp3").write_bytes(build_archive(game_files(tag), encrypt))
    script = base / f"input-{tag}.txt"
    script.write_text("1500 quit\n", encoding="ascii")
    env = os.environ.copy()
    env["KK4S_ROOT"] = str(root)
    env["KK4S_INPUT_SCRIPT"] = str(script)
    subprocess.run([executable, str(game)], env=env, timeout=60, check=False, capture_output=True)
    logs = sorted((root / "logs").glob("kk4s-*.log"))
    return logs[-1].read_text(encoding="utf-8", errors="replace") if logs else ""


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())
    failures = []
    with tempfile.TemporaryDirectory(prefix="kk4s-xp3crypt-") as temp:
        base = Path(temp)
        for tag, encrypt in FAMILIES.items():
            log = run(executable, base, tag, encrypt)
            bound = [l for l in log.splitlines() if "xp3crypt:" in l and " uses " in l]
            if f"XP3CRYPT_PASS {tag}" not in log or not bound:
                failures.append(f"{tag}: not decrypted ({bound or 'no scheme bound'})\n"
                                + "\n".join(l for l in log.splitlines() if "xp3crypt" in l))
        log = run(executable, base, "plain", lambda h, d: d)
        if "XP3CRYPT_PASS plain" not in log or "xp3crypt:" in log:
            failures.append("plain archive: must run without any scheme")
        rnd = random.Random(7)
        log = run(executable, base, "unknown", lambda h, d: bytes(b ^ rnd.randrange(256) for b in d))
        if "no simple scheme fits" not in log or "XP3CRYPT_PASS unknown" in log:
            failures.append("unknown scheme: must be reported and not bound")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"xp3 keyless schemes: PASS ({len(FAMILIES)} families + 2 controls)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
