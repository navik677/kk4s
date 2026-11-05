#!/usr/bin/env python3
"""csvParser.dll and varfile.dll contracts.

Usage: python tests/csvvar/smoke.py path/to/kk4s

Boots tests/tjs/csvvar/startup.tjs: CSV quoting, multi-line fields, doLine, tab
separator, initStorage read modes; var:// files backed by octet variables
(read and write). No game assets are used.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())
    game = Path(__file__).resolve().parents[1] / "tjs" / "csvvar"
    with tempfile.TemporaryDirectory(prefix="kk4s-csvvar-") as temp:
        base = Path(temp)
        root = base / "user-data"
        script = base / "input.txt"
        script.write_text("2500 quit\n", encoding="ascii")
        env = os.environ.copy()
        env["KK4S_ROOT"] = str(root)
        env["KK4S_INPUT_SCRIPT"] = str(script)
        result = subprocess.run([executable, str(game)], env=env, timeout=60, check=False,
                                capture_output=True, text=True)
        logs = sorted((root / "logs").glob("kk4s-*.log"))
        log = logs[-1].read_text(encoding="utf-8", errors="replace") if logs else ""
    if "CSVVAR PASS" not in log or result.returncode != 0:
        print("\n".join(l for l in log.splitlines() if "CSVVAR" in l or "xception" in l) or log[-4000:],
              file=sys.stderr)
        print(f"exit {result.returncode}", file=sys.stderr)
        return 1
    print("csvParser / varfile contracts: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
