#!/usr/bin/env python3
"""textrender.dll (Kirikiri Z's TextRenderBase, a TJS shim in compat/kk4s_plugins.cpp).

Usage: python tests/textrender/smoke.py path/to/kk4s

Boots tests/tjs/textrender/startup.tjs: layout, line breaks, measurement, link
colours, ruby, the draw dispatch, overflow, and the inline commands listed in
KAGZ's textrender.tjs header (%fface; / %f;, %bX %iX %sX %eX, %N; in percent,
#rrggbb;, %pN;, timing commands, backslash escapes). Each character carries the
fields textrender.tjs' drawText reads (face, bold, italic, edge + edgeColor,
shadow + shadowColor). No game assets are used.
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
    game = Path(__file__).resolve().parents[1] / "tjs" / "textrender"
    with tempfile.TemporaryDirectory(prefix="kk4s-textrender-") as temp:
        root = Path(temp) / "user-data"
        script = Path(temp) / "input.txt"
        script.write_text("2500 quit\n", encoding="ascii")
        env = os.environ.copy()
        env["KK4S_ROOT"] = str(root)
        env["KK4S_INPUT_SCRIPT"] = str(script)
        subprocess.run([executable, str(game)], env=env, timeout=60, check=False, capture_output=True)
        logs = sorted((root / "logs").glob("kk4s-*.log"))
        log = logs[-1].read_text(encoding="utf-8", errors="replace") if logs else ""
    if "TextRenderBase synthetic layout/draw: PASS" not in log:
        print("\n".join(l for l in log.splitlines() if "xception" in l or "TextRender" in l)[-3000:] or log[-3000:],
              file=sys.stderr)
        return 1
    print("textrender.dll (TextRenderBase) contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
