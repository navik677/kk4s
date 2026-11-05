#!/usr/bin/env python3
"""A game's own XP3 filter plugin (.tpm) run in the x86 interpreter.

Usage: python tests/tpm/smoke.py path/to/kk4s

Builds the synthetic filter plugins of tests/tpm/src with i686-w64-mingw32-gcc
(our own code, written from the documented tp_stub ABI; skipped without the
compiler), encrypts a synthetic XP3 archive with each plugin's algorithm, puts
the plugin next to it (game folder or plugin/) with a plugin that sets no
filter, and checks that the game's scripts run and a 60 KB text file reads
back intact:
  xor    one byte per file          -> bound, XOR keystream, decrypted natively
  table  prefix + 256-byte period   -> bound, XOR keystream (cdecl exporter, kernel32 import)
  add    adds (hash ^ position)     -> bound, interpreted on every read
and that a plain archive next to a .tpm binds nothing. No game content is used.
"""
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "xp3"))
from crypt_smoke import build_archive  # noqa: E402

CC = "i686-w64-mingw32-gcc"
CFLAGS = ["-O2", "-march=i686", "-mno-mmx", "-mno-sse", "-shared", "-nostdlib",
          "-fno-asynchronous-unwind-tables", "-Wl,--entry,_DllMain@12"]
PLUGINS = {
    "xor": (["filter_xor.c"], ["-Wl,--kill-at"], ["-lgcc"]),
    "table": (["filter_table.c"], ["-Wl,--kill-at", "-DKK4S_TPM_CDECL"], ["-lkernel32", "-lgcc"]),
    "add": (["filter_add.c"], [], ["-lgcc"]),
    "nofilter": (["plugin_nofilter.c"], ["-Wl,--kill-at"], ["-lgcc"]),
}


def enc_xor(h, data):
    k = ((h >> 8) & 0xFF) ^ 0x5A
    return bytes(b ^ k for b in data)


def enc_table(h, data):
    out = bytearray(data)
    for o in range(len(out)):
        k = ((h & 0xFF) ^ 0xA5) if o < 0x80 else ((((o & 255) * 37 + 11) & 0xFF) ^ (h & 0xFF))
        out[o] ^= k
    return bytes(out)


def enc_add(h, data):
    return bytes((b - ((h ^ o) & 0xFF)) & 0xFF for o, b in enumerate(data))


def game_files(tag):
    rnd = random.Random(tag)
    big = "".join(f"line {i:04d} kirikiri\n" for i in range(3000)).encode("ascii")
    script = (f'var a = [];\n'
              f'a.load("big.txt");\n'
              f'if (a.count >= 3000 && a[0] == "line 0000 kirikiri" && a[1234] == "line 1234 kirikiri"'
              f' && a[2999] == "line 2999 kirikiri") Debug.message("TPMTEST_PASS {tag}");\n'
              f'else Debug.message("TPMTEST_FAIL {tag} count=" + a.count + " [1234]=" + a[1234]);\n')
    files = [("startup.tjs", script.encode("ascii")), ("big.txt", big)]
    for i, size in enumerate((40, 300, 1200, 4000, 9000)):
        files.append((f"data{i}.txt", bytes(rnd.choice(b"kirikiri tjs layer ") for _ in range(size))))
    return files


def run(executable, base, tag, encrypt, plugins):
    game, root = base / f"game-{tag}", base / f"root-{tag}"
    (game / "plugin").mkdir(parents=True)
    (game / "data.xp3").write_bytes(build_archive(game_files(tag), encrypt))
    for src, dest in plugins:
        shutil.copy(base / f"{src}.tpm", game / dest)
    script = base / f"input-{tag}.txt"
    script.write_text("4000 quit\n", encoding="ascii")
    env = os.environ.copy()
    env["KK4S_ROOT"] = str(root)
    env["KK4S_INPUT_SCRIPT"] = str(script)
    subprocess.run([executable, str(game)], env=env, timeout=90, check=False, capture_output=True)
    logs = sorted((root / "logs").glob("kk4s-*.log"))
    return logs[-1].read_text(encoding="utf-8", errors="replace") if logs else ""


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())
    if not shutil.which(CC):
        print(f"SKIP: no {CC} to build the test plugins")
        return 0
    failures = []
    with tempfile.TemporaryDirectory(prefix="kk4s-tpm-") as temp:
        base = Path(temp)
        for name, (srcs, extra, libs) in PLUGINS.items():
            cmd = [CC, *CFLAGS, *extra, "-o", str(base / f"{name}.tpm"), *[str(HERE / "src" / s) for s in srcs], *libs]
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0:
                print(f"building {name}.tpm failed:\n{r.stderr}", file=sys.stderr)
                return 1
        cases = [
            ("xor", enc_xor, [("xor", "crypt.tpm"), ("nofilter", "plugin/other.tpm")], "XOR keystream"),
            ("table", enc_table, [("table", "plugin/table.tpm")], "XOR keystream"),
            ("add", enc_add, [("add", "add.tpm")], "every read runs in the x86 interpreter"),
        ]
        for tag, encrypt, plugins, how in cases:
            log = run(executable, base, tag, encrypt, plugins)
            tpm = [l for l in log.splitlines() if "tpm:" in l]
            if f"TPMTEST_PASS {tag}" not in log or not any(" uses " in l and how in l for l in tpm):
                failures.append(f"{tag}: not decrypted as expected ({how})\n  " + "\n  ".join(tpm or ["no tpm lines"])
                                + "\n  " + "\n  ".join(l for l in log.splitlines() if "TPMTEST" in l or "xception" in l))
        if not any("other.tpm: V2Link did not set" in l for l in run(executable, base, "xor2", enc_xor,
                   [("xor", "crypt.tpm"), ("nofilter", "plugin/other.tpm")]).splitlines()):
            failures.append("a plugin without a filter must be reported and ignored")
        log = run(executable, base, "plain", lambda h, d: d, [("xor", "crypt.tpm")])
        if "TPMTEST_PASS plain" not in log or " uses " in log:
            failures.append("plain archive next to a .tpm: must run without a filter")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print("game .tpm filters in the x86 interpreter: PASS (xor, table, add + 2 controls)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
