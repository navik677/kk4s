#!/usr/bin/env python3
"""saveStruct.dll: the current wamsoft contract.

Usage: python tests/savestruct/smoke.py path/to/kk4s

Boots tests/tjs/savestruct/startup.tjs: ssoIndent / ssoConst / ssoSort /
ssoHidden globals, Scripts / Array / Dictionary.toStructString output with
each option (exact strings from the upstream writer), saveStruct2 and save2
files read back with Scripts.evalStorage / Array.load, Japanese and Ukrainian
text included. No game assets are used.
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
    game = Path(__file__).resolve().parents[1] / "tjs" / "savestruct"
    with tempfile.TemporaryDirectory(prefix="kk4s-savestruct-") as temp:
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
    if "SAVESTRUCT PASS" not in log or result.returncode != 0:
        print("\n".join(l for l in log.splitlines() if "SAVESTRUCT" in l or "xception" in l) or log[-4000:],
              file=sys.stderr)
        print(f"exit {result.returncode}", file=sys.stderr)
        return 1
    print("saveStruct.dll contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
