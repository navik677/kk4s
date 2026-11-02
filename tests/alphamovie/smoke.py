#!/usr/bin/env python3
"""Run the generic AlphaMovie AJPM decoder against a generated 16x16 movie.

Usage: python tests/alphamovie/smoke.py path/to/kk4s

The movie and its temporary game folder are synthetic; no game assets are used.
"""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib


def append_bits(bits: list[int], value: int, count: int) -> None:
    for shift in range(count - 1, -1, -1):
        bits.append((value >> shift) & 1)


def make_entropy() -> bytes:
    # Standard baseline JPEG Huffman codes: DC category 0 is 00 for luma and
    # chroma; AC EOB is 1010 for luma and 00 for chroma. This makes a constant
    # 128 Y/Cb/Cr plane while exercising all six 4:2:0 color blocks.
    bits: list[int] = []
    for component in ("chroma", "chroma", "luma", "luma", "luma", "luma"):
        append_bits(bits, 0b00, 2)
        append_bits(bits, 0b00 if component == "chroma" else 0b1010,
                    2 if component == "chroma" else 4)
    while len(bits) % 8:
        bits.append(1)
    encoded = bytearray()
    for i in range(0, len(bits), 8):
        byte = 0
        for bit in bits[i:i + 8]:
            byte = (byte << 1) | bit
        encoded.append(byte)
    return bytes(encoded)


def make_movie() -> bytes:
    width = height = 16
    alpha = zlib.compress(bytes([255]) * (width * height))
    color = make_entropy()

    # AJPM header: revision 0, 128-byte quant table block, one frame, 30/1 FPS,
    # 16x16, flag 2 selects the zlib-alpha frame path.
    header = b"AJPM" + struct.pack(
        "<IIIIIIIHHI", 0, 0, 168, 0, 1, 30, 1, width, height, 2
    )
    assert len(header) == 40
    quantization = bytes([1]) * 128

    # Frame size counts bytes following the tag + size fields.
    payload_size = 16 + len(alpha) + len(color)
    frame = b"FRAM" + struct.pack(
        "<IIhhHHI", payload_size, 0, 0, 0, width, height, len(alpha)
    ) + alpha + color
    assert len(frame) == 24 + len(alpha) + len(color)
    return header + quantization + frame


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    executable = str(Path(sys.argv[1]).resolve())
    test_script = Path(__file__).resolve().parents[1] / "tjs" / "alphamovie" / "startup.tjs"

    with tempfile.TemporaryDirectory(prefix="kk4s-alphamovie-") as temp:
        temp_path = Path(temp)
        game = temp_path / "game"
        root = temp_path / "user-data"
        game.mkdir()
        (game / "startup.tjs").write_bytes(test_script.read_bytes())
        (game / "probe.amv").write_bytes(make_movie())
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
            print(f"FAIL: kk4s produced no log (exit {result.returncode}; root {root})", file=sys.stderr)
            if result.stdout:
                print(result.stdout[-2000:], file=sys.stderr)
            if result.stderr:
                print(result.stderr[-2000:], file=sys.stderr)
            print(f"files: {[str(p.relative_to(temp_path)) for p in temp_path.rglob('*')]}", file=sys.stderr)
            return 1
        log_text = logs[-1].read_text(encoding="utf-8", errors="replace")
        if "AlphaMovie AJPM synthetic decode: PASS" not in log_text:
            print(log_text[-5000:], file=sys.stderr)
            return 1
        if "(exception)" in log_text or result.returncode != 0:
            print(log_text[-5000:], file=sys.stderr)
            print(f"FAIL: kk4s exited with {result.returncode}", file=sys.stderr)
            return 1
    print("AlphaMovie AJPM synthetic decode: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
