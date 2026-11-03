# -*- coding: utf-8 -*-
"""Run the generic TextStream encoding detection against synthetic fixtures.

Usage: python tests/textstream/smoke.py path/to/kk4s
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())
    test_script = Path(__file__).resolve().parents[1] / "tjs" / "textstream" / "startup.tjs"

    with tempfile.TemporaryDirectory(prefix="kk4s-textstream-") as temp:
        temp_path = Path(temp)
        game = temp_path / "game"
        root = temp_path / "user-data"
        game.mkdir()
        root.mkdir()
        (game / "startup.tjs").write_bytes(test_script.read_bytes())
        (game / "ascii.txt").write_bytes(b"Hello, world!\n")
        (game / "sjis.txt").write_bytes("日本語テスト\n".encode("shift_jis"))
        (game / "utf8.txt").write_bytes("café\n".encode("utf-8"))
        (game / "utf8bom.txt").write_bytes(b"\xef\xbb\xbf" + "BOMテキスト\n".encode("utf-8"))
        (game / "empty.txt").write_bytes(b"")
        (game / "bad.txt").write_bytes(bytes([0x85]))
        (game / "bad2.txt").write_bytes(bytes([0x85]))
        (game / "trunc-utf8.txt").write_bytes(bytes([0xE0]))
        (game / "trunc-sjis.txt").write_bytes(bytes([0x82]))
        (game / "trunc-gbk.txt").write_bytes(bytes([0x85]))
        (game / "gbk.txt").write_bytes("中文测试\n".encode("gbk"))

        input_script = temp_path / "input.txt"
        input_script.write_text("5000 quit\n", encoding="ascii")

        env = os.environ.copy()
        env["KK4S_ROOT"] = str(root)
        env["KK4S_INPUT_SCRIPT"] = str(input_script)
        env.pop("KK4S_DUMP_BYTECODE", None)
        env.pop("KK4S_KAG_TRACE", None)
        result = subprocess.run([executable, str(game)], env=env, timeout=45, check=False,
                                capture_output=True, text=True)

        logs = sorted((root / "logs").glob("kk4s-*.log"))
        if not logs:
            print(f"FAIL: kk4s produced no log (exit {result.returncode})", file=sys.stderr)
            if result.stdout:
                print(result.stdout[-2000:], file=sys.stderr)
            if result.stderr:
                print(result.stderr[-2000:], file=sys.stderr)
            return 1
        log_text = logs[-1].read_text(encoding="utf-8", errors="replace")
        if "TextStream encoding tests: PASS" not in log_text:
            print(log_text[-6000:], file=sys.stderr)
            return 1
        if result.returncode not in (0,):
            print(f"FAIL: kk4s exited with {result.returncode}", file=sys.stderr)
            print(log_text[-4000:], file=sys.stderr)
            return 1
    print("TextStream encoding tests: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
