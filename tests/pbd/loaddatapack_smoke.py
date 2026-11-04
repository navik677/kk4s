"""Integration test: Scripts.loadDataPack returns real TJS Array/Dictionary trees.

  python tests/pbd/loaddatapack_smoke.py path/to/kk4s

Builds a TJS/4s0 (PackinOne crypt + LZ4) pack, a TJS/ns0 pack and an invalid
file, runs the engine's startup.tjs and checks the values the script saw. No
game data is used.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_tests as pbd  # encoder + canonical helpers, no side effects on import


SCRIPT = r"""
try {
    var p = Scripts.loadDataPack("test_4s0.pbd");
    Debug.message("PBD_4S0_BASE " + p[0].height + " " + p[0].width);
    Debug.message("PBD_4S0_LAYER " + p[1].name + " " + p[1].layer_id + " " + p[1].layer_type + " " + p[1].visible);

    var q = Scripts.loadDataPack("test_ns0.pbd");
    Debug.message("PBD_NS0 " + q.name + " " + q.count + " " + q.nested.x);

    try {
        Scripts.loadDataPack("bad.pbd");
        Debug.message("PBD_BAD_NOT_REJECTED");
    } catch(e) {
        Debug.message("PBD_BAD_REJECTED");
    }
    Debug.message("PBD_LOADDATAPACK_PASS");
} catch(e) {
    Debug.message("PBD_LOADDATAPACK_FAIL " + e.message);
}
"""


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())

    four_root = [
        {"height": 5000, "width": 3524},
        {"name": "レイヤ", "layer_id": 7, "layer_type": 0, "visible": 1,
         "left": 12, "top": 34, "width": 56, "height": 78, "opacity": 255, "type": 13},
    ]
    ns0_root = {"name": "ns0", "count": 7, "nested": {"x": 11}}

    with tempfile.TemporaryDirectory(prefix="kk4s-pbd-load-") as temp:
        temp_path = Path(temp)
        game = temp_path / "game"
        root = temp_path / "data"
        game.mkdir()
        root.mkdir()

        (game / "startup.tjs").write_text(SCRIPT, encoding="utf-8")
        (game / "test_4s0.pbd").write_bytes(
            pbd.build_4s0(four_root, 0x14353CC6, 1, b"", 0x11223344))
        (game / "test_ns0.pbd").write_bytes(pbd.build_ns0(ns0_root, 0x12345678))
        (game / "bad.pbd").write_bytes(b"not a data pack")

        input_script = temp_path / "input.txt"
        input_script.write_text("5000 quit\n", encoding="ascii")

        env = os.environ.copy()
        env["KK4S_ROOT"] = str(root)
        env["KK4S_INPUT_SCRIPT"] = str(input_script)
        env.pop("KK4S_DUMP_BYTECODE", None)
        env.pop("KK4S_KAG_TRACE", None)
        result = subprocess.run([executable, str(game)], env=env, timeout=45, check=False,
                                capture_output=True, text=True)

        logs = sorted((root / "logs").glob("kk4s-*.log"))
        if not logs:
            print(f"FAIL: kk4s produced no log (exit {result.returncode})", file=sys.stderr)
            if result.stdout:
                print(result.stdout[-2000:], file=sys.stderr)
            if result.stderr:
                print(result.stderr[-2000:], file=sys.stderr)
            return 1
        log_text = logs[-1].read_text(encoding="utf-8", errors="replace")
        for marker in ("PBD_4S0_BASE 5000 3524",
                       "PBD_4S0_LAYER レイヤ 7 0 1",
                       "PBD_NS0 ns0 7 11",
                       "PBD_BAD_REJECTED",
                       "PBD_LOADDATAPACK_PASS"):
            if marker not in log_text:
                print(f"FAIL: missing {marker!r}", file=sys.stderr)
                print(log_text[-6000:], file=sys.stderr)
                return 1
        if "PBD_LOADDATAPACK_FAIL" in log_text:
            print("FAIL: startup.tjs caught an exception", file=sys.stderr)
            print(log_text[-6000:], file=sys.stderr)
            return 1
        if result.returncode != 0:
            print(f"FAIL: kk4s exited with {result.returncode}", file=sys.stderr)
            return 1
    print("Scripts.loadDataPack: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
