#!/usr/bin/env python3
"""psdfile.dll: the PSD class and the psd:// storage.

Usage: python tests/psdfile/smoke.py path/to/kk4s

Writes a synthetic PSD (make_psd.py: two layers, merged image) next to
tests/tjs/psdfile/startup.tjs in a temporary game folder and checks size,
layer names, getLayerData (RGBA order), getBlend and psd://.../blue.bmp.
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
    here = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="kk4s-psdfile-") as temp:
        base = Path(temp)
        game = base / "game"
        game.mkdir()
        (game / "startup.tjs").write_bytes((here.parent / "tjs" / "psdfile" / "startup.tjs").read_bytes())
        subprocess.run([sys.executable, str(here / "make_psd.py"), str(game / "test.psd")], check=True)
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
    if "PSD PASS" not in log or result.returncode != 0:
        print("\n".join(l for l in log.splitlines() if "PSD" in l or "xception" in l) or log[-4000:],
              file=sys.stderr)
        print(f"exit {result.returncode}", file=sys.stderr)
        return 1
    print("psdfile.dll: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
