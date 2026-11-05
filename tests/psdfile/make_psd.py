#!/usr/bin/env python3
"""Writes a small synthetic PSD (8x8 RGB, two raw-data layers) for tests/psdfile.

Layer "red": 4x4 opaque red at (1, 1). Layer "blue": 2x2 opaque blue at
(5, 5), above red. The merged image is the same picture on white. Plain
Photoshop format, no compression, no game content.
"""
import struct
import sys

W = H = 8


def pascal4(name: bytes) -> bytes:
    data = bytes([len(name)]) + name
    return data + b"\0" * (-len(data) % 4)


def layer(name, top, left, h, w, rgb):
    chans = [(-1, bytes([255]) * (w * h))] + [(i, bytes([c]) * (w * h)) for i, c in enumerate(rgb)]
    rec = struct.pack(">iiiiH", top, left, top + h, left + w, len(chans))
    for cid, data in chans:
        rec += struct.pack(">hI", cid, 2 + len(data))
    extra = struct.pack(">I", 0) + struct.pack(">I", 0) + pascal4(name)
    rec += b"8BIMnorm" + bytes([255, 0, 0, 0]) + struct.pack(">I", len(extra)) + extra
    image = b"".join(struct.pack(">H", 0) + data for _, data in chans)
    return rec, image


def main(path):
    layers = [layer(b"red", 1, 1, 4, 4, (255, 0, 0)), layer(b"blue", 5, 5, 2, 2, (0, 0, 255))]
    info = struct.pack(">h", len(layers)) + b"".join(r for r, _ in layers) + b"".join(i for _, i in layers)
    info += b"\0" * (len(info) % 2)
    layer_info = struct.pack(">I", len(info)) + info
    mask_section = layer_info + struct.pack(">I", 0)
    # merged picture: white, red square, blue square
    px = [[(255, 255, 255)] * W for _ in range(H)]
    for y in range(1, 5):
        for x in range(1, 5):
            px[y][x] = (255, 0, 0)
    for y in range(5, 7):
        for x in range(5, 7):
            px[y][x] = (0, 0, 255)
    planes = b"".join(bytes(px[y][x][c] for y in range(H) for x in range(W)) for c in range(3))
    out = b"8BPS" + struct.pack(">H6xHIIHH", 1, 3, H, W, 8, 3)
    out += struct.pack(">I", 0) + struct.pack(">I", 0)
    out += struct.pack(">I", len(mask_section)) + mask_section
    out += struct.pack(">H", 0) + planes
    open(path, "wb").write(out)


if __name__ == "__main__":
    main(sys.argv[1])
