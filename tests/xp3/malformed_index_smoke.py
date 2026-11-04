#!/usr/bin/env python3
"""Regression test: a corrupt XP3 index must be rejected, not read out of bounds.

Usage: python tests/xp3/malformed_index_smoke.py path/to/kk4s

The archive contains one 'File' entry whose first sub-chunk declares a size of
0xFFFFFFF4. That value passes the 32-bit cast check but makes FindChunk's
running offset wrap to zero, so the old code looped and dereferenced memory far
outside the index buffer. The parser must throw a read error instead.

The archive and all runtime data are created in a temporary directory. No real
game content or files are used.
"""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


MAGIC = b"XP3\r\n \n\x1a\x8bg\x01"
DATA_OFFSET = len(MAGIC) + 8  # magic + u64 index offset


def chunk(tag: bytes, body: bytes) -> bytes:
    return tag + struct.pack("<Q", len(body)) + body


def build_archive() -> bytes:
    # A child that claims to be almost 4 GiB large. The old loop computed
    # pos += 12 + 0xFFFFFFF4 -> 0 and walked `start` backwards/off the buffer.
    bogus = b"junk" + struct.pack("<Q", 0xFFFFFFF4)
    file_body = bogus
    index = chunk(b"File", file_body)
    header = MAGIC + struct.pack("<Q", DATA_OFFSET)
    assert len(header) == DATA_OFFSET
    index_record = b"\x00" + struct.pack("<Q", len(index)) + index
    return header + index_record


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="kk4s-xp3-bad-") as temp:
        temp_path = Path(temp)
        game = temp_path / "game"
        root = temp_path / "data"
        game.mkdir()
        (game / "bad.xp3").write_bytes(build_archive())
        (game / "startup.tjs").write_text(
            'try {\n'
            '    Storages.addAutoPath("bad.xp3>");\n'
            '    var exists = Storages.isExistentStorage("missing.txt");\n'
            '    Debug.message("MALFORMED_XP3_NO_THROW " + exists);\n'
            '} catch(e) {\n'
            '    Debug.message("MALFORMED_XP3_CAUGHT");\n'
            '}\n'
            'Debug.message("MALFORMED_XP3_DONE");\n',
            encoding="ascii",
        )
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
        if "MALFORMED_XP3_CAUGHT" not in log_text or "MALFORMED_XP3_DONE" not in log_text:
            print(log_text[-4000:], file=sys.stderr)
            return 1
        if result.returncode != 0:
            print(f"FAIL: kk4s exited with {result.returncode}", file=sys.stderr)
            return 1
    print("XP3 malformed index: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
