#!/usr/bin/env python3
"""KAG3 player interactions with the default pad: history, hiding, auto, skip.

Usage: python tests/kag3/interact_smoke.py path/to/kk4s

Uses the same vendored KAG3 template as tests/kag3/smoke.py with a long text
scenario, and plays it like a player on a gamepad: A (Enter) reads on, L (wheel
up) opens the history and scrolls it back, R (wheel down) at its end closes
it, B (right click) closes it too and then hides the message window, A shows it
again; auto mode is switched through kag.enterAutoMode(), and ZR (Ctrl held)
skips. The state is read back with `eval` lines. KAG3 opens its history on
wheel up / R / Shift+Up (not Page Up) and divides the wheel delta by 120, so
these checks pin both the pad mapping and the Windows wheel semantics.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import smoke  # noqa: E402  (tests/kag3/smoke.py: make_game)

PAGES = 16  # four history lines and one click each
SCENARIO = "*start|スタート\n[eval exp=\"dm('KAG3_IA start')\"]\n[nowait]\n[cm]\n" + "".join(
    f"{i + 1}ページ目の文章です。[r]履歴に残ります。[r]続きの行です。[r]最後の行です。[p]\n" for i in range(PAGES)
) + "[eval exp=\"dm('KAG3_IA done')\"]\n[s]\n"

HIST = "kag.historyLayer"
AT_END = f"({HIST}.dispStart >= {HIST}.dataLines - {HIST}.dispLines)"
STEPS = [
    # (pad input or None, ms until the eval, eval expression, wanted result)
    ("pad L", 700, f"'history=' + kag.historyShowing + ' at_end=' + {AT_END}", "history=1 at_end=1"),
    ("pad R", 700, "'history=' + kag.historyShowing", "history=0"),     # wheel down at the end closes it
    ("pad L", 700, "'history=' + kag.historyShowing", "history=1"),
    ("pad L", 700, f"'scrolled=' + !{AT_END}", "scrolled=1"),           # wheel up scrolls back
    ("pad B", 700, "'history=' + kag.historyShowing", "history=0"),     # right click closes it
    ("pad B", 700, "'hidden=' + kag.messageLayerHiding", "hidden=1"),   # right click hides the text
    ("pad A", 700, "'hidden=' + kag.messageLayerHiding", "hidden=0"),   # any click shows it again
    (None, 300, "kag.enterAutoMode(), 'auto=' + kag.autoMode", "auto=1"),
    (None, 3000, "'auto=' + kag.autoMode", "auto=1"),
    (None, 100, "kag.cancelAutoMode(), 'auto=' + kag.autoMode", "auto=0"),
]


def input_script() -> str:
    t = 2500
    lines = []

    def at(delta, text):
        nonlocal t
        t += delta
        lines.append(f"{t} {text}")

    # [nowait]: each page is complete at once, so KAG waits for a click (its
    # "stable" state, the only one in which history and hiding work). Read
    # enough pages for the history to scroll.
    for _ in range(10):
        at(500, "pad A")
    at(700, "eval 'pages=' + kag.historyLayer.dataLines")
    for pad, wait, expr, _ in STEPS:
        if pad:
            at(600, pad)
        at(wait, f"eval {expr}")
    at(300, "pad ZR 2500")                              # Ctrl held: skip
    at(2800, "eval 'line=' + kag.conductor.curLine")
    for _ in range(30):                                 # read the rest
        at(250, "pad A")
    at(500, "quit")
    return "\n".join(lines) + "\n"


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="kk4s-kag3-ia-") as temp:
        base = Path(temp)
        game, root = base / "game", base / "user-data"
        game.mkdir()
        smoke.make_game(game)
        (game / "scenario" / "first.ks").write_bytes(SCENARIO.replace("\n", "\r\n").encode("cp932"))
        script = base / "input.txt"
        script.write_text(input_script(), encoding="ascii")
        env = os.environ.copy()
        env["KK4S_ROOT"] = str(root)
        env["KK4S_INPUT_SCRIPT"] = str(script)
        result = subprocess.run([executable, str(game)], env=env, timeout=120, check=False,
                                capture_output=True, text=True)
        logs = sorted((root / "logs").glob("kk4s-*.log"))
        log = logs[-1].read_text(encoding="utf-8", errors="replace") if logs else ""

    evals = [e.strip() for e in re.findall(r"script: eval -> (.*)", log)]
    problems = []
    got = [e for e in evals if not e.startswith(("line=", "pages="))]
    want = [w for _, _, _, w in STEPS]
    if got[:len(want)] != want:
        problems.append(f"state sequence {got}, want {want}")
    if "KAG3_IA done" not in log:
        problems.append("the scenario did not reach its end")
    if result.returncode != 0:
        problems.append(f"kk4s exited with {result.returncode}")
    if problems:
        print("\n".join(problems), file=sys.stderr)
        tail = [l for l in log.splitlines() if not l.startswith("fonts:")][-50:]
        print("\n".join(tail), file=sys.stderr)
        return 1
    print("KAG3 interactions: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
