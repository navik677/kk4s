#!/usr/bin/env python3
"""Regression test: a cyclic XP3 index chain must be rejected, not followed forever.

Usage: python tests/xp3/index_cycle_smoke.py path/to/kk4s

Two empty index blocks carry the TVP_XP3_INDEX_CONTINUE flag and point at each
other. A parser that only follows the chain loops until the process is killed;
the parser must detect the repeated index offset and throw a read error.

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


def index_block(next_offset: int) -> bytes:
    # 0x80: raw index data + TVP_XP3_INDEX_CONTINUE
    return b"\x80" + struct.pack("<Q", 0) + struct.pack("<Q", next_offset)


def build_archive() -> bytes:
    block_len = 1 + 8 + 8  # flag, index size, next index offset
    first = DATA_OFFSET
    second = first + block_len
    index = index_block(second) + index_block(first)
    header = MAGIC + struct.pack("<Q", first)
    assert len(header) == DATA_OFFSET
    return header + index


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="kk4s-xp3-cycle-") as temp:
        temp_path = Path(temp)
        game = temp_path / "game"
        root = temp_path / "data"
        game.mkdir()
        (game / "cycle.xp3").write_bytes(build_archive())
        (game / "startup.tjs").write_text(
            'try {\n'
            '    Storages.addAutoPath("cycle.xp3>");\n'
            '    var exists = Storages.isExistentStorage("missing.txt");\n'
            '    Debug.message("CYCLE_XP3_NO_THROW " + exists);\n'
            '} catch(e) {\n'
            '    Debug.message("CYCLE_XP3_CAUGHT");\n'
            '}\n'
            'Debug.message("CYCLE_XP3_DONE");\n',
            encoding="ascii",
        )
        input_script = temp_path / "input.txt"
        input_script.write_text("5000 quit\n", encoding="ascii")

        env = os.environ.copy()
        env["KK4S_ROOT"] = str(root)
        env["KK4S_INPUT_SCRIPT"] = str(input_script)
        env.pop("KK4S_DUMP_BYTECODE", None)
        env.pop("KK4S_KAG_TRACE", None)
        try:
            result = subprocess.run([executable, str(game)], env=env, timeout=45, check=False)
        except subprocess.TimeoutExpired:
            print("FAIL: kk4s did not finish; the cyclic index was not rejected", file=sys.stderr)
            return 1
        logs = sorted((root / "logs").glob("kk4s-*.log"))
        if not logs:
            print("FAIL: kk4s produced no log", file=sys.stderr)
            return 1
        log_text = logs[-1].read_text(encoding="utf-8", errors="replace")
        if "CYCLE_XP3_CAUGHT" not in log_text or "CYCLE_XP3_DONE" not in log_text:
            print(log_text[-4000:], file=sys.stderr)
            return 1
        if result.returncode != 0:
            print(f"FAIL: kk4s exited with {result.returncode}", file=sys.stderr)
            return 1
    print("XP3 cyclic index: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
