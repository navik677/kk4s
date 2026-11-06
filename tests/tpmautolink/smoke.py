#!/usr/bin/env python3
"""Auto-loaded .tpm plugins.

Usage: python tests/tpmautolink/smoke.py path/to/kk4s

krkr.exe loads every *.tpm next to the executable and in plugin/ at start-up,
even when the game never calls Plugins.link. kk4s must link the built-in
replacement of the DLL behind such a file. The game here links nothing:
csvParser.tpm must provide CSVParser, and a .tpm with no replacement must be
logged and skipped without becoming an ignored plugin link. No game assets are
used; the .tpm files are dummy bytes because kk4s never executes them.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())
    fixture = Path(__file__).resolve().parents[1] / "tjs" / "tpmautolink" / "startup.tjs"

    with tempfile.TemporaryDirectory(prefix="kk4s-tpm-auto-") as temp:
        base = Path(temp)
        game = base / "game"
        game.mkdir()
        shutil.copyfile(fixture, game / "startup.tjs")
        (game / "csvParser.tpm").write_bytes(b"dummy\n")
        (game / "kk4sUnknownPlugin.tpm").write_bytes(b"dummy\n")
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

    problems = []
    if "TPM_AUTOLINK PASS" not in log:
        problems.append("CSVParser was not registered from csvParser.tpm")
    if "auto-loaded plugin csvparser.dll (csvparser.tpm)" not in log:
        problems.append("the auto-load was not logged")
    if "kk4sunknownplugin.tpm: no built-in replacement, skipped" not in log:
        problems.append("an unknown .tpm was not reported as skipped")
    if result.returncode != 0:
        problems.append(f"exit {result.returncode}")
    if problems:
        print("\n".join(l for l in log.splitlines()
                        if "TPM_AUTOLINK" in l or "auto-loaded" in l or "replacement" in l
                        or "xception" in l) or log[-4000:], file=sys.stderr)
        for problem in problems:
            print(problem, file=sys.stderr)
        return 1
    print("auto-loaded .tpm plugins: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
