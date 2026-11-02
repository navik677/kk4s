#!/usr/bin/env python3
"""End-to-end smoke test for XP3v2 eliF filename mapping in kk4s.

Usage: python tests/xp3/elif_smoke.py path/to/kk4s

The archive and all runtime data are created in a temporary directory. No real
game content or files are used.
"""
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib


MAGIC = b"XP3\r\n \n\x1a\x8bg\x01"


def chunk(tag: bytes, body: bytes) -> bytes:
    return tag + struct.pack("<Q", len(body)) + body


def build_archive() -> bytes:
    script = b'Debug.message("XP3_ELIF_SMOKE_PASS");\n'
    name = "startup.tjs"
    fake_name = hashlib.md5(name.encode("utf-8")).hexdigest()
    key = zlib.adler32(script) & 0xFFFFFFFF

    # XP3v2 data begins after the 0x28-byte version header.
    data_offset = 0x28
    index_offset = data_offset + len(script)
    elif_entry = struct.pack("<IH", key, len(name)) + name.encode("utf-16le") + b"\0\0"

    info = struct.pack("<IQQH", 0, len(script), len(script), len(fake_name))
    info += fake_name.encode("utf-16le")
    segments = struct.pack("<IQQQ", 0, data_offset, len(script), len(script))
    adlr = struct.pack("<I", key)
    file_entry = chunk(b"File", chunk(b"info", info) + chunk(b"segm", segments) + chunk(b"adlr", adlr))
    index = zlib.compress(chunk(b"eliF", elif_entry) + file_entry)

    # The XP3v2 header points to a table marker at 0x17. Its continuation
    # record points at the actual compressed index table.
    header = MAGIC + struct.pack("<Q", 0x17) + struct.pack("<I", 1)
    header += struct.pack("<Q", 0x80) + b"\0" + struct.pack("<Q", index_offset)
    assert len(header) == data_offset
    index_record = b"\x01" + struct.pack("<QQ", len(index), len(zlib.decompress(index))) + index
    return header + script + index_record


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="kk4s-xp3-elif-") as temp:
        temp_path = Path(temp)
        game = temp_path / "game"
        root = temp_path / "data"
        game.mkdir()
        (game / "data.xp3").write_bytes(build_archive())
        input_script = temp_path / "input.txt"
        input_script.write_text("5000 quit\n", encoding="ascii")

        env = os.environ.copy()
        env["KK4S_ROOT"] = str(root)
        env["KK4S_INPUT_SCRIPT"] = str(input_script)
        env.pop("KK4S_DUMP_BYTECODE", None)
        env.pop("KK4S_KAG_TRACE", None)
        result = subprocess.run([executable, str(game)], env=env, timeout=45, check=False)
        logs = sorted((root / "logs").glob("kk4s-*.log"))
        if not logs:
            print("FAIL: kk4s produced no log", file=sys.stderr)
            return 1
        log_text = logs[-1].read_text(encoding="utf-8", errors="replace")
        if "XP3_ELIF_SMOKE_PASS" not in log_text:
            print(log_text[-4000:], file=sys.stderr)
            return 1
        if result.returncode != 0:
            print(f"FAIL: kk4s exited with {result.returncode}", file=sys.stderr)
            return 1
    print("XP3v2 eliF filename mapping: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
