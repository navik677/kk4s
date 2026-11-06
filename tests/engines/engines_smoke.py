#!/usr/bin/env python3
"""Games of other engines (docs/engines.md): detection and the PC hand-off.

Usage: python tests/engines/engines_smoke.py path/to/kk4s

Cases (all synthetic, no game content):
  list      kk4s --list-games names the engine of each folder: NScripter
            (nscript.dat; 0.txt + arc.nsa; inside a wrapper folder), RealLive
            (Gameexe.ini + Seen.txt), Ren'Py (game/*.rpyc); Kirikiri wins over
            a stray game/*.rpy; a lone 0.txt or a game/ without scripts is no game
  missing   without engines/<id>/ the line says "(engine not installed)"
  builtin   kk4s built with ONScripter: kk4s <NScripter folder> runs a tiny
            script in it; the global save lands in saves/<game>/, not in the game
  launch    otherwise: kk4s <NScripter folder> runs engines/onscripter/onscripter(.exe)
            with --root <folder> and waits for it (needs a C compiler: cc / gcc,
            else skipped)
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

FAKE_ENGINE = r"""
#include <stdio.h>
int main(int argc, char **argv) {
    FILE *f = fopen(ARGS_FILE, "w");
    if (!f) return 3;
    for (int i = 1; i < argc; i++) fprintf(f, "%s\n", argv[i]);
    fclose(f);
    return 7;
}
"""


def run(executable: str, root: Path, args: list[str]) -> tuple[int, str, str]:
    env = os.environ.copy()
    env["KK4S_ROOT"] = str(root)
    env.setdefault("SDL_VIDEODRIVER", "dummy")
    env.setdefault("SDL_AUDIODRIVER", "dummy")
    result = subprocess.run([executable, *args], env=env, timeout=60, check=False,
                            capture_output=True, text=True)
    logs = sorted((root / "logs").glob("kk4s-*.log"))
    log = logs[-1].read_text(encoding="utf-8", errors="replace") if logs else ""
    return result.returncode, result.stdout, log


def touch(path: Path, data: bytes = b"x") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def make_games(games: Path) -> dict[str, str]:
    """Create the folders; returns folder -> expected engine name ("" = not a game)."""
    touch(games / "ns_dat" / "NSCRIPT.DAT")
    touch(games / "ns_txt" / "0.txt")
    touch(games / "ns_txt" / "arc.nsa")
    touch(games / "ns_wrap" / "Inner Game" / "nscr_sec.dat")
    touch(games / "rl" / "Gameexe.ini")
    touch(games / "rl" / "SEEN.TXT")
    touch(games / "rp" / "game" / "script.rpyc")
    touch(games / "kiri" / "data.xp3")
    touch(games / "kiri" / "game" / "notes.rpy")
    touch(games / "readme_only" / "0.txt")
    touch(games / "rp_no_scripts" / "game" / "bg.png")
    return {"ns_dat": "NScripter", "ns_txt": "NScripter", "ns_wrap": "NScripter", "rl": "RealLive",
            "rp": "Ren'Py", "kiri": "Kirikiri", "readme_only": "", "rp_no_scripts": ""}


def find_cc() -> str:
    for cc in ("cc", "gcc"):
        if shutil.which(cc):
            return cc
    return ""


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())
    failures = []
    with tempfile.TemporaryDirectory(prefix="kk4s-engines-") as temp:
        root = Path(temp) / "root"
        expected = make_games(root / "games")

        code, stdout, _ = run(executable, root, ["--list-games"])
        listed = dict(line.split("\t", 1) for line in stdout.splitlines() if "\t" in line)
        for folder, engine in expected.items():
            line = listed.get(folder)
            if not engine:
                if line is not None:
                    failures.append(f"list: {folder} is no game, listed as {line!r}")
            elif line is None:
                failures.append(f"list: {folder} ({engine}) not listed")
            elif engine == "Kirikiri":
                if not line.endswith("data.xp3>"):
                    failures.append(f"list: {folder} should stay Kirikiri, got {line!r}")
            elif not line.startswith(engine + ": "):
                failures.append(f"list: {folder} should be {engine}, got {line!r}")
            elif "(engine not installed)" not in line and folder != "rp" and not line.endswith(": built in"):
                failures.append(f"missing: {folder}: {line!r}")
        if code != 0:
            failures.append(f"--list-games exited with {code}")

        builtin = listed.get("ns_dat", "").endswith(": built in")
        cc = find_cc()
        if builtin:
            # ONScripter inside kk4s: run a tiny script; its global save goes to saves/<game>/
            game = root / "games" / "ns_run"
            game.mkdir()
            (game / "0.txt").write_bytes(b"*define\r\nglobalon\r\ngame\r\n*start\r\nmov %200,42\r\nend\r\n")
            (game / "arc.nsa").write_bytes(b"\x00\x00\x00\x00\x00\x06")  # empty NSA archive
            code, _, log = run(executable, root, [str(game)])
            save = root / "saves" / "ns_run" / "gloval.sav"
            if "onscripter: game ended" not in log:
                failures.append("builtin: ONScripter did not run the script\n" + log[-1500:])
            if not save.exists() or save.read_bytes()[:4] != (42).to_bytes(4, "little"):
                failures.append(f"builtin: no global save with %200 = 42 in {save}")
            if (game / "gloval.sav").exists():
                failures.append("builtin: the save went to the game folder")
            if code != 0:
                failures.append(f"builtin: kk4s exited with {code}")
        elif not cc:
            print("launch: no C compiler, skipped")
        else:
            args_file = Path(temp) / "engine-args.txt"
            exe_name = "onscripter.exe" if os.name == "nt" else "onscripter"
            engine = root / "engines" / "onscripter" / exe_name
            engine.parent.mkdir(parents=True)
            src = Path(temp) / "fake_engine.c"
            src.write_text(FAKE_ENGINE, encoding="ascii")
            define = '-DARGS_FILE="' + str(args_file).replace("\\", "/") + '"'
            build = subprocess.run([cc, define, "-o", str(engine), str(src)], capture_output=True, text=True)
            if build.returncode != 0:
                failures.append("launch: cannot build the fake engine: " + build.stderr[-500:])
            else:
                game = root / "games" / "ns_wrap"
                code, _, log = run(executable, root, [str(game)])
                got = args_file.read_text(encoding="utf-8").splitlines() if args_file.exists() else []
                if len(got) < 2 or got[0] != "--root" or not got[1].replace("\\", "/").endswith("ns_wrap/Inner Game"):
                    failures.append(f"launch: engine args {got}, want --root .../ns_wrap/Inner Game\n" + log[-1500:])
                if "exited with code 7" not in log:
                    failures.append("launch: kk4s did not wait for the engine\n" + log[-1500:])
                if code != 0:
                    failures.append(f"launch: kk4s exited with {code}")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print("other engines: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
