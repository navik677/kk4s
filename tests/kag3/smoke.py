#!/usr/bin/env python3
"""Boot the original Kirikiri 2 KAG3 system scripts and play a scenario.

Usage: python tests/kag3/smoke.py path/to/kk4s [--keep DIR]

tests/kag3/template/ is the unmodified KAG3 template from the Kirikiri 2 source
(Shift_JIS, CRLF; dual Kirikiri/GPL license, see README.md there). This script
builds a game around it: the template's system/ and startup.tjs, a scenario
(Shift_JIS, like most games) and generated images, rule and sounds. The pad is
pressed through KK4S_INPUT_SCRIPT like a player would (A = Enter, DOWN focuses a
link). Each step of the scenario logs "KAG3_STEP <name>"; the test passes when
all steps appear in order and "KAG3 PASS" is logged, and reports the last step
reached otherwise. Almost every KAG3 game is built on these system scripts, so a
failure here is a failure for the whole class.
"""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
import math

TEMPLATE = Path(__file__).resolve().parent / "template"

STEPS = [
    "start", "text", "trans_crossfade", "trans_universal", "trans_scroll", "move", "quake",
    "font", "macro kk4s", "if", "call", "return", "se", "bgm", "emb", "save 1",
    "link choice1", "yesno",
]

SCENARIO = """*start|スタート
[eval exp="dm('KAG3_STEP start')"]
[image storage="bg_red" layer=base page=fore]
[cm]
吉里吉里のテストです。[l][r]
二行目です。[p]
[eval exp="dm('KAG3_STEP text')"]
[backlay]
[image storage="bg_blue" layer=base page=back]
[image storage="chara" layer=0 page=back visible=true left=100 top=40]
[trans method=crossfade time=200]
[wt]
[eval exp="dm('KAG3_STEP trans_crossfade')"]
[backlay]
[image storage="bg_red" layer=base page=back]
[trans method=universal rule="rule_grad" vague=64 time=200]
[wt]
[eval exp="dm('KAG3_STEP trans_universal')"]
[backlay]
[image storage="bg_blue" layer=base page=back]
[trans method=scroll from=left stay=stayfore time=200]
[wt]
[eval exp="dm('KAG3_STEP trans_scroll')"]
[move layer=0 page=fore path="(150,60,255)(200,80,128)" time=200]
[wm]
[eval exp="dm('KAG3_STEP move')"]
[quake time=200 hmax=5 vmax=5]
[wq]
[eval exp="dm('KAG3_STEP quake')"]
[cm]
[font size=28 color=0xff8080]色付きの文字[resetfont]と[ruby text="かんじ"]漢字[l][r]
[style align=center]中央寄せ[resetstyle][p]
[eval exp="dm('KAG3_STEP font')"]
[macro name=kk4s_macro][eval exp="dm('KAG3_STEP macro ' + mp.who)"][endmacro]
[kk4s_macro who=kk4s]
[eval exp="f.counter = 1"]
[if exp="f.counter == 1"][eval exp="dm('KAG3_STEP if')"][else][eval exp="dm('KAG3_FAIL if')"][endif]
[call storage="sub.ks" target=*sub]
[eval exp="dm('KAG3_STEP return')"]
[playse storage="se_beep" buf=0]
[ws buf=0 canskip=false]
[eval exp="dm('KAG3_STEP se')"]
[playbgm storage="bgm_loop"]
[wait time=200]
[stopbgm]
[eval exp="dm('KAG3_STEP bgm')"]
[cm]
[emb exp="'埋め込み'"]の文字[l][er]
[eval exp="dm('KAG3_STEP emb')"]
[eval exp="kag.saveBookMark(0, false)"]
[eval exp="dm('KAG3_STEP save ' + (Storages.isExistentStorage(kag.getBookMarkFileNameAtNum(0)) ? 1 : 0))"]
[cm]
選んでください。[r]
[link target=*choice1]一つ目[endlink][r]
[link target=*choice2]二つ目[endlink][r]
[s]
*choice1
[eval exp="dm('KAG3_STEP link choice1')"]
[jump target=*after_choice]
*choice2
[eval exp="dm('KAG3_STEP link choice2')"]
*after_choice
[cm]
[eval exp="global.kk4s_answer = askYesNo('続けますか？')"]
[eval exp="dm('KAG3_STEP yesno')"]
[eval exp="dm('KAG3 PASS')"]
[s]
"""

SUB = """*sub
[eval exp="dm('KAG3_STEP call')"]
[return]
"""


def png(width: int, height: int, pixel) -> bytes:
    """RGBA PNG; pixel(x, y) -> (r, g, b, a)."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(pixel(x, y))

    def chunk(tag: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))


def wav(seconds: float, freq: float) -> bytes:
    rate = 22050
    n = int(rate * seconds)
    samples = b"".join(struct.pack("<h", int(8000 * math.sin(2 * math.pi * freq * i / rate))) for i in range(n))
    fmt = struct.pack("<HHIIHH", 1, 1, rate, rate * 2, 2, 16)
    return (b"RIFF" + struct.pack("<I", 36 + len(samples)) + b"WAVE" + b"fmt " + struct.pack("<I", 16) + fmt
            + b"data" + struct.pack("<I", len(samples)) + samples)


def make_game(game: Path) -> None:
    shutil.copytree(TEMPLATE / "system", game / "system")
    shutil.move(game / "system" / "Config.~new", game / "system" / "Config.tjs")  # as the template asks
    shutil.copy(TEMPLATE / "startup.tjs", game / "startup.tjs")
    for d in ("scenario", "bgimage", "fgimage", "rule", "sound", "bgm", "image", "others", "video"):
        (game / d).mkdir(exist_ok=True)
    sjis = lambda text: text.replace("\n", "\r\n").encode("cp932")
    (game / "scenario" / "first.ks").write_bytes(sjis(SCENARIO))
    (game / "scenario" / "sub.ks").write_bytes(sjis(SUB))
    (game / "bgimage" / "bg_red.png").write_bytes(png(640, 480, lambda x, y: (200, 40, 40, 255)))
    (game / "bgimage" / "bg_blue.png").write_bytes(png(640, 480, lambda x, y: (40, 40, 200, 255)))
    (game / "fgimage" / "chara.png").write_bytes(
        png(160, 240, lambda x, y: (240, 220, 120, 255 if 20 < x < 140 else 0)))
    (game / "rule" / "rule_grad.png").write_bytes(png(640, 480, lambda x, y: (x * 255 // 639,) * 3 + (255,)))
    (game / "sound" / "se_beep.wav").write_bytes(wav(0.3, 880))
    (game / "bgm" / "bgm_loop.wav").write_bytes(wav(1.0, 440))


def input_script(seconds: int) -> str:
    lines = ["2500 eval kag.conductor.curStorage + ':' + kag.conductor.curLine"]
    t = 2500
    n = 0
    while t < seconds * 1000:
        t += 350
        n += 1
        # mostly Enter; every fourth press DOWN, which focuses a link once [link] waits at [s]
        lines.append(f"{t} pad {'DOWN' if n % 4 == 0 else 'A'}")
        if n % 20 == 0:
            lines.append(f"{t + 10} eval kag.conductor.curStorage + ':' + kag.conductor.curLine")
    lines.append(f"{t + 200} quit")
    return "\n".join(lines) + "\n"


def main() -> int:
    args = sys.argv[1:]
    keep = None
    if "--keep" in args:
        keep = Path(args[args.index("--keep") + 1])
        args = [a for a in args if a not in ("--keep", str(keep))]
    if len(args) != 1:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(args[0]).resolve())
    with tempfile.TemporaryDirectory(prefix="kk4s-kag3-") as temp:
        base = keep if keep else Path(temp)
        base.mkdir(parents=True, exist_ok=True)
        game, root = base / "game", base / "user-data"
        if game.exists():
            shutil.rmtree(game)
        game.mkdir()
        make_game(game)
        before = {p.relative_to(game): p.stat().st_size for p in game.rglob("*") if p.is_file()}
        script = base / "input.txt"
        script.write_text(input_script(28), encoding="ascii")
        env = os.environ.copy()
        env["KK4S_ROOT"] = str(root)
        env["KK4S_INPUT_SCRIPT"] = str(script)
        result = subprocess.run([executable, str(game)], env=env, timeout=120, check=False,
                                capture_output=True, text=True)
        logs = sorted((root / "logs").glob("kk4s-*.log"))
        log = logs[-1].read_text(encoding="utf-8", errors="replace") if logs else ""
        # only the lines dm() wrote ("HH:MM:SS KAG3_STEP x"), not the scenario trace that quotes them
        marks = [l[9:] for l in log.splitlines() if len(l) > 9 and l[2] == ":" and l[5] == ":" and l[8] == " "]
        reached = [m[len("KAG3_STEP "):].strip() for m in marks if m.startswith("KAG3_STEP ")]
        failed = [m for m in marks if m.startswith("KAG3_FAIL")]
        after = {p.relative_to(game): p.stat().st_size for p in game.rglob("*") if p.is_file()}

    problems = []
    expected = list(STEPS)
    got = [s for s in reached if s in expected]
    if got != expected:
        last = got[-1] if got else "(none)"
        missing = [s for s in expected if s not in got]
        problems.append(f"steps reached: {got}\nlast step: {last}; missing: {missing}")
    if "KAG3 PASS" not in marks:
        problems.append("KAG3 PASS not logged")
    if failed:
        problems.append(f"failure markers: {failed}")
    if before != after:
        problems.append(f"the game folder changed: {sorted(set(after) ^ set(before))}")
    if result.returncode != 0:
        problems.append(f"kk4s exited with {result.returncode}")
    if problems:
        print("\n".join(problems), file=sys.stderr)
        tail = [l for l in log.splitlines() if not l.startswith("fonts:")][-60:]
        print("\n".join(tail), file=sys.stderr)
        return 1
    print("KAG3 template scenario: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
