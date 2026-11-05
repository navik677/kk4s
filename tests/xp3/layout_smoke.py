#!/usr/bin/env python3
"""Game-folder layouts Kirikiri accepts, besides startup.tjs / data.xp3.

Usage: python tests/xp3/layout_smoke.py path/to/kk4s

Cases (all synthetic):
  data_dir     game/data/startup.tjs                  -> project dir game/data/
  self_exe     game/Game.exe with an XP3 appended     -> project dir game/Game.exe>
  data_exe     game/data.exe (XP3) + game/tool.exe   -> project dir game/data.exe>
  deep_exe     the XP3 sits far past the PE image    -> found by the full scan
  list         kk4s --list-games lists exactly the folders above (not a folder
               with only a readme, nor one whose exe has no archive)
The engine must run the startup.tjs from the chosen place.
"""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

MAGIC = b"XP3\r\n \n\x1a\x8bg\x01"
DATA_OFFSET = len(MAGIC) + 8


def chunk(tag: bytes, body: bytes) -> bytes:
    return tag + struct.pack("<Q", len(body)) + body


def build_archive(files: list[tuple[str, bytes]]) -> bytes:
    """Uncompressed XP3v1; offsets are relative to the archive start."""
    offset, entries, data = DATA_OFFSET, b"", b""
    for name, body in files:
        encoded = name.encode("utf-16le")
        entries += chunk(b"File",
                         chunk(b"info", struct.pack("<IQQH", 0, len(body), len(body), len(encoded) // 2) + encoded)
                         + chunk(b"segm", struct.pack("<IQQQ", 0, offset, len(body), len(body)))
                         + chunk(b"adlr", struct.pack("<I", 0)))
        offset += len(body)
        data += body
    header = MAGIC + struct.pack("<Q", DATA_OFFSET + len(data))
    return header + data + b"\x00" + struct.pack("<Q", len(entries)) + entries


def build_exe(appended: bytes, gap: int = 0) -> bytes:
    """A minimal PE32 image with one 0x200-byte section, then `appended`
    (after `gap` bytes of padding) as the overlay."""
    dos = bytearray(0x40)
    dos[0:2] = b"MZ"
    struct.pack_into("<I", dos, 0x3C, 0x40)
    coff = b"PE\0\0" + struct.pack("<HHIIIHH", 0x14C, 1, 0, 0, 0, 0xE0, 0x0102)
    optional = bytes(0xE0)
    section = b".text\0\0\0" + struct.pack("<IIIIIIHHI", 0x200, 0x1000, 0x200, 0x200, 0, 0, 0, 0, 0x60000020)
    head = bytes(dos) + coff + optional + section
    head += bytes(0x200 - len(head))
    body = b"\xcc" * 0x200
    return head + body + bytes(gap) + appended


def startup(case: str) -> bytes:
    return f'Debug.message("LAYOUT_PASS {case}");\n'.encode("ascii")


def run(executable: str, root: Path, args: list[str], env_extra=None) -> tuple[int, str, str]:
    env = os.environ.copy()
    env["KK4S_ROOT"] = str(root)
    script = root.parent / f"input-{root.name}.txt"
    script.write_text("1500 quit\n", encoding="ascii")
    env["KK4S_INPUT_SCRIPT"] = str(script)
    if env_extra:
        env.update(env_extra)
    result = subprocess.run([executable, *args], env=env, timeout=60, check=False,
                            capture_output=True, text=True)
    logs = sorted((root / "logs").glob("kk4s-*.log"))
    log = logs[-1].read_text(encoding="utf-8", errors="replace") if logs else ""
    return result.returncode, result.stdout, log


def make_layouts(games: Path) -> dict[str, str]:
    """Create the game folders; returns case -> expected project-dir suffix."""
    expected = {}
    d = games / "data_dir"
    (d / "data").mkdir(parents=True)
    (d / "data" / "startup.tjs").write_bytes(startup("data_dir"))
    expected["data_dir"] = "/data_dir/data/"

    d = games / "self_exe"
    d.mkdir()
    (d / "Game.exe").write_bytes(build_exe(build_archive([("startup.tjs", startup("self_exe"))])))
    (d / "voice.bin").write_bytes(b"not an archive")
    expected["self_exe"] = "/self_exe/Game.exe>"

    d = games / "data_exe"
    d.mkdir()
    (d / "data.exe").write_bytes(build_exe(build_archive([("startup.tjs", startup("data_exe"))])))
    (d / "tool.exe").write_bytes(build_exe(b"") + bytes(4096))  # bigger exe without an archive
    expected["data_exe"] = "/data_exe/data.exe>"

    d = games / "deep_exe"
    d.mkdir()
    # 6 MiB of resources after the image: outside the quick window, found by the full scan
    (d / "Game.exe").write_bytes(build_exe(build_archive([("startup.tjs", startup("deep_exe"))]),
                                           gap=6 * 1024 * 1024))
    expected["deep_exe"] = "/deep_exe/Game.exe>"

    (games / "not_a_game").mkdir()
    (games / "not_a_game" / "readme.txt").write_text("hello", encoding="ascii")
    (games / "plain_exe").mkdir()
    (games / "plain_exe" / "setup.exe").write_bytes(build_exe(b""))
    return expected


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())
    failures = []
    with tempfile.TemporaryDirectory(prefix="kk4s-layout-") as temp:
        temp_path = Path(temp)
        games = temp_path / "games-src"
        games.mkdir()
        expected = make_layouts(games)

        for case, suffix in expected.items():
            root = temp_path / f"root-{case}"
            code, _, log = run(executable, root, [str(games / case)])
            project = [l for l in log.splitlines() if l.startswith("project dir:")]
            if not project or not project[-1].endswith(suffix):
                failures.append(f"{case}: project dir {project[-1] if project else 'missing'}, want ...{suffix}")
            if f"LAYOUT_PASS {case}" not in log:
                failures.append(f"{case}: startup.tjs did not run (exit {code})\n" + log[-1500:])

        # --list-games: the launcher's view of a games/ folder
        root = temp_path / "root-list"
        root.mkdir()
        os.symlink(games, root / "games")
        code, stdout, log = run(executable, root, ["--list-games"])
        listed = {line.split("\t")[0] for line in stdout.splitlines() if "\t" in line}
        # deep_exe is only found by the full scan, which the launcher skips
        want = {"data_dir", "self_exe", "data_exe"}
        if not want <= listed:
            failures.append(f"--list-games missed {sorted(want - listed)}; listed {sorted(listed)}")
        if listed & {"not_a_game", "plain_exe"}:
            failures.append(f"--list-games listed non-games: {sorted(listed & {'not_a_game', 'plain_exe'})}")
        if code != 0:
            failures.append(f"--list-games exited with {code}")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print("game-folder layouts: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
