#!/usr/bin/env python3
"""Check the windowEx.dll replacement (System / Window / MenuItem members).

Usage: python tests/windowex/smoke.py path/to/kk4s

Boots tests/tjs/windowex/startup.tjs: monitor and metric queries, environment
expansion (APPDATA = the save folder), cursor, and Window members on a window
created after the link. No game assets are used.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

PASS = "windowEx synthetic contract: PASS"


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())
    test_script = Path(__file__).resolve().parents[1] / "tjs" / "windowex" / "startup.tjs"

    with tempfile.TemporaryDirectory(prefix="kk4s-windowex-") as temp:
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
        result = subprocess.run([executable, str(game)], env=env, timeout=45, check=False,
                                capture_output=True, text=True)

        logs = sorted((root / "logs").glob("kk4s-*.log"))
        if not logs:
            print(f"FAIL: kk4s produced no log (exit {result.returncode})", file=sys.stderr)
            print(result.stdout[-2000:], result.stderr[-2000:], file=sys.stderr)
            return 1
        log_text = logs[-1].read_text(encoding="utf-8", errors="replace")
        failures = [line for line in log_text.splitlines() if "windowEx FAIL" in line]
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
