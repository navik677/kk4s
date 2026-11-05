#!/usr/bin/env python3
"""Check the layerExSave.dll replacement (image saving and helpers).

Usage: python tests/layersave/smoke.py path/to/kk4s

Boots tests/tjs/layersave/startup.tjs (PNG / TLG5 round trips, TLG tags, the
analysis helpers, Window.startSaveLayerImage events) and decodes the saved PNG
here as an independent check of the colour order. No game assets are used.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zlib

PASS = "layerExSave synthetic contract: PASS"


def png_pixel(data: bytes, x: int, y: int) -> tuple:
    """RGBA of one pixel of an 8-bit RGBA, non-interlaced PNG (filters undone)."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat, width = 8, b"", 0
    while pos < len(data):
        n = int.from_bytes(data[pos:pos + 4], "big")
        kind, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            width = int.from_bytes(body[:4], "big")
            assert body[8] == 8 and body[9] == 6, "expected 8-bit RGBA"
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw, bpp, stride = zlib.decompress(idat), 4, width * 4
    rows, prev = [], bytearray(stride)
    for r in range(len(raw) // (stride + 1)):
        f, line = raw[r * (stride + 1)], bytearray(raw[r * (stride + 1) + 1:(r + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return tuple(rows[y][x * 4:x * 4 + 4])


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())
    test_script = Path(__file__).resolve().parents[1] / "tjs" / "layersave" / "startup.tjs"

    with tempfile.TemporaryDirectory(prefix="kk4s-layersave-") as temp:
        temp_path = Path(temp)
        game = temp_path / "game"
        root = temp_path / "user-data"
        game.mkdir()
        (game / "startup.tjs").write_bytes(test_script.read_bytes())
        input_script = temp_path / "input.txt"
        input_script.write_text("3000 quit\n", encoding="ascii")

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
            print(result.stdout[-2000:], result.stderr[-2000:], file=sys.stderr)
            return 1
        log_text = logs[-1].read_text(encoding="utf-8", errors="replace")
        failures = [line for line in log_text.splitlines() if "layerSave FAIL" in line]
        if failures or PASS not in log_text:
            print("\n".join(failures) or log_text[-5000:], file=sys.stderr)
            return 1
        pngs = list(root.rglob("probe.png"))
        if not pngs or png_pixel(pngs[0].read_bytes(), 1, 1) != (255, 0, 0, 255):
            print(f"FAIL: saved PNG pixel is {png_pixel(pngs[0].read_bytes(), 1, 1) if pngs else 'missing'}", file=sys.stderr)
            return 1
        if any(p.name != "startup.tjs" for p in game.iterdir()):
            print("FAIL: the game folder was modified", file=sys.stderr)
            return 1
        if "(exception)" in log_text or result.returncode != 0:
            print(log_text[-5000:], file=sys.stderr)
            print(f"FAIL: kk4s exited with {result.returncode}", file=sys.stderr)
            return 1
    print(PASS)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
