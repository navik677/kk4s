#!/usr/bin/env python3
"""Touch -> game coordinates with and without aspect-correct scaling.

Usage: python tests/aspect/smoke.py path/to/kk4s

A 640x480 game on the 1280x720 output: with "keep aspect ratio" (default) the
picture is 960x720 at x = 160, so a tap at the screen centre is (320, 240), a
tap on the left bar is clamped to x = 0 and a tap at 3/8 of the width lands at
x = 213. With keep_aspect=0 in settings.ini the picture is stretched and the
same 3/8 tap lands at x = 240.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

TAPS = [(0.5, 0.5), (0.05, 0.5), (0.375, 0.5)]


def run(executable: str, base: Path, name: str, keep_aspect: bool) -> list:
    game = Path(__file__).resolve().parents[1] / "tjs" / "aspect"
    root = base / name
    root.mkdir()
    if not keep_aspect:
        (root / "settings.ini").write_text("keep_aspect=0\n", encoding="ascii")
    script = base / f"{name}.txt"
    lines = [f"{1500 + i * 300} tap {x} {y}" for i, (x, y) in enumerate(TAPS)]
    script.write_text("\n".join(lines) + f"\n{1500 + len(TAPS) * 300 + 300} quit\n", encoding="ascii")
    env = os.environ.copy()
    env["KK4S_ROOT"] = str(root)
    env["KK4S_INPUT_SCRIPT"] = str(script)
    subprocess.run([executable, str(game)], env=env, timeout=60, check=False, capture_output=True)
    logs = sorted((root / "logs").glob("kk4s-*.log"))
    text = logs[-1].read_text(encoding="utf-8", errors="replace") if logs else ""
    return [(int(a), int(b)) for a, b in re.findall(r"ASPECT_TAP (-?\d+) (-?\d+)", text)]


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="kk4s-aspect-") as temp:
        base = Path(temp)
        fit = run(executable, base, "fit", True)
        stretch = run(executable, base, "stretch", False)
    problems = []
    if fit != [(320, 240), (0, 240), (213, 240)]:
        problems.append(f"keep aspect: taps landed at {fit}, want [(320, 240), (0, 240), (213, 240)]")
    if stretch != [(320, 240), (32, 240), (240, 240)]:
        problems.append(f"stretch: taps landed at {stretch}, want [(320, 240), (32, 240), (240, 240)]")
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    print("aspect-correct scaling and touch mapping: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
