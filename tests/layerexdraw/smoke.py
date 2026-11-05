#!/usr/bin/env python3
"""layerExDraw.dll drawn with plutovg instead of GDI+.

Usage: python tests/layerexdraw/smoke.py path/to/kk4s

Boots tests/tjs/layerexdraw/startup.tjs: brush fill, pen stroke, ellipse,
linear gradient, SourceOver, transforms, getColorRegionRects, Path, text
outlines and metrics, clear; pixels read back in krkrs RGBA order.
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
    game = Path(__file__).resolve().parents[1] / "tjs" / "layerexdraw"
    with tempfile.TemporaryDirectory(prefix="kk4s-layerexdraw-") as temp:
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
    if "LXD PASS" not in log or result.returncode != 0:
        print("\n".join(l for l in log.splitlines() if "LXD" in l or "xception" in l) or log[-4000:],
              file=sys.stderr)
        print(f"exit {result.returncode}", file=sys.stderr)
        return 1
    print("layerExDraw.dll: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
