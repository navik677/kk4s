#!/usr/bin/env python3
"""Regression test: XP3 project dir and archive overlay precedence.

Usage: python tests/xp3/precedence_smoke.py path/to/kk4s

Fate/stay night's Russian patch depends on two rules of the storage layer, and
both are easy to break by "fixing" one game:

1. Project dir. A folder with ``startup.tjs`` is the project dir; a folder
   without it but with ``data.xp3`` resolves to ``<folder>/data.xp3>`` so that
   the game's own startup script is read from the archive.
2. Overlay order. ``Storages.addAutoPath`` appends; a name that already exists
   in the auto-path table is *replaced*, so the archive mounted **last** wins
   for a file that exists in several mounted archives. The current directory
   (the project dir) is still checked before the auto-path table.

The test never asserts a "nicer" behaviour than the engine has: case A pins the
archive-as-project-dir rules (including the fact that a *relative* sibling name
starting from ``data.xp3>`` normalizes into the archive), case B pins the
last-mounted-wins overlay rule and that re-ordering flips the result.

All archives and scripts are synthetic; no game data is used.
"""
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

MAGIC = b"XP3\r\n \n\x1a\x8bg\x01"
DATA_OFFSET = len(MAGIC) + 8  # magic + u64 index offset


def chunk(tag: bytes, body: bytes) -> bytes:
    return tag + struct.pack("<Q", len(body)) + body


def build_archive(files: list[tuple[str, bytes]]) -> bytes:
    """Build an uncompressed XP3v1 archive from (name, content) pairs."""
    offset = DATA_OFFSET
    entries = b""
    data = b""
    for name, body in files:
        encoded = name.encode("utf-16le")
        entries += chunk(
            b"File",
            chunk(b"info", struct.pack("<IQQH", 0, len(body), len(body),
                                       len(encoded) // 2) + encoded)
            + chunk(b"segm", struct.pack("<IQQQ", 0, offset, len(body), len(body)))
            + chunk(b"adlr", struct.pack("<I", 0)),
        )
        offset += len(body)
        data += body
    header = MAGIC + struct.pack("<Q", DATA_OFFSET + len(data))
    assert len(header) == DATA_OFFSET
    index_record = b"\x00" + struct.pack("<Q", len(entries)) + entries
    return header + data + index_record


def scenario_line(origin: str) -> bytes:
    return ('Debug.message("PREC scenario=%s");\n' % origin).encode("ascii")


# Case A: no startup.tjs on disk -> project dir is <game>/data.xp3>.
CASE_A_STARTUP = rb"""
var ok = true;
function check(label, cond, actual) {
    if (cond) {
        Debug.message("PREC PASS " + label);
    } else {
        ok = false;
        Debug.message("PREC FAIL " + label + " actual=" + actual);
    }
}

var placed = Storages.getPlacedPath("scenario.ks");
check("project-dir-is-archive", placed.indexOf("data.xp3>") >= 0, placed);

// A relative sibling name is resolved against the current directory, which is
// the archive itself. This is the engine's actual normalization; pin it so a
// change to the rule is a deliberate decision, not an accident.
Debug.message("PREC info relative-overlay=" + Storages.getFullPath("patch.xp3>"));
Storages.addAutoPath("patch.xp3>");
check("cwd-wins-over-autopath",
      Storages.getPlacedPath("scenario.ks").indexOf("data.xp3>") >= 0,
      Storages.getPlacedPath("scenario.ks"));

// Fate/stay night mounts its overlays by absolute path
// (System.exePath + "rupatch.xp3>"), not relative to the archive. That works
// for files only the overlay has, but the project dir still comes first for a
// name the archive itself contains.
Storages.addAutoPath(System.exePath + "patch.xp3>");
check("absolute-overlay-mounted",
      Storages.getPlacedPath("only_patch.txt").indexOf("patch.xp3>") >= 0,
      Storages.getPlacedPath("only_patch.txt"));
check("cwd-first-over-absolute-overlay",
      Storages.getPlacedPath("scenario.ks").indexOf("data.xp3>") >= 0,
      Storages.getPlacedPath("scenario.ks"));

Scripts.execStorage("scenario.ks");
if (ok) Debug.message("PRECEDENCE_CASE_A_PASS");
else throw new Exception("case A failed");
"""

# Case B: startup.tjs on disk -> project dir is the folder; archives are mounted
# by the game. The last mounted archive wins for a shared name.
CASE_B_STARTUP = rb"""
var ok = true;
function check(label, cond, actual) {
    if (cond) {
        Debug.message("PREC PASS " + label);
    } else {
        ok = false;
        Debug.message("PREC FAIL " + label + " actual=" + actual);
    }
}
function placedIs(name) {
    return Storages.getPlacedPath("scenario.ks").indexOf(name) >= 0;
}

Storages.addAutoPath("data.xp3>");
check("base-mounted", placedIs("data.xp3>"), Storages.getPlacedPath("scenario.ks"));
check("base-only-file", Storages.isExistentStorage("only_base.txt"),
      Storages.getPlacedPath("only_base.txt"));

Storages.addAutoPath("patch.xp3>");
check("last-mounted-wins", placedIs("patch.xp3>"), Storages.getPlacedPath("scenario.ks"));
Scripts.execStorage("scenario.ks");

// Re-mounting the base archive after the patch moves it to the end of the
// auto-path list, so the base wins again.
Storages.removeAutoPath("data.xp3>");
Storages.addAutoPath("data.xp3>");
check("remount-flips-order", placedIs("data.xp3>"), Storages.getPlacedPath("scenario.ks"));
Scripts.execStorage("scenario.ks");
check("overlay-does-not-hide-base", Storages.isExistentStorage("only_base.txt"),
      Storages.getPlacedPath("only_base.txt"));

// And the patch can be put back on top.
Storages.removeAutoPath("patch.xp3>");
Storages.addAutoPath("patch.xp3>");
check("patch-back-on-top", placedIs("patch.xp3>"), Storages.getPlacedPath("scenario.ks"));
Scripts.execStorage("scenario.ks");

if (ok) Debug.message("PRECEDENCE_CASE_B_PASS");
else throw new Exception("case B failed");
"""


def run_case(executable: str, case: str, startup: bytes, startup_on_disk: bool,
             expected: list[str], forbidden: list[str]) -> list[str]:
    """Run one fixture game and return the problems found (empty = pass)."""
    with tempfile.TemporaryDirectory(prefix="kk4s-xp3-precedence-") as temp:
        temp_path = Path(temp)
        game = temp_path / "game"
        root = temp_path / "data"
        game.mkdir()
        root.mkdir()

        base_files = [("scenario.ks", scenario_line("base")),
                      ("only_base.txt", b"base only\n")]
        if startup_on_disk:
            (game / "startup.tjs").write_bytes(startup)
        else:
            base_files.insert(0, ("startup.tjs", startup))
        (game / "data.xp3").write_bytes(build_archive(base_files))
        (game / "patch.xp3").write_bytes(build_archive(
            [("scenario.ks", scenario_line("patch")),
             ("only_patch.txt", b"patch only\n")]))

        input_script = temp_path / "input.txt"
        input_script.write_text("4000 quit\n", encoding="ascii")

        env = os.environ.copy()
        env["KK4S_ROOT"] = str(root)
        env["KK4S_INPUT_SCRIPT"] = str(input_script)
        env.pop("KK4S_DUMP_BYTECODE", None)
        env.pop("KK4S_KAG_TRACE", None)
        env.pop("KK4S_NO_SHIM", None)
        try:
            result = subprocess.run([executable, str(game)], env=env, timeout=60,
                                    check=False, capture_output=True, text=True)
        except subprocess.TimeoutExpired:
            return ["%s: kk4s did not exit within 60 s" % case]

        logs = sorted((root / "logs").glob("kk4s-*.log"))
        if not logs:
            detail = (result.stderr or result.stdout or "").strip()[-800:]
            return ["%s: kk4s produced no log (exit %d) %s"
                    % (case, result.returncode, detail)]
        log_text = logs[-1].read_text(encoding="utf-8", errors="replace")

        problems = []
        if case == "case-a" and "project dir" in log_text:
            if ("game/data.xp3>" not in log_text.replace("\\", "/")
                    and "game\\data.xp3>" not in log_text):
                problems.append("%s: project dir is not the archive (log below)\n%s"
                                % (case, log_text[-3000:]))
        for marker in expected:
            if marker not in log_text:
                problems.append("%s: missing %r\n%s" % (case, marker, log_text[-4000:]))
        for marker in forbidden:
            if marker in log_text:
                problems.append("%s: unexpected %r\n%s" % (case, marker, log_text[-4000:]))
        if "PREC FAIL" in log_text:
            problems.append("%s: assertion failed\n%s" % (case, log_text[-4000:]))
        if result.returncode != 0:
            problems.append("%s: kk4s exited with %d" % (case, result.returncode))
        return problems


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())

    problems = []
    problems += run_case(
        executable, "case-a", CASE_A_STARTUP, startup_on_disk=False,
        expected=["PREC PASS project-dir-is-archive",
                  "PREC PASS cwd-wins-over-autopath",
                  "PREC PASS absolute-overlay-mounted",
                  "PREC PASS cwd-first-over-absolute-overlay",
                  "PREC scenario=base",
                  "PRECEDENCE_CASE_A_PASS"],
        forbidden=["PREC scenario=patch"])
    problems += run_case(
        executable, "case-b", CASE_B_STARTUP, startup_on_disk=True,
        expected=["PREC PASS base-mounted",
                  "PREC PASS base-only-file",
                  "PREC PASS last-mounted-wins",
                  "PREC scenario=patch",
                  "PREC PASS remount-flips-order",
                  "PREC scenario=base",
                  "PREC PASS overlay-does-not-hide-base",
                  "PREC PASS patch-back-on-top",
                  "PRECEDENCE_CASE_B_PASS"],
        forbidden=["PREC FAIL"])

    if problems:
        for problem in problems:
            print(problem, file=sys.stderr)
        return 1
    print("XP3 project dir / overlay precedence: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
