"""PBD regression test: C++ decoder (compat/pbd) vs a Python encoder.

  python tests/pbd/run_tests.py PBD_DUMP_EXE [DIR_WITH_REAL_PBD_FILES]

Synthetic data packs are generated here (every value type, nested containers,
UTF-16 strings, all six TJS/4s0 crypt modes, empty/non-empty IVs, LZ4 frames
that reference the 64 KiB dictionary, truncation/corruption). No game data is
stored in the repository; a directory of real `TJS/ns0`/`TJS/4s0` files can be
passed to check those too.

Build the dumper:
  g++ -std=c++17 -O2 -I compat tests/pbd/pbd_dump.cpp compat/pbd/pbd.cpp -o pbd_dump
"""
import glob
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile

M = 0xFFFFFFFF
VOID = object()


# ---------------------------------------------------------------- xxh32 / chacha
def rotl(x, r):
    return ((x << r) | (x >> (32 - r))) & M


P1, P2, P3, P4, P5 = 0x9E3779B1, 0x85EBCA77, 0xC2B2AE3D, 0x27D4EB2F, 0x165667B1


def xxh32(data, seed):
    n = len(data)
    i = 0
    if n >= 16:
        v1 = (seed + P1 + P2) & M
        v2 = (seed + P2) & M
        v3 = seed
        v4 = (seed - P1) & M
        while i + 16 <= n:
            for which in range(4):
                lane = struct.unpack_from("<I", data, i)[0]
                if which == 0:
                    v1 = (rotl((v1 + lane * P2) & M, 13) * P1) & M
                elif which == 1:
                    v2 = (rotl((v2 + lane * P2) & M, 13) * P1) & M
                elif which == 2:
                    v3 = (rotl((v3 + lane * P2) & M, 13) * P1) & M
                else:
                    v4 = (rotl((v4 + lane * P2) & M, 13) * P1) & M
                i += 4
        h = (rotl(v1, 1) + rotl(v2, 7) + rotl(v3, 12) + rotl(v4, 18)) & M
    else:
        h = (seed + P5) & M
    h = (h + n) & M
    while i + 4 <= n:
        h = (rotl((h + struct.unpack_from("<I", data, i)[0] * P3) & M, 17) * P4) & M
        i += 4
    while i < n:
        h = (rotl((h + data[i] * P5) & M, 11) * P1) & M
        i += 1
    h ^= h >> 15
    h = (h * P2) & M
    h ^= h >> 13
    h = (h * P3) & M
    h ^= h >> 16
    return h & M


def quarter_round(s, a, b, c, d):
    s[a] = (s[a] + s[b]) & M
    s[d] = rotl(s[d] ^ s[a], 16)
    s[c] = (s[c] + s[d]) & M
    s[b] = rotl(s[b] ^ s[c], 12)
    s[a] = (s[a] + s[b]) & M
    s[d] = rotl(s[d] ^ s[a], 8)
    s[c] = (s[c] + s[d]) & M
    s[b] = rotl(s[b] ^ s[c], 7)


def chacha_block(key, counter, nonce_lo, nonce_hi, rounds):
    state = list(struct.unpack("<4I", b"expand 32-byte k")) + list(struct.unpack("<8I", key))
    state += [counter & M, (counter >> 32) & M, nonce_lo, nonce_hi]
    original = list(state)
    for _ in range(rounds // 2):
        quarter_round(state, 0, 4, 8, 12)
        quarter_round(state, 1, 5, 9, 13)
        quarter_round(state, 2, 6, 10, 14)
        quarter_round(state, 3, 7, 11, 15)
        quarter_round(state, 0, 5, 10, 15)
        quarter_round(state, 1, 6, 11, 12)
        quarter_round(state, 2, 7, 8, 13)
        quarter_round(state, 3, 4, 9, 14)
    for i in range(16):
        state[i] = (state[i] + original[i]) & M
    return struct.pack("<16I", *state)


def xorshift32(x, fallback):
    x ^= (x << 13) & M
    x ^= x >> 17
    x ^= (x << 5) & M
    x &= M
    return fallback if x == 0 else x


def expand_stream(base, blocks, fallback):
    if blocks <= 1:
        return base
    words = list(struct.unpack("<16I", base))
    source = 0
    for _ in range(blocks * 4 - 4):
        for _ in range(4):
            words.append(xorshift32(words[source], fallback))
            source += 1
    return struct.pack("<%dI" % len(words), *words)


CRYPT_PARAMS = {0: None, 1: (8, 0x10), 2: (12, 8), 3: (20, 4), 4: (8, 1), 5: (12, 1), 6: (20, 1)}


def crypt_4s0(data, seed, crypt, iv):
    params = CRYPT_PARAMS[crypt]
    if params is None:
        return data
    rounds, blocks = params
    key = hashlib.blake2s(iv, key=struct.pack("<I", seed), digest_size=32).digest()
    nonce_lo = xxh32(iv, seed)
    nonce_hi = seed
    fallback = nonce_hi ^ nonce_lo
    if fallback == 0:
        fallback = seed
    if fallback == 0:
        fallback = 0xFFFFFFFF
    out = bytearray(data)
    offset = 0
    counter = 0
    while offset < len(out):
        stream = expand_stream(chacha_block(key, counter, nonce_lo, nonce_hi, rounds), blocks, fallback)
        counter += 1
        take = min(len(stream), len(out) - offset)
        for i in range(take):
            out[offset + i] ^= stream[i]
        offset += take
    return bytes(out)


# ---------------------------------------------------------------- type checkers
class ByteChecker:
    def __init__(self, seed):
        self.seed = seed

    @staticmethod
    def _round(b):
        a = (b[0] ^ (b[0] * 2)) & 0xFF
        v = a
        v >>= 2
        v ^= b[2]
        v >>= 3
        v ^= b[2]
        v ^= a
        b[0], b[1], b[2] = b[1], b[2], v & 0xFF

    def byte_for_type(self, t):
        b = list(struct.pack("<I", self.seed))
        if t == 0:
            return b[2]
        self._round(b)
        self.seed = struct.unpack("<I", bytes(b))[0]
        return b[2]

    def final_check(self):
        b = list(struct.pack("<I", self.seed))
        self._round(b)
        self._round(b)
        self._round(b)
        b[0], b[2] = b[2], b[0]
        return struct.unpack("<I", bytes(b))[0]


class FourS0Checker:
    def __init__(self, seed):
        self.b0 = ((seed >> 24) & 0xFF) ^ (seed & 0xFF)
        self.b1 = (seed >> 8) & 0xFF
        self.b2 = (seed >> 16) & 0xFF

    def byte_for_type(self, t):
        if t == 0:
            return self.b2
        x = self.b0 ^ ((self.b0 << 1) & 0xFF)
        self.b0, self.b1 = self.b1, self.b2
        self.b2 = (self.b2 ^ (self.b2 >> 3) ^ x ^ (x >> 5)) & 0xFF
        return self.b2


# ---------------------------------------------------------------- writer
def write_value(out, value, checker, depth=0):
    if depth > 256:
        raise ValueError("too deep")
    if value is VOID:
        t = 0
    elif isinstance(value, str):
        t = 2
    elif isinstance(value, bool):
        raise TypeError("bool is not a TJS data pack type")
    elif isinstance(value, int):
        t = 4
    elif isinstance(value, float):
        t = 5
    elif isinstance(value, list):
        t = 0x81
    elif isinstance(value, dict):
        t = 0xC1
    else:
        raise TypeError(type(value))

    out.append(t)
    out.append(checker.byte_for_type(t))
    if t == 0:
        return
    if t == 2:
        units = value.encode("utf-16-le")
        out += struct.pack("<I", len(units) // 2) + units
    elif t == 4:
        out += struct.pack("<q", value)
    elif t == 5:
        out += struct.pack("<d", value)
    elif t == 0x81:
        out += struct.pack("<I", len(value))
        for item in value:
            write_value(out, item, checker, depth + 1)
    else:
        out += struct.pack("<I", len(value))
        for key, item in value.items():
            units = key.encode("utf-16-le")
            out += struct.pack("<I", len(units) // 2) + units
            write_value(out, item, checker, depth + 1)


def build_ns0(root, seed):
    checker = ByteChecker(seed)
    body = bytearray()
    write_value(body, root, checker)
    body += struct.pack("<I", checker.final_check())
    return b"TJS/ns0\0" + struct.pack("<IHH", seed, 0, 0) + bytes(body)


def lz4_literals(data):
    out = bytearray()
    n = len(data)
    out.append((min(n, 15) << 4) & 0xFF)
    if n >= 15:
        rest = n - 15
        while rest >= 255:
            out.append(255)
            rest -= 255
        out.append(rest)
    out += data
    return bytes(out)


def lz4_match(offset, length):
    """A match-only LZ4 sequence (length includes the 4-byte minimum)."""
    extra = length - 4
    token = 15 if extra >= 15 else extra
    out = bytearray([token])
    out += struct.pack("<H", offset)
    if extra >= 15:
        rest = extra - 15
        while rest >= 255:
            out.append(255)
            rest -= 255
        out.append(rest)
    return bytes(out)


def frame_literals(plain, chunk=4096):
    out = b""
    for i in range(0, len(plain), chunk):
        block = lz4_literals(plain[i:i + chunk])
        out += struct.pack("<H", len(block)) + block
    return out


def build_4s0(root, seed, crypt, iv, trailer, frames=None):
    checker = FourS0Checker(seed)
    body = bytearray()
    write_value(body, root, checker)
    body += struct.pack("<I", trailer)
    framed = frame_literals(bytes(body)) if frames is None else frames
    if crypt:
        framed = crypt_4s0(framed, seed, crypt, iv)
    return (b"TJS/4s0\0" + struct.pack("<IHH", seed, crypt, len(iv)) + iv + framed)


# ---------------------------------------------------------------- canonical JSON
def quote(s):
    out = ['"']
    for ch in s:
        o = ord(ch)
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif ch == "\b":
            out.append("\\b")
        elif ch == "\f":
            out.append("\\f")
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        elif o < 0x20:
            out.append("\\u%04x" % o)
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def canon(value):
    if value is VOID:
        return "null"
    if isinstance(value, str):
        return quote(value)
    if isinstance(value, bool):
        raise TypeError(value)
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return "%.17g" % value
    if isinstance(value, list):
        return "[" + ",".join(canon(v) for v in value) + "]"
    if isinstance(value, dict):
        return "{" + ",".join(quote(k) + ":" + canon(v) for k, v in value.items()) + "}"
    raise TypeError(type(value))


# ---------------------------------------------------------------- samples
def samples():
    root = {
        "void": VOID,
        "int": -42,
        "big": (1 << 62) + 5,
        "double": 1.5,
        "str": "日本語 тест",
        "astral": "🙂",
        "empty": "",
        "quote": 'a"b\\c\nd',
        "arr": [1, "two", [3, [4]], {"five": 5}, VOID],
        "emptyarr": [],
        "emptydict": {},
        "nested": {"a": {"b": [1, 2, {"c": "d"}]}},
    }
    out = {"ns0": (build_ns0(root, 0x12345678), root)}
    for crypt in range(0, 7):
        iv = b"" if crypt in (0, 4) else ("kk4s-iv-%d" % crypt).encode("ascii")
        name = "4s0_crypt%d" % crypt
        out[name] = (build_4s0(root, 0x14353CC6, crypt, iv, 0x11223344), root)
    return out


def dictionary_sample():
    """A TJS/4s0 file whose second LZ4 block is a match into the first block."""
    seed = 0x2F2F2F2F
    string = "ABCD" * 1000  # 4000 UTF-16 units -> 8000 bytes, period 8
    root = string
    checker = FourS0Checker(seed)
    plain = bytearray()
    write_value(plain, root, checker)
    plain += struct.pack("<I", 0x55667788)
    # write_value layout: type(1) + check(1) + units(4), then the UTF-16 bytes
    split1 = 6006  # (6006 - 6) % 8 == 0, inside the repeated string
    split2 = 7006
    assert 6 + 8000 >= split2
    block1 = lz4_literals(bytes(plain[:split1]))
    block2 = lz4_match(8, split2 - split1)
    block3 = lz4_literals(bytes(plain[split2:]))
    framed = b""
    for block in (block1, block2, block3):
        framed += struct.pack("<H", len(block)) + block
    data = b"TJS/4s0\0" + struct.pack("<IHH", seed, 1, 0) + crypt_4s0(framed, seed, 1, b"")
    return [("4s0_dict", data, root)]


def corrupt(data):
    out = {}
    for n in (4, 15, 16, 20, len(data) // 2, len(data) - 1):
        if 0 < n < len(data):
            out["trunc%d" % n] = data[:n]
    for i in range(0, min(len(data), 300), 29):
        b = bytearray(data)
        b[i] ^= 0xFF
        out["flip%d" % i] = bytes(b)
    return out


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    exe = sys.argv[1]
    real = sys.argv[2] if len(sys.argv) > 2 else None
    failures = 0
    checked = 0

    # Anchor the cipher against the PackinOne reference vector from xp3-brute.
    key = hashlib.blake2s(b"", key=struct.pack("<I", 0x14353CC6), digest_size=32).digest()
    assert key.hex() == "2fca145d50b8cb030395f0868c49ed85aa0280c6d0b844c09673351e64408aed"
    assert xxh32(b"", 0x14353CC6) == 0x984823D3
    stream = expand_stream(chacha_block(key, 0, 0x984823D3, 0x14353CC6, 8), 0x10, 0x14353CC6 ^ 0x984823D3)
    assert stream[:16].hex() == "2e9d7adaabc57ee65d0d4f2b440895d5"

    with tempfile.TemporaryDirectory(prefix="kk4s-pbd-") as td:
        def run(name, data, expected):
            nonlocal failures, checked
            path = os.path.join(td, "t.pbd")
            with open(path, "wb") as f:
                f.write(data)
            result = subprocess.run([exe, path], capture_output=True)
            if result.returncode not in (0, 1):
                print("CRASH %s (exit %s)" % (name, result.returncode))
                failures += 1
                return
            checked += 1
            if expected is None:
                return  # corruption: either a value or a clean error is fine
            got = result.stdout.decode("utf-8", "replace").rstrip("\r\n")
            want = canon(expected)
            if result.returncode != 0 or got != want:
                print("MISMATCH %s\n  got (%d): %s\n  want: %s" % (
                    name, result.returncode, got[:240], want[:240]))
                failures += 1

        table = samples()
        for name, (data, root) in table.items():
            run(name, data, root)
        for name, data, root in dictionary_sample():
            run(name, data, root)

        # Corruption must be rejected cleanly, never crash.
        for name in ("ns0", "4s0_crypt1", "4s0_dict"):
            data = table.get(name, (None, None))[0]
            if data is None:
                data = dict((n, d) for n, d, _ in dictionary_sample())[name]
            for cname, cdata in corrupt(data).items():
                run(name + "/" + cname, cdata, None)

    if real:
        for path in glob.glob(os.path.join(real, "**", "*.pbd"), recursive=True):
            with open(path, "rb") as f:
                data = f.read()
            result = subprocess.run([exe, path], capture_output=True)
            checked += 1
            if result.returncode != 0 or not result.stdout.strip():
                print("REAL FAIL %s: %s" % (path, result.stderr.decode("utf-8", "replace").strip()[:200]))
                failures += 1

    print("pbd tests: %d checked, %d failed" % (checked, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
