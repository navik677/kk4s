#!/usr/bin/env python3
"""Check the krkrz/krkr2 native members kk4s installs for every game.

Usage: python tests/natives/smoke.py path/to/kk4s

Boots tests/tjs/natives/startup.tjs without linking plugins: the Storages /
System / Window members Kirikiri Z has natively, System.inputString (fed by
$KK4S_INPUT_STRING here), Debug.visible. No game assets are used.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

PASS = "engine natives: PASS"


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())
    test_script = Path(__file__).resolve().parents[1] / "tjs" / "natives" / "startup.tjs"

    with tempfile.TemporaryDirectory(prefix="kk4s-natives-") as temp:
        temp_path = Path(temp)
        game = temp_path / "game"
        root = temp_path / "user-data"
        game.mkdir()
        (game / "startup.tjs").write_bytes(test_script.read_bytes())
        input_script = temp_path / "input.txt"
        input_script.write_text("3000 quit\n", encoding="ascii")

        env = os.environ.copy()
        env["KK4S_ROOT"] = str(root)
        env["KK4S_INPUT_SCRIPT"] = str(input_script)
        env.pop("KK4S_DUMP_BYTECODE", None)
        env.pop("KK4S_KAG_TRACE", None)
        env["KK4S_INPUT_STRING"] = "名前テスト"
        result = subprocess.run([executable, str(game)], env=env, timeout=45, check=False,
                                capture_output=True, text=True)

        logs = sorted((root / "logs").glob("kk4s-*.log"))
        if not logs:
            print(f"FAIL: kk4s produced no log (exit {result.returncode})", file=sys.stderr)
            print(result.stdout[-2000:], result.stderr[-2000:], file=sys.stderr)
            return 1
        log_text = logs[-1].read_text(encoding="utf-8", errors="replace")
        failures = [line for line in log_text.splitlines() if "natives FAIL" in line]
        if failures or PASS not in log_text:
            print("\n".join(failures) or log_text[-5000:], file=sys.stderr)
            return 1
        if "(exception)" in log_text or result.returncode != 0:
            print(log_text[-5000:], file=sys.stderr)
            print(f"FAIL: kk4s exited with {result.returncode}", file=sys.stderr)
            return 1
    print(PASS)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
