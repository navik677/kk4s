// kk4s: TinyFilter (CXDEC variant) regression test (no engine needed).
//
//   tinyfilter_test tiny_vectors.txt
//       vectors from tests/cxdec/gen_tiny_vectors.py: the keystream byte derived
//       from adlr must decrypt to the same adler32, and chunked reads with odd
//       sizes must equal a whole-buffer decryption.
//
// Build: g++ -std=c++17 -O2 -I compat tests/cxdec/tinyfilter_test.cpp \
//            compat/cxdec/tinyfilter.cpp -lz
#include "cxdec/tinyfilter.h"

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

int RunVectors(const char* path) {
    std::ifstream f(path);
    if (!f) { std::printf("cannot open %s\n", path); return 2; }
    std::string line;
    int n = 0, bad = 0;
    while (std::getline(f, line)) {
        unsigned adlr, want;
        size_t len;
        if (std::sscanf(line.c_str(), "%x %zu %x", &adlr, &len, &want) != 3) continue;
        n++;
        std::vector<uint8_t> whole(len), chunked(len);
        for (size_t i = 0; i < len; i++) whole[i] = chunked[i] = (uint8_t)i;
        kk4s::cxdec::TinyFilterDecrypt(adlr, whole.data(), whole.size());
        if ((uint32_t)adler32(1, whole.data(), (uInt)whole.size()) != want) {
            std::printf("adler mismatch at line %d (adlr %08x)\n", n, adlr);
            bad++;
            continue;
        }
        // stream reads arrive in arbitrary chunk sizes; the filter is position
        // independent, so any split must give the same bytes.
        size_t pos = 0, step = 1 + (adlr % 4093);
        while (pos < len) {
            size_t c = std::min(step, len - pos);
            kk4s::cxdec::TinyFilterDecrypt(adlr, chunked.data() + pos, c);
            pos += c;
            step = step * 3 % 9001 + 1;
        }
        if (chunked != whole) {
            std::printf("chunked decryption differs at line %d (adlr %08x)\n", n, adlr);
            bad++;
        }
    }
    // The TPM substitutes 0xd7 when the xor of the four bytes would be zero;
    // missing that turns the entry into a no-op and the whole filter fails.
    const uint32_t zero_adlr = 0x01010101u ^ 0x1548e29cu;
    if (kk4s::cxdec::TinyFilterKey(zero_adlr) != 0xd7) {
        std::printf("zero-key substitution failed\n");
        bad++;
    }
    std::printf("%d vectors, %d failed\n", n, bad);
    return bad ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2) return RunVectors(argv[1]);
    std::printf("usage: tinyfilter_test tiny_vectors.txt\n");
    return 2;
}
