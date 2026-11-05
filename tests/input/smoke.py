#!/usr/bin/env python3
"""Mouse, wheel, touch and gamepad events follow Kirikiri's Windows rules.

Usage: python tests/input/smoke.py path/to/kk4s

Boots tests/tjs/mouse/startup.tjs (a 640x480 window, a layer on its left half)
and drives the PC mouse path, the touch path and the pad through
KK4S_INPUT_SCRIPT. Checked against krkrz's TVPWindow.cpp / WindowForm.cpp and
krkr2's DInputMgn.cpp:
- onClick only for the left button, at the press point, before onMouseUp; not
  for the second press of a double click (that one is onDoubleClick)
- the shift of mouse events carries the buttons held (ssLeft / ssRight /
  ssMiddle); System.getKeyState(VK_LBUTTON / VK_RBUTTON) follows them
- a wheel notch is 120 (WHEEL_DELTA)
- Layer.cursorX/Y are game coordinates; setCursorPos makes a mouse move there
- pad B is a right click (no onClick), L / R are wheel notches that repeat
  while held, A and the D-pad repeat as keys with ssRepeat after 500 ms
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

# 640x480 game on the 1280x720 output with black bars: game x = (wx - 160) / 1.5
LEFT = "0.2421875 0.5"   # game (100, 240), on the left-half layer
CENTRE = "0.5 0.5"       # game (320, 240), primary layer only

LCLICK = "W.down:L+L L.down:L+L W.click L.click W.up:L L.up:L"

STEPS = [
    # (time, input lines, eval time, eval expression, check(result) -> True or a problem)
    (1500, [], 1540, "flush()", None),
    (1600, [f"mouse {LEFT} left"], 1940, "flush()", lambda r: r == LCLICK or f"left click: {r!r}, want {LCLICK!r}"),
    (2000, [f"mouse {LEFT} right"], 2340, "flush()",
     lambda r: r == "W.down:R+R L.down:R+R W.up:R L.up:R" or f"right click: {r!r} (no onClick, ssRight while down)"),
    (2400, [f"mouse {LEFT} middle"], 2740, "flush()",
     lambda r: r == "W.down:M+M L.down:M+M W.up:M L.up:M" or f"middle click: {r!r}"),
    (2800, [f"mouse {LEFT} left 60 1", f"+150 mouse {LEFT} left 60 2"], 3240, "flush()",
     lambda r: r == LCLICK + " W.dbl L.dbl W.down:L+L L.down:L+L W.up:L L.up:L"
     or f"double click: {r!r} (onDoubleClick, no onClick for the second press)"),
    (3300, ["wheel 1", "+50 wheel -2"], 3540, "flush()",
     lambda r: r == "W.wheel120 W.wheel-240" or f"wheel: {r!r}, want 120 per notch"),
    # press on the layer, drag to the centre, let go there
    (3600, [f"mouse {LEFT} left 700", f"+250 move {CENTRE}"], 4100,
     "'held=' + System.getKeyState(VK_LBUTTON) + ' at=' + mw.primaryLayer.cursorX + ',' + mw.primaryLayer.cursorY",
     lambda r: r == "held=1 at=320,240" or f"while dragging: {r!r}, want held=1 at=320,240"),
    (4300, [], 4500, "'held=' + System.getKeyState(VK_LBUTTON) + ' at=' + mw.primaryLayer.cursorX + ' ' + flush()",
     lambda r: r == "held=0 at=320 W.down:L+L L.down:L+L W.drag+L L.drag+L W.click L.click W.up:L L.up:L"
     or f"drag: {r!r} (ssLeft in onMouseMove, click at the press point, capture kept)"),
    (4700, ["pad B 300"], 4850, "'rheld=' + System.getKeyState(VK_RBUTTON)",
     lambda r: r == "rheld=1" or f"pad B held: {r!r}"),
    (5000, [], 5100, "flush()",
     lambda r: r == "W.down:R+R P.down:R+R W.up:R P.up:R" or f"pad B: {r!r}, want a right click without onClick"),
    (5300, [], 5300, "mw.primaryLayer.setCursorPos(50, 60), 'at=' + mw.primaryLayer.cursorX + ',' + mw.primaryLayer.cursorY",
     lambda r: r == "at=50,60" or f"setCursorPos: {r!r}"),
    (5400, [], 5500, "mv[mv.count - 1]", lambda r: r == "50,60" or f"setCursorPos made no mouse move to 50,60: {r!r}"),
    (5600, [f"tap {LEFT}"], 5940, "flush()", lambda r: r == LCLICK or f"tap: {r!r}, want {LCLICK!r}"),
    (6000, ["pad L 100", "+300 pad R 100"], 6740, "flush()",
     lambda r: r == "W.wheel120 W.wheel-120" or f"pad L / R: {r!r}, want one wheel notch each"),
    (6800, ["pad L 870"], 7940, "flush()", None),  # checked in main(): repeats
    (8000, ["pad A 800"], 9140, "flush()", None),
    (9200, ["pad DOWN 700"], 10340, "flush()", None),
]


def input_script() -> str:
    lines = []
    for at, inputs, eval_at, expr, _ in STEPS:
        t = at
        for line in inputs:
            if line.startswith("+"):
                delta, rest = line[1:].split(" ", 1)
                t += int(delta)
                lines.append((t, rest))
            else:
                lines.append((at, line))
        lines.append((eval_at, f"eval {expr}"))
    lines.append((STEPS[-1][2] + 300, "quit"))
    lines.sort(key=lambda l: l[0])  # stable: an eval at the same time as an input runs after it
    return "".join(f"{t} {l}\n" for t, l in lines)


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())
    game = Path(__file__).resolve().parents[1] / "tjs" / "mouse"
    with tempfile.TemporaryDirectory(prefix="kk4s-input-") as temp:
        base = Path(temp)
        root = base / "user-data"
        script = base / "input.txt"
        script.write_text(input_script(), encoding="ascii")
        env = os.environ.copy()
        env["KK4S_ROOT"] = str(root)
        env["KK4S_INPUT_SCRIPT"] = str(script)
        result = subprocess.run([executable, str(game)], env=env, timeout=90, check=False,
                                capture_output=True, text=True)
        logs = sorted((root / "logs").glob("kk4s-*.log"))
        log = logs[-1].read_text(encoding="utf-8", errors="replace") if logs else ""

    evals = re.findall(r"script: eval -> (.*)", log)
    problems = []
    if "MOUSE_READY" not in log:
        problems.append("the test window did not start")
    if len(evals) != len(STEPS):
        problems.append(f"{len(evals)} eval result(s) for {len(STEPS)} steps")
    for (at, _, _, _, check), got in zip(STEPS, evals):
        if check:
            verdict = check(got.strip())
            if verdict is not True:
                problems.append(f"{at} ms: {verdict}")
    if len(evals) == len(STEPS):
        held_l, held_a, held_down = (evals[-3].split(), evals[-2].split(), evals[-1].split())
        # L held 870 ms: a notch on press, then one every 100 ms after 500 ms
        if not (held_l and set(held_l) == {"W.wheel120"} and 3 <= len(held_l) <= 6):
            problems.append(f"pad L held: {held_l}, want 4 or so wheel notches")
        # A held 800 ms: key down, repeats every 30 ms after 500 ms with ssRepeat, key up
        if not (held_a[:1] == ["W.key13"] and held_a[-1:] == ["W.keyup13"]
                and 5 <= held_a.count("W.key13r") <= 12 and len(held_a) == held_a.count("W.key13r") + 2):
            problems.append(f"pad A held: {held_a}, want key13, ~10 x key13r, keyup13")
        if not (held_down[:1] == ["W.key40"] and held_down[-1:] == ["W.keyup40"]
                and 3 <= held_down.count("W.key40r") <= 9):
            problems.append(f"pad DOWN held: {held_down}, want key40, ~6 x key40r, keyup40")
    if result.returncode != 0:
        problems.append(f"kk4s exited with {result.returncode}")
    if problems:
        print("\n".join(problems), file=sys.stderr)
        print("\n".join(l for l in log.splitlines() if "script:" in l or "exception" in l.lower())[-6000:],
              file=sys.stderr)
        return 1
    print("Kirikiri input semantics (mouse, wheel, touch, pad repeat): PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
