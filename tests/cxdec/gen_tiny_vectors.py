"""Generates tests/cxdec/tiny_vectors.txt from the Python reference (tools/cxdec_ref.py).

No game data: the adlr values are synthetic (xorshift32 PRNG), plus the explicit
case whose keystream byte is zero before the filter's 0xd7 substitution.
Line format: adlr length adler32_of_decrypted(buffer[i] = i & 0xff)
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


def main():
    out = []
    rng = Rng(0x5eed1234)
    # 0x01010101 ^ TINY_CONST has a zero xor-of-bytes, so the key must become 0xd7.
    zero_case = 0x01010101 ^ cxdec_ref.TINY_CONST
    aldlrs = [zero_case, 0, 0xffffffff, 0x1548e29c, 0x6094b719]
    aldlrs += [rng() for _ in range(91)]
    for adlr in aldlrs:
        n = rng() % 70000 + 1
        buf = bytes(i & 0xff for i in range(n))
        out.append("%08x %d %08x" % (adlr & 0xffffffff, n,
                                     zlib.adler32(cxdec_ref.tiny_decrypt(adlr, buf)) & 0xffffffff))
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tiny_vectors.txt")
    open(path, "w", newline="\n").write("\n".join(out) + "\n")
    print(len(out), "vectors ->", path)


if __name__ == "__main__":
    main()
