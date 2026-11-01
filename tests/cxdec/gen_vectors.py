"""Generates tests/cxdec/vectors.txt from the Python reference (tools/cxdec_ref.py).

The schemes are synthetic (xorshift32 PRNG, see synth_scheme); no game data.
cxdec_test.cpp rebuilds the same schemes and must produce the same adler32 values.
Line format: scheme_seed hash length adler32_of_decrypted(buffer[i] = i & 0xff)
"""
import os, sys, zlib
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
import cxdec_ref


class Rng:
    def __init__(self, s): self.s = s or 1
    def __call__(self):
        x = self.s
        x ^= (x << 13) & 0xffffffff; x ^= x >> 17; x ^= (x << 5) & 0xffffffff
        self.s = x
        return x


def perm(rng, n):
    v = list(range(n))
    for i in range(n - 1, 0, -1):
        j = rng() % (i + 1)
        v[i], v[j] = v[j], v[i]
    return v


def synth_scheme(seed):
    rng = Rng(seed)
    cb = b"".join(rng().to_bytes(4, "little") for _ in range(1024))
    mask = rng() & 0x3ff
    offset = rng() & 0x7ff
    return cxdec_ref.Cx(mask, offset, perm(rng, 3), perm(rng, 6), perm(rng, 8), cb), rng


def fast_decrypt(cx, h, buf):
    def dec(key, off, start, n):
        r1, r2 = cx.xcode(key)
        k1, k2, k3 = r2 >> 16, r2 & 0xffff, r1 & 0xff
        if k1 == k2: k2 += 1
        if k3 == 0: k3 = 1
        buf[start:start + n] = bytes(buf[start:start + n]).translate(bytes(i ^ k3 for i in range(256)))
        if off <= k2 < off + n: buf[start + k2 - off] ^= (r1 >> 16) & 0xff
        if off <= k1 < off + n: buf[start + k1 - off] ^= (r1 >> 8) & 0xff
    base = (h & cx.mask) + cx.offset
    n = len(buf)
    if base > 0: dec(h, 0, 0, min(base, n))
    if n > base: dec((h >> 16) ^ h, base, base, n - base)


def main():
    out = []
    for seed in (1, 0x1234567, 0xdeadbeef, 0x5eccd8aa):
        cx, rng = synth_scheme(seed)
        for _ in range(96):
            h = rng()
            n = rng() % 70000 + 1
            buf = bytearray(i & 0xff for i in range(n))
            fast_decrypt(cx, h, buf)
            out.append("%08x %08x %d %08x" % (seed, h, n, zlib.adler32(bytes(buf)) & 0xffffffff))
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "vectors.txt")
    open(path, "w", newline="\n").write("\n".join(out) + "\n")
    print(len(out), "vectors ->", path)


if __name__ == "__main__":
    main()
