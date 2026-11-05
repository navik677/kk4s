#!/usr/bin/env python3
"""extrans.dll transitions run for real instead of falling back to a cross-fade.

Usage: python tests/extrans/smoke.py path/to/kk4s

Boots tests/tjs/extrans/startup.tjs, starts each transition of krkrz's extrans
plugin (wave, mosaic, turn, rotatezoom, rotatevanish, rotateswap, ripple) on a
640x480 layer going from white to black, takes a screenshot half-way and checks
what only that transition can produce:
- wave / rotateswap: the background colour 0xff8000 shows (and red and blue are
  not swapped: krkrs pixels are RGBA, option colours are 0xRRGGBB)
- mosaic: the 2 px checkerboard of the first image has become large blocks
- turn / rotatezoom / rotatevanish: pure white and pure black at once
- ripple: from a 16 px checkerboard, many samples show the other colour than
  the square they lie in (displacement); the same cross-fade shows none
- crossfade (controls): one uniform grey from white; the checkerboard in place
Every transition must end with onTransitionCompleted, and the log must not say
"-> crossfade". (Horizontal stripes showed no vertical displacement in ripple;
see docs/HANDOFF.md.)
"""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

BG = "0xff8000"
METHODS = [
    # (name, method, options, checkerboard cell in px or 0 for plain white)
    ("wave", "wave", f"%[bgcolor1: {BG}, bgcolor2: {BG}, maxh: 80]", 0),
    ("mosaic", "mosaic", "%[maxsize: 40]", 2),
    ("turn", "turn", f"%[bgcolor: {BG}]", 0),
    ("rotatezoom", "rotatezoom", "%[]", 0),
    ("rotatevanish", "rotatevanish", "%[]", 0),
    ("rotateswap", "rotateswap", f"%[bgcolor: {BG}]", 0),
    ("ripple", "ripple", "%[]", 16),
    ("crossfade", "crossfade", "%[]", 0),
    ("crossfade-checker", "crossfade", "%[]", 16),
]
STEP = 2400  # ms per transition (time 1600 + margin)


def read_bmp(path: Path):
    data = path.read_bytes()
    offset, = struct.unpack_from("<I", data, 10)
    width, height, _, bpp = struct.unpack_from("<iiHH", data, 18)
    if bpp != 32:
        raise ValueError(f"{path.name}: {bpp} bpp")
    rows = abs(height)
    stride = width * 4

    def pixel(x, y):
        row = y if height < 0 else rows - 1 - y
        b, g, r, _ = data[offset + row * stride + x * 4: offset + row * stride + x * 4 + 4]
        return r, g, b
    return width, rows, pixel


def game_pixels(path: Path):
    """The 640x480 game picture inside the letterboxed screenshot, every 4th pixel."""
    width, height, pixel = read_bmp(path)
    scale = min(width / 640, height / 480)
    ox, oy = (width - 640 * scale) / 2, (height - 480 * scale) / 2
    out = {}
    for gy in range(0, 480, 4):
        for gx in range(0, 640, 2):
            out[gx, gy] = pixel(int(ox + (gx + 0.5) * scale), int(oy + (gy + 0.5) * scale))
    return out


def near(c, want, tol=40):
    return all(abs(a - b) <= tol for a, b in zip(c, want))


def displaced(px: dict, cell: int = 16) -> int:
    """Samples whose darkness differs from the checkerboard square they lie in."""
    count = 0
    for (x, y), c in px.items():
        if x % cell in (0, 1, cell - 2, cell - 1) or y % cell in (0, 1, 2, cell - 3, cell - 2, cell - 1):
            continue  # square borders: scaling of the screenshot
        black_square = (x - (y // cell) % 2 * cell) % (cell * 2) < cell
        if (sum(c) < 60) != black_square:
            count += 1
    return count


def check(method: str, px: dict):
    colours = list(px.values())
    orange = sum(near(c, (255, 128, 0)) for c in colours)
    azure = sum(near(c, (0, 128, 255)) for c in colours)
    white = sum(near(c, (255, 255, 255), 24) for c in colours)
    black = sum(near(c, (0, 0, 0), 24) for c in colours)
    n = len(colours)
    if method in ("wave", "rotateswap"):
        if orange < n // 200 or azure:
            return f"{method}: background 0xff8000 not shown right (orange {orange}, azure {azure} of {n})"
    elif method == "mosaic":
        # neighbours 2 px apart differ everywhere in the checkerboard; in blocks they rarely do
        changes = sum(px[x, y] != px[x + 2, y] for (x, y) in px if x + 2 < 640)
        if changes > n // 4:
            return f"{method}: no blocks half-way ({changes} colour changes in {n} samples)"
    elif method == "crossfade":
        if white > n // 50 or black > n // 50:
            return f"crossfade control is not uniform (white {white}, black {black})"
    elif method == "crossfade-checker":
        moved = displaced(px)
        if moved > n // 100:
            return f"crossfade control moved checkerboard squares ({moved} of {n})"
    elif method == "ripple":
        moved = displaced(px)
        if moved < n // 20:
            return f"ripple: no displacement half-way ({moved} of {n} samples off their square)"
    else:
        if white < n // 200 or black < n // 200:
            return f"{method}: looks like a cross-fade half-way (white {white}, black {black} of {n})"
    return None


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())
    game = Path(__file__).resolve().parents[1] / "tjs" / "extrans"
    problems = []
    with tempfile.TemporaryDirectory(prefix="kk4s-extrans-") as temp:
        base = Path(temp)
        root = base / "user-data"
        lines = []
        for i, (name, method, opts, cell) in enumerate(METHODS):
            t = 1500 + i * STEP
            lines.append(f"{t} eval start('{method}', {opts}, {cell})")
            lines.append(f"{t + 800} shot {name}.bmp")
        lines.append(f"{1500 + len(METHODS) * STEP + 300} quit")
        script = base / "input.txt"
        script.write_text("\n".join(lines) + "\n", encoding="ascii")
        env = os.environ.copy()
        env["KK4S_ROOT"] = str(root)
        env["KK4S_INPUT_SCRIPT"] = str(script)
        result = subprocess.run([executable, str(game)], env=env, timeout=120, check=False,
                                capture_output=True, text=True)
        logs = sorted((root / "logs").glob("kk4s-*.log"))
        log = logs[-1].read_text(encoding="utf-8", errors="replace") if logs else ""
        if "EXTRANS_READY" not in log:
            problems.append("the test did not start")
        if "-> crossfade" in log:
            problems.append("a transition fell back to crossfade: " +
                            "; ".join(l for l in log.splitlines() if "-> crossfade" in l))
        for name, method, _, _ in METHODS:
            if f"EXTRANS_DONE {method}" not in log:
                problems.append(f"{method}: onTransitionCompleted never came")
            shot = root / "shots" / f"{name}.bmp"
            if not shot.exists():
                problems.append(f"{name}: no screenshot")
                continue
            verdict = check(name, game_pixels(shot))
            if verdict:
                problems.append(verdict)
        if result.returncode != 0:
            problems.append(f"kk4s exited with {result.returncode}")
    if problems:
        print("\n".join(problems), file=sys.stderr)
        print("\n".join(l for l in log.splitlines() if "EXTRANS" in l or "exception" in l.lower()
                        or "script: eval" in l)[-4000:], file=sys.stderr)
        return 1
    print(f"extrans transitions: PASS ({len(METHODS) - 2} methods + 2 crossfade controls)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
