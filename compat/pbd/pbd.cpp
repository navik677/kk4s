#include "pbd.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace kk4s {
namespace pbd {
namespace {

constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kLz4DictSize = 64 * 1024;
constexpr int kMaxDepth = 256;
constexpr std::uint32_t kMaxContainerItems = 1'000'000;
constexpr std::uint32_t kMaxStringUnits = 64 * 1024 * 1024;

const std::uint8_t kMagicNs0[8] = {'T', 'J', 'S', '/', 'n', 's', '0', 0};
const std::uint8_t kMagic4s0[8] = {'T', 'J', 'S', '/', '4', 's', '0', 0};

std::uint16_t Le16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
std::uint32_t Le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint64_t Le64(const std::uint8_t* p) {
    return static_cast<std::uint64_t>(Le32(p)) | (static_cast<std::uint64_t>(Le32(p + 4)) << 32);
}

std::uint32_t Rotl32(std::uint32_t x, unsigned n) {
    return (x << n) | (x >> (32 - n));
}
std::uint32_t Rotr32(std::uint32_t x, unsigned n) {
    return (x >> n) | (x << (32 - n));
}

// ---------------------------------------------------------------------------
// XXH32 (the data pack cipher uses it as the second nonce)

std::uint32_t Xxh32(const std::uint8_t* data, std::size_t size, std::uint32_t seed) {
    const std::uint32_t P1 = 0x9E3779B1u;
    const std::uint32_t P2 = 0x85EBCA77u;
    const std::uint32_t P3 = 0xC2B2AE3Du;
    const std::uint32_t P4 = 0x27D4EB2Fu;
    const std::uint32_t P5 = 0x165667B1u;

    std::size_t i = 0;
    std::uint32_t h;
    if (size >= 16) {
        std::uint32_t v1 = seed + P1 + P2;
        std::uint32_t v2 = seed + P2;
        std::uint32_t v3 = seed;
        std::uint32_t v4 = seed - P1;
        while (i + 16 <= size) {
            v1 = Rotl32(v1 + Le32(data + i) * P2, 13) * P1;
            i += 4;
            v2 = Rotl32(v2 + Le32(data + i) * P2, 13) * P1;
            i += 4;
            v3 = Rotl32(v3 + Le32(data + i) * P2, 13) * P1;
            i += 4;
            v4 = Rotl32(v4 + Le32(data + i) * P2, 13) * P1;
            i += 4;
        }
        h = Rotl32(v1, 1) + Rotl32(v2, 7) + Rotl32(v3, 12) + Rotl32(v4, 18);
    } else {
        h = seed + P5;
    }
    h += static_cast<std::uint32_t>(size);
    while (i + 4 <= size) {
        h = Rotl32(h + Le32(data + i) * P3, 17) * P4;
        i += 4;
    }
    while (i < size) {
        h = Rotl32(h + data[i] * P5, 11) * P1;
        i++;
    }
    h ^= h >> 15;
    h *= P2;
    h ^= h >> 13;
    h *= P3;
    h ^= h >> 16;
    return h;
}

// ---------------------------------------------------------------------------
// LZ4 block with an external dictionary (no library dependency: the format
// used by data packs is the standard LZ4 block sequence).
bool Lz4Block(const std::uint8_t* src, std::size_t srcSize, const std::uint8_t* dict,
              std::size_t dictSize, std::vector<std::uint8_t>& out, std::string& error) {
    std::vector<std::uint8_t> history;
    history.reserve(dictSize + kLz4DictSize);
    if (dictSize) history.insert(history.end(), dict, dict + dictSize);

    std::size_t pos = 0;
    while (pos < srcSize) {
        std::uint8_t token = src[pos++];
        std::size_t literalLength = token >> 4;
        if (literalLength == 15) {
            std::uint8_t extension = 0;
            do {
                if (pos >= srcSize) {
                    error = "TJS/4s0 LZ4 literal length is truncated";
                    return false;
                }
                extension = src[pos++];
                literalLength += extension;
            } while (extension == 255);
        }
        if (literalLength > srcSize - pos) {
            error = "TJS/4s0 LZ4 literals are truncated";
            return false;
        }
        history.insert(history.end(), src + pos, src + pos + literalLength);
        pos += literalLength;
        if (history.size() - dictSize > kLz4DictSize) {
            error = "TJS/4s0 LZ4 block expands past 64 KiB";
            return false;
        }
        if (pos == srcSize) break;  // a block may end with a literal-only sequence

        if (srcSize - pos < 2) {
            error = "TJS/4s0 LZ4 match offset is truncated";
            return false;
        }
        std::size_t offset = static_cast<std::size_t>(src[pos]) |
                             (static_cast<std::size_t>(src[pos + 1]) << 8);
        pos += 2;
        if (offset == 0 || offset > history.size()) {
            error = "TJS/4s0 LZ4 match offset is out of range";
            return false;
        }
        std::size_t matchLength = token & 0x0F;
        if (matchLength == 15) {
            std::uint8_t extension = 0;
            do {
                if (pos >= srcSize) {
                    error = "TJS/4s0 LZ4 match length is truncated";
                    return false;
                }
                extension = src[pos++];
                matchLength += extension;
            } while (extension == 255);
        }
        matchLength += 4;
        std::size_t start = history.size() - offset;
        for (std::size_t i = 0; i < matchLength; i++) {
            history.push_back(history[start + i]);
            if (history.size() - dictSize > kLz4DictSize) {
                error = "TJS/4s0 LZ4 block expands past 64 KiB";
                return false;
            }
        }
    }

    out.assign(history.begin() + dictSize, history.end());
    return true;
}

// ---------------------------------------------------------------------------
// BLAKE2s-256, RFC 7693 (keyed mode is used by the data pack cipher)

const std::uint32_t kBlake2sIv[8] = {
    0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
    0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u,
};

const std::uint8_t kBlake2sSigma[10][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
    {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
    {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
    {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
    {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
    {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
    {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
    {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
};

void Blake2sG(std::uint32_t v[16], int a, int b, int c, int d, std::uint32_t x, std::uint32_t y) {
    v[a] = v[a] + v[b] + x;
    v[d] = Rotr32(v[d] ^ v[a], 16);
    v[c] = v[c] + v[d];
    v[b] = Rotr32(v[b] ^ v[c], 12);
    v[a] = v[a] + v[b] + y;
    v[d] = Rotr32(v[d] ^ v[a], 8);
    v[c] = v[c] + v[d];
    v[b] = Rotr32(v[b] ^ v[c], 7);
}

void Blake2sCompress(std::uint32_t h[8], const std::uint8_t block[64], std::uint64_t counter,
                     bool last) {
    std::uint32_t m[16];
    for (int i = 0; i < 16; i++) m[i] = Le32(block + i * 4);

    std::uint32_t v[16];
    for (int i = 0; i < 8; i++) v[i] = h[i];
    for (int i = 0; i < 8; i++) v[8 + i] = kBlake2sIv[i];
    v[12] ^= static_cast<std::uint32_t>(counter);
    v[13] ^= static_cast<std::uint32_t>(counter >> 32);
    if (last) v[14] = ~v[14];

    for (int r = 0; r < 10; r++) {
        const std::uint8_t* s = kBlake2sSigma[r];
        Blake2sG(v, 0, 4, 8, 12, m[s[0]], m[s[1]]);
        Blake2sG(v, 1, 5, 9, 13, m[s[2]], m[s[3]]);
        Blake2sG(v, 2, 6, 10, 14, m[s[4]], m[s[5]]);
        Blake2sG(v, 3, 7, 11, 15, m[s[6]], m[s[7]]);
        Blake2sG(v, 0, 5, 10, 15, m[s[8]], m[s[9]]);
        Blake2sG(v, 1, 6, 11, 12, m[s[10]], m[s[11]]);
        Blake2sG(v, 2, 7, 8, 13, m[s[12]], m[s[13]]);
        Blake2sG(v, 3, 4, 9, 14, m[s[14]], m[s[15]]);
    }

    for (int i = 0; i < 8; i++) h[i] ^= v[i] ^ v[i + 8];
}

void Blake2sKeyed256(const std::uint8_t* key, std::size_t keyLen, const std::uint8_t* message,
                     std::size_t messageLen, std::uint8_t out[32]) {
    std::uint32_t h[8];
    for (int i = 0; i < 8; i++) h[i] = kBlake2sIv[i];
    // digest length 32, key length in the second parameter byte
    h[0] ^= 0x01010000u ^ (static_cast<std::uint32_t>(keyLen) << 8) ^ 32u;

    std::uint64_t counter = 0;
    if (keyLen) {
        std::uint8_t keyBlock[64] = {0};
        std::memcpy(keyBlock, key, std::min<std::size_t>(keyLen, 64));
        if (messageLen == 0) {
            Blake2sCompress(h, keyBlock, 64, true);
            counter = 64;
        } else {
            Blake2sCompress(h, keyBlock, 64, false);
            counter = 64;
        }
    }

    std::size_t offset = 0;
    while (messageLen - offset > 64) {
        Blake2sCompress(h, message + offset, counter + 64, false);
        counter += 64;
        offset += 64;
    }

    std::uint8_t last[64] = {0};
    std::size_t remaining = messageLen - offset;
    if (remaining) std::memcpy(last, message + offset, remaining);
    if (!(keyLen && messageLen == 0)) {
        Blake2sCompress(h, last, counter + remaining, true);
    }

    for (int i = 0; i < 8; i++) {
        out[i * 4 + 0] = static_cast<std::uint8_t>(h[i]);
        out[i * 4 + 1] = static_cast<std::uint8_t>(h[i] >> 8);
        out[i * 4 + 2] = static_cast<std::uint8_t>(h[i] >> 16);
        out[i * 4 + 3] = static_cast<std::uint8_t>(h[i] >> 24);
    }
}

// ---------------------------------------------------------------------------
// TJS/4s0 cipher: ChaCha rounds + PackinOne xorshift expansion

void ChaChaBlock(const std::uint8_t key[32], std::uint64_t counter, std::uint32_t nonceLo,
                 std::uint32_t nonceHi, int rounds, std::uint8_t out[64]) {
    static const std::uint8_t kConstants[16] = {'e', 'x', 'p', 'a', 'n', 'd', ' ', '3',
                                                '2', '-', 'b', 'y', 't', 'e', ' ', 'k'};
    std::uint32_t state[16];
    for (int i = 0; i < 4; i++) state[i] = Le32(kConstants + i * 4);
    for (int i = 0; i < 8; i++) state[4 + i] = Le32(key + i * 4);
    state[12] = static_cast<std::uint32_t>(counter);
    state[13] = static_cast<std::uint32_t>(counter >> 32);
    state[14] = nonceLo;
    state[15] = nonceHi;

    std::uint32_t original[16];
    std::memcpy(original, state, sizeof state);

    auto quarter = [&state](int a, int b, int c, int d) {
        state[a] += state[b];
        state[d] = Rotl32(state[d] ^ state[a], 16);
        state[c] += state[d];
        state[b] = Rotl32(state[b] ^ state[c], 12);
        state[a] += state[b];
        state[d] = Rotl32(state[d] ^ state[a], 8);
        state[c] += state[d];
        state[b] = Rotl32(state[b] ^ state[c], 7);
    };

    for (int i = 0; i < rounds / 2; i++) {
        quarter(0, 4, 8, 12);
        quarter(1, 5, 9, 13);
        quarter(2, 6, 10, 14);
        quarter(3, 7, 11, 15);
        quarter(0, 5, 10, 15);
        quarter(1, 6, 11, 12);
        quarter(2, 7, 8, 13);
        quarter(3, 4, 9, 14);
    }

    for (int i = 0; i < 16; i++) state[i] += original[i];
    for (int i = 0; i < 16; i++) {
        out[i * 4 + 0] = static_cast<std::uint8_t>(state[i]);
        out[i * 4 + 1] = static_cast<std::uint8_t>(state[i] >> 8);
        out[i * 4 + 2] = static_cast<std::uint8_t>(state[i] >> 16);
        out[i * 4 + 3] = static_cast<std::uint8_t>(state[i] >> 24);
    }
}

std::uint32_t XorShift32(std::uint32_t x, std::uint32_t fallback) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x == 0 ? fallback : x;
}

// PackinOne expands a 64-byte ChaCha block to `blocks * 64` bytes by feeding
// already-produced words, in order, through xorshift32.
std::vector<std::uint8_t> ExpandStream(const std::uint8_t base[64], int blocks,
                                       std::uint32_t fallback) {
    if (blocks <= 1) return std::vector<std::uint8_t>(base, base + 64);

    std::vector<std::uint32_t> words;
    words.reserve(static_cast<std::size_t>(blocks) * 16);
    for (int i = 0; i < 16; i++) words.push_back(Le32(base + i * 4));

    std::size_t source = 0;
    const std::size_t groups = static_cast<std::size_t>(blocks) * 4 - 4;
    for (std::size_t g = 0; g < groups; g++) {
        for (int i = 0; i < 4; i++) {
            std::uint32_t next = XorShift32(words[source++], fallback);
            words.push_back(next);
        }
    }

    std::vector<std::uint8_t> out(words.size() * 4);
    for (std::size_t i = 0; i < words.size(); i++) {
        out[i * 4 + 0] = static_cast<std::uint8_t>(words[i]);
        out[i * 4 + 1] = static_cast<std::uint8_t>(words[i] >> 8);
        out[i * 4 + 2] = static_cast<std::uint8_t>(words[i] >> 16);
        out[i * 4 + 3] = static_cast<std::uint8_t>(words[i] >> 24);
    }
    return out;
}

bool CryptParameters(std::uint16_t crypt, int& rounds, int& blocks) {
    switch (crypt) {
        case 0: return false;  // no cipher
        case 1: rounds = 8; blocks = 0x10; return true;
        case 2: rounds = 12; blocks = 8; return true;
        case 3: rounds = 20; blocks = 4; return true;
        case 4: rounds = 8; blocks = 1; return true;
        case 5: rounds = 12; blocks = 1; return true;
        case 6: rounds = 20; blocks = 1; return true;
        default: return false;
    }
}

void Crypt4s0(std::vector<std::uint8_t>& data, std::uint32_t seed, std::uint16_t crypt,
              const std::vector<std::uint8_t>& iv) {
    int rounds = 0;
    int blocks = 0;
    if (!CryptParameters(crypt, rounds, blocks)) return;

    std::uint8_t seedBytes[4] = {
        static_cast<std::uint8_t>(seed),
        static_cast<std::uint8_t>(seed >> 8),
        static_cast<std::uint8_t>(seed >> 16),
        static_cast<std::uint8_t>(seed >> 24),
    };
    std::uint8_t key[32];
    Blake2sKeyed256(seedBytes, sizeof seedBytes, iv.data(), iv.size(), key);

    static const std::uint8_t kEmpty[1] = {0};
    std::uint32_t nonceLo = Xxh32(iv.empty() ? kEmpty : iv.data(), iv.size(), seed);
    std::uint32_t nonceHi = seed;
    std::uint32_t fallback = nonceHi ^ nonceLo;
    if (fallback == 0) fallback = seed;
    if (fallback == 0) fallback = 0xFFFFFFFFu;

    std::size_t offset = 0;
    std::uint64_t counter = 0;
    while (offset < data.size()) {
        std::uint8_t base[64];
        ChaChaBlock(key, counter++, nonceLo, nonceHi, rounds, base);
        std::vector<std::uint8_t> stream = ExpandStream(base, blocks, fallback);
        std::size_t take = std::min(stream.size(), data.size() - offset);
        for (std::size_t i = 0; i < take; i++) data[offset + i] ^= stream[i];
        offset += take;
    }
}

// ---------------------------------------------------------------------------
// TJS/4s0 framed LZ4 (u16 block sizes, 64 KiB history dictionary)

bool DecompressFrames(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out,
                      std::string& error) {
    std::size_t pos = 0;
    while (pos < size) {
        if (size - pos < 2) {
            error = "TJS/4s0 LZ4 frame has a truncated block size";
            return false;
        }
        std::size_t blockSize = Le16(data + pos);
        pos += 2;
        if (blockSize == 0) {
            for (std::size_t i = pos; i < size; i++) {
                if (data[i] != 0) {
                    error = "TJS/4s0 has non-zero data after the LZ4 terminator";
                    return false;
                }
            }
            return true;
        }
        if (blockSize > size - pos) {
            error = "TJS/4s0 LZ4 block is truncated";
            return false;
        }

        std::size_t dictStart = out.size() > kLz4DictSize ? out.size() - kLz4DictSize : 0;
        std::size_t dictSize = out.size() - dictStart;
        std::vector<std::uint8_t> decoded;
        if (!Lz4Block(data + pos, blockSize, out.data() + dictStart, dictSize, decoded, error)) {
            // lz4_flex falls back to a dictionary-less decode for writers that
            // emitted a block with a stale dictionary; do the same.
            if (dictSize == 0 || !Lz4Block(data + pos, blockSize, nullptr, 0, decoded, error)) {
                return false;
            }
        }
        if (decoded.empty()) {
            error = "TJS/4s0 LZ4 block decoded to zero bytes";
            return false;
        }
        out.insert(out.end(), decoded.begin(), decoded.end());
        pos += blockSize;
    }
    return true;
}

// ---------------------------------------------------------------------------
// type checkers

struct ByteChecker {
    std::uint32_t seed;
    explicit ByteChecker(std::uint32_t s) : seed(s) {}

    static void Round(std::uint8_t bytes[4]) {
        std::uint8_t a = static_cast<std::uint8_t>(bytes[0] ^ (bytes[0] * 2));
        std::uint8_t b = a;
        b >>= 2;
        b ^= bytes[2];
        b >>= 3;
        b ^= bytes[2];
        b ^= a;
        bytes[0] = bytes[1];
        bytes[1] = bytes[2];
        bytes[2] = b;
    }

    std::uint8_t ByteForType(std::uint8_t type) {
        std::uint8_t bytes[4] = {
            static_cast<std::uint8_t>(seed),
            static_cast<std::uint8_t>(seed >> 8),
            static_cast<std::uint8_t>(seed >> 16),
            static_cast<std::uint8_t>(seed >> 24),
        };
        if (type == 0) return bytes[2];
        Round(bytes);
        seed = Le32(bytes);
        return bytes[2];
    }

    std::uint32_t FinalCheck() const {
        std::uint8_t bytes[4] = {
            static_cast<std::uint8_t>(seed),
            static_cast<std::uint8_t>(seed >> 8),
            static_cast<std::uint8_t>(seed >> 16),
            static_cast<std::uint8_t>(seed >> 24),
        };
        Round(bytes);
        Round(bytes);
        Round(bytes);
        std::swap(bytes[0], bytes[2]);
        return Le32(bytes);
    }
};

struct FourS0Checker {
    std::uint8_t b0, b1, b2;
    explicit FourS0Checker(std::uint32_t seed)
        : b0(static_cast<std::uint8_t>((seed >> 24) ^ seed)),
          b1(static_cast<std::uint8_t>(seed >> 8)),
          b2(static_cast<std::uint8_t>(seed >> 16)) {}

    std::uint8_t ByteForType(std::uint8_t type) {
        if (type == 0) return b2;
        std::uint8_t t = static_cast<std::uint8_t>(b0 ^ (b0 << 1));
        b0 = b1;
        b1 = b2;
        b2 = static_cast<std::uint8_t>(b2 ^ (b2 >> 3) ^ t ^ (t >> 5));
        return b2;
    }
};

// ---------------------------------------------------------------------------
// value stream

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    bool Eof() const { return pos_ == size_; }
    std::size_t Remaining() const { return size_ - pos_; }

    bool Take(std::size_t length, const std::uint8_t*& out) {
        if (length > size_ - pos_) return false;
        out = data_ + pos_;
        pos_ += length;
        return true;
    }
    bool U8(std::uint8_t& out) {
        const std::uint8_t* p;
        if (!Take(1, p)) return false;
        out = p[0];
        return true;
    }
    bool U32(std::uint32_t& out) {
        const std::uint8_t* p;
        if (!Take(4, p)) return false;
        out = Le32(p);
        return true;
    }
    bool U64(std::uint64_t& out) {
        const std::uint8_t* p;
        if (!Take(8, p)) return false;
        out = Le64(p);
        return true;
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

void AppendUtf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string Utf16ToUtf8(const std::uint8_t* data, std::size_t units) {
    std::string out;
    out.reserve(units);
    for (std::size_t i = 0; i < units; i++) {
        std::uint32_t u = static_cast<std::uint32_t>(data[i * 2]) |
                          (static_cast<std::uint32_t>(data[i * 2 + 1]) << 8);
        if (u >= 0xD800 && u <= 0xDBFF && i + 1 < units) {
            std::uint32_t lo = static_cast<std::uint32_t>(data[(i + 1) * 2]) |
                               (static_cast<std::uint32_t>(data[(i + 1) * 2 + 1]) << 8);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                AppendUtf8(out, 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00));
                i++;
                continue;
            }
        }
        if (u >= 0xD800 && u <= 0xDFFF) u = 0xFFFD;
        AppendUtf8(out, u);
    }
    return out;
}

template <typename Checker>
bool ParseValue(Reader& reader, Checker& checker, Value& out, int depth, std::string& error) {
    if (depth > kMaxDepth) {
        error = "PBD value nesting exceeds the safety limit";
        return false;
    }
    std::uint8_t type = 0;
    std::uint8_t check = 0;
    if (!reader.U8(type) || !reader.U8(check)) {
        error = "PBD value stream is truncated";
        return false;
    }
    std::uint8_t expected = checker.ByteForType(type);
    if (check != expected) {
        error = "PBD type check mismatch (type " + std::to_string(type) + ", stored " +
                std::to_string(check) + ", expected " + std::to_string(expected) + ")";
        return false;
    }

    switch (type) {
        case 0:
            out.type = Value::Void;
            return true;
        case 2: {
            std::uint32_t units = 0;
            if (!reader.U32(units)) {
                error = "PBD string length is truncated";
                return false;
            }
            if (units > kMaxStringUnits) {
                error = "PBD string exceeds the safety limit";
                return false;
            }
            const std::uint8_t* bytes = nullptr;
            if (units > reader.Remaining() / 2 || !reader.Take(static_cast<std::size_t>(units) * 2, bytes)) {
                error = "PBD string is truncated";
                return false;
            }
            out.type = Value::String;
            out.string = Utf16ToUtf8(bytes, units);
            return true;
        }
        case 4: {
            std::uint64_t raw = 0;
            if (!reader.U64(raw)) {
                error = "PBD integer is truncated";
                return false;
            }
            out.type = Value::Integer;
            out.integer = static_cast<std::int64_t>(raw);
            return true;
        }
        case 5: {
            std::uint64_t raw = 0;
            if (!reader.U64(raw)) {
                error = "PBD double is truncated";
                return false;
            }
            out.type = Value::Double;
            std::memcpy(&out.real, &raw, sizeof out.real);
            return true;
        }
        case 0x81: {
            std::uint32_t count = 0;
            if (!reader.U32(count)) {
                error = "PBD array count is truncated";
                return false;
            }
            if (count > kMaxContainerItems || count > reader.Remaining() / 2 + 1) {
                error = "PBD array count exceeds the data limit";
                return false;
            }
            out.type = Value::Array;
            out.array.clear();
            out.array.reserve(std::min<std::uint32_t>(count, 65536));
            for (std::uint32_t i = 0; i < count; i++) {
                Value item;
                if (!ParseValue(reader, checker, item, depth + 1, error)) return false;
                out.array.push_back(std::move(item));
            }
            return true;
        }
        case 0xC1: {
            std::uint32_t count = 0;
            if (!reader.U32(count)) {
                error = "PBD dictionary count is truncated";
                return false;
            }
            if (count > kMaxContainerItems || count > reader.Remaining() / 2 + 1) {
                error = "PBD dictionary count exceeds the data limit";
                return false;
            }
            out.type = Value::Dictionary;
            out.dictionary.clear();
            out.dictionary.reserve(std::min<std::uint32_t>(count, 65536));
            for (std::uint32_t i = 0; i < count; i++) {
                std::uint32_t units = 0;
                if (!reader.U32(units)) {
                    error = "PBD dictionary key is truncated";
                    return false;
                }
                if (units > kMaxStringUnits || units > reader.Remaining() / 2) {
                    error = "PBD dictionary key exceeds the data limit";
                    return false;
                }
                const std::uint8_t* keyBytes = nullptr;
                if (!reader.Take(static_cast<std::size_t>(units) * 2, keyBytes)) {
                    error = "PBD dictionary key is truncated";
                    return false;
                }
                std::string key = Utf16ToUtf8(keyBytes, units);
                Value item;
                if (!ParseValue(reader, checker, item, depth + 1, error)) return false;
                out.dictionary.emplace_back(std::move(key), std::move(item));
            }
            return true;
        }
        default:
            error = "unsupported PBD value type " + std::to_string(type);
            return false;
    }
}

}  // namespace

bool Sniff(const std::uint8_t* data, std::size_t size) {
    if (!data || size < 8) return false;
    return std::memcmp(data, kMagicNs0, 8) == 0 || std::memcmp(data, kMagic4s0, 8) == 0;
}

bool Decode(const std::uint8_t* data, std::size_t size, Value& out, std::string& error) {
    if (!data || size < kHeaderSize) {
        error = "PBD header is truncated";
        return false;
    }

    bool ns0 = std::memcmp(data, kMagicNs0, 8) == 0;
    bool four = std::memcmp(data, kMagic4s0, 8) == 0;
    if (!ns0 && !four) {
        error = "PBD magic is neither TJS/ns0 nor TJS/4s0";
        return false;
    }

    std::uint32_t seed = Le32(data + 8);
    std::uint16_t crypt = Le16(data + 12);
    std::uint16_t ivLength = Le16(data + 14);

    if (ns0) {
        if (crypt != 0 || ivLength != 0) {
            error = "TJS/ns0 must have crypt=0 and iv_len=0";
            return false;
        }
        Reader reader(data + kHeaderSize, size - kHeaderSize);
        ByteChecker checker(seed);
        if (!ParseValue(reader, checker, out, 0, error)) return false;
        std::uint32_t checksum = 0;
        if (!reader.U32(checksum)) {
            error = "TJS/ns0 checksum is truncated";
            return false;
        }
        if (checksum != checker.FinalCheck()) {
            error = "TJS/ns0 checksum mismatch";
            return false;
        }
        if (!reader.Eof()) {
            error = "TJS/ns0 has trailing bytes after the checksum";
            return false;
        }
        return true;
    }

    if (crypt > 6) {
        error = "unsupported TJS/4s0 crypt field";
        return false;
    }
    if (ivLength > size - kHeaderSize) {
        error = "TJS/4s0 IV is truncated";
        return false;
    }
    std::vector<std::uint8_t> iv(data + kHeaderSize, data + kHeaderSize + ivLength);
    std::vector<std::uint8_t> framed(data + kHeaderSize + ivLength, data + size);
    if (crypt != 0) Crypt4s0(framed, seed, crypt, iv);

    std::vector<std::uint8_t> plain;
    if (!DecompressFrames(framed.data(), framed.size(), plain, error)) return false;

    Reader reader(plain.data(), plain.size());
    FourS0Checker checker(seed);
    if (!ParseValue(reader, checker, out, 0, error)) return false;
    std::uint32_t trailer = 0;
    if (!reader.U32(trailer)) {
        error = "TJS/4s0 trailer is truncated";
        return false;
    }
    if (!reader.Eof()) {
        error = "TJS/4s0 has trailing bytes after the trailer";
        return false;
    }
    return true;
}

}  // namespace pbd
}  // namespace kk4s
