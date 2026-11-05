#!/usr/bin/env python3
"""Soft mode for members of ignored plugins (compat/kk4s_softmode.cpp).

Usage: python tests/softmode/smoke.py path/to/kk4s

Boots tests/tjs/softmode/startup.tjs twice. With soft_mode=0 (the default)
every missing member throws. With soft_mode=1 in settings.ini, after an
ignored Plugins.link a missing member of a native object (System, a Layer
subclass instance) reads / calls as void and is logged once, while plain
script objects and unknown globals still throw, and nothing changes before a
plugin was ignored. No game assets are used.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ALWAYS = {
    "before.System.call": "threw",
    "Plain.call": "threw",
    "Plain.get": "threw",
    "global.call": "threw",
    "Layer.existing": "ok Integer",
}
SOFT = {
    "System.call": "ok void",
    "System.get": "ok void",
    "Layer.call": "ok void",
    "Layer.get": "ok void",
    "Layer.result": "ok String",
}


def run(executable: str, base: Path, soft: bool):
    root = base / ("soft" if soft else "strict")
    root.mkdir(parents=True)
    (root / "settings.ini").write_text(f"soft_mode={1 if soft else 0}\n", encoding="ascii")
    script = base / "input.txt"
    script.write_text("5000 quit\n", encoding="ascii")
    env = os.environ.copy()
    env["KK4S_ROOT"] = str(root)
    env["KK4S_INPUT_SCRIPT"] = str(script)
    game = Path(__file__).resolve().parents[1] / "tjs" / "softmode"
    result = subprocess.run([executable, str(game)], env=env, timeout=60, check=False,
                            capture_output=True, text=True)
    logs = sorted((root / "logs").glob("kk4s-*.log"))
    log = logs[-1].read_text(encoding="utf-8", errors="replace") if logs else ""
    got = dict(re.findall(r"SOFT (\S+) (ok \S+|threw)", log))
    return result.returncode, log, got


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())
    problems = []
    with tempfile.TemporaryDirectory(prefix="kk4s-softmode-") as temp:
        base = Path(temp)
        for soft in (False, True):
            mode = "soft_mode=1" if soft else "soft_mode=0"
            code, log, got = run(executable, base, soft)
            if "SOFT done" not in log:
                problems.append(f"{mode}: the script did not finish (exit {code})")
            want = dict(ALWAYS)
            want.update({k: (v if soft else "threw") for k, v in SOFT.items()})
            for k, v in want.items():
                if got.get(k) != v:
                    problems.append(f"{mode}: {k}: got {got.get(k)!r}, want {v!r}")
            soft_lines = re.findall(r"compat: soft mode: (\S+) does not exist", log)
            if soft:
                for m in ("System.kk4sNoSuchMember", "System.kk4sNoSuchProp", "Layer.kk4sNoSuchMethod",
                          "Layer.kk4sNoSuchProp"):
                    if soft_lines.count(m) != 1:
                        problems.append(f"{mode}: '{m}' logged {soft_lines.count(m)} times, want once")
                if "compat: soft mode on" not in log:
                    problems.append(f"{mode}: no 'soft mode on' line")
            elif soft_lines or "compat: soft mode on" in log:
                problems.append(f"{mode}: soft mode lines in the log: {soft_lines}")
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    print("soft mode (off by default, native objects only, after an ignored plugin): PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
