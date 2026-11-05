#!/usr/bin/env python3
"""Hashed-name XP3 whose entries all carry one constant adlr.

Usage: python tests/xp3/constadlr_smoke.py path/to/kk4s

Kirikiri never checks an entry's adlr; it only feeds the extraction filter.
Patch tools write one constant there (NekoPara Vol. 1's patch.xp3), so
adler32 can never confirm a filter. kk4s then accepts a filter only when the
decrypted samples start with known file formats (PNG, OggS, PSB, a UTF-16
BOM, ...) and the stored bytes did not. This builds such an archive (eliF
names, TinyFilter with the constant's key), checks that its startup.tjs runs,
and that the same archive XOR-ed with a wrong key is not bound.
"""
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from crypt_smoke import MAGIC, DATA_OFFSET, chunk  # noqa: E402

ADLR = 0x689792E4  # any constant works; this one gives TinyFilter key 0xAA


def tiny_key(h):
    x = h ^ 0x1548E29C
    k = ((x >> 24) ^ (x >> 16) ^ (x >> 8) ^ x) & 0xFF
    return k or 0xD7


def files():
    rnd = random.Random(5)
    out = [("startup.tjs", "﻿Debug.message(\"CONSTADLR_PASS\");\n".encode("utf-16le"))]
    for i, head in enumerate((b"\x89PNG\r\n\x1a\n", b"OggS", b"PSB\0", b"\x89PNG\r\n\x1a\n", b"OggS", b"PSB\0",
                              b"\x89PNG\r\n\x1a\n", b"RIFF")):
        out.append((f"asset{i}.bin", head + bytes(rnd.randrange(256) for _ in range(300 + 400 * i))))
    return out


def build(key):
    offset, entries, data = DATA_OFFSET, b"", b""
    for n, (name, body) in enumerate(files()):
        stored = bytes(b ^ key for b in body)
        hashed = f"{n:032x}".encode("utf-16le")  # the File record's own name is a hash
        entries += chunk(b"File",
                         chunk(b"info", struct.pack("<IQQH", 0, len(body), len(body), len(hashed) // 2) + hashed)
                         + chunk(b"segm", struct.pack("<IQQQ", 0, offset, len(stored), len(stored)))
                         + chunk(b"adlr", struct.pack("<I", ADLR)))
        real = name.encode("utf-16le")
        entries += chunk(b"eliF", struct.pack("<IH", ADLR, len(real) // 2) + real)
        offset += len(stored)
        data += stored
    return MAGIC + struct.pack("<Q", DATA_OFFSET + len(data)) + data + b"\x00" + struct.pack("<Q", len(entries)) + entries


def run(executable, base, tag, key):
    game, root = base / f"game-{tag}", base / f"root-{tag}"
    game.mkdir()
    (game / "data.xp3").write_bytes(build(key))
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
    problems = []
    with tempfile.TemporaryDirectory(prefix="kk4s-constadlr-") as temp:
        base = Path(temp)
        log = run(executable, base, "tiny", tiny_key(ADLR))
        if "CONSTADLR_PASS" not in log or "verified by file signatures" not in log:
            problems.append("constant adlr + TinyFilter: not bound / startup.tjs did not run\n  " +
                            "\n  ".join(l for l in log.splitlines() if "cxdec" in l))
        log = run(executable, base, "wrong", tiny_key(ADLR) ^ 0x55)
        if "CONSTADLR_PASS" in log or "verified by file signatures" in log:
            problems.append("a wrong key must not be bound")
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    print("hashed-name archive with a constant adlr: PASS (bound by file signatures; wrong key rejected)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
