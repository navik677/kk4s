#!/usr/bin/env python3
"""Regression test: an XP3 entry with no 'segm' records is a zero-length file.

Usage: python tests/xp3/empty_entry_smoke.py path/to/kk4s

An empty 'segm' chunk is a valid way for a packer to describe a zero-byte
entry. kk4s used to dereference Segments[0] unconditionally in
tTVPXP3ArchiveStream, which crashed (0xC0000005) as soon as such an entry was
opened. Opening, repeated reads, Seek(0) and a seek past the end must behave
like an ordinary empty file.

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


def info(name: str, org_size: int, arc_size: int) -> bytes:
    encoded = name.encode("utf-16le")
    return struct.pack("<IQQH", 0, org_size, arc_size, len(encoded) // 2) + encoded


def build_archive() -> bytes:
    script = (
        b'var a = new Array();\n'
        b'a.load("empty.txt");\n'
        b'a.load("empty.txt");\n'
        b'a.load("zero.bin");\n'
        b'try {\n'
        b'    Scripts.execStorage("empty.txt", "o0");\n'
        b'    Debug.message("EMPTY_SEEK_ZERO_OK");\n'
        b'} catch(e) {\n'
        b'    Debug.message("EMPTY_SEEK_ZERO_FAIL");\n'
        b'}\n'
        b'try {\n'
        b'    Scripts.execStorage("empty.txt", "o5");\n'
        b'    Debug.message("EMPTY_SEEK_PAST_NOT_REJECTED");\n'
        b'} catch(e) {\n'
        b'    Debug.message("EMPTY_SEEK_PAST_REJECTED");\n'
        b'}\n'
        b'Debug.message("XP3_ZERO_SEGMENT_PASS");\n'
    )
    index_offset = DATA_OFFSET + len(script)

    startup = chunk(
        b"File",
        chunk(b"info", info("startup.tjs", len(script), len(script)))
        + chunk(b"segm", struct.pack("<IQQQ", 0, DATA_OFFSET, len(script), len(script)))
        + chunk(b"adlr", struct.pack("<I", 0)),
    )
    # Zero-byte entry with an empty 'segm' chunk (this is the regression case).
    empty = chunk(
        b"File",
        chunk(b"info", info("empty.txt", 0, 0))
        + chunk(b"segm", b"")
        + chunk(b"adlr", struct.pack("<I", 0)),
    )
    # Zero-byte entry described by a single zero-size segment (control case).
    zero = chunk(
        b"File",
        chunk(b"info", info("zero.bin", 0, 0))
        + chunk(b"segm", struct.pack("<IQQQ", 0, 0, 0, 0))
        + chunk(b"adlr", struct.pack("<I", 0)),
    )

    index = startup + empty + zero
    header = MAGIC + struct.pack("<Q", index_offset)
    assert len(header) == DATA_OFFSET
    index_record = b"\x00" + struct.pack("<Q", len(index)) + index
    return header + script + index_record


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="kk4s-xp3-empty-") as temp:
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
        for marker in ("XP3_ZERO_SEGMENT_PASS", "EMPTY_SEEK_ZERO_OK",
                       "EMPTY_SEEK_PAST_REJECTED"):
            if marker not in log_text:
                print(f"FAIL: missing {marker}", file=sys.stderr)
                print(log_text[-4000:], file=sys.stderr)
                return 1
        if result.returncode != 0:
            print(f"FAIL: kk4s exited with {result.returncode}", file=sys.stderr)
            return 1
    print("XP3 zero-segment entry: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
