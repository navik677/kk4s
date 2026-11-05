// The .tpm loader (compat/x86/tpm_plugin) on synthetic filter plugins built
// from tests/tpm/src by i686-w64-mingw32-gcc: each filter must give the same
// bytes as a native copy of its algorithm for random buffers, offsets and
// chunkings, and the XOR model must be found exactly for the XOR filters.
//   tpm_test <dir with filter_xor.tpm filter_table.tpm filter_add.tpm plugin_nofilter.tpm>
#include "x86/tpm_plugin.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>

using namespace kk4s::x86;

namespace {

std::vector<uint8_t> ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void NativeXor(uint32_t h, uint64_t, uint8_t* b, size_t n) {
    uint8_t key = (uint8_t)(h >> 8) ^ 0x5A;
    for (size_t i = 0; i < n; ++i) b[i] ^= key;
}
void NativeTable(uint32_t h, uint64_t off, uint8_t* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        uint64_t o = off + i;
        b[i] ^= o < 0x80 ? (uint8_t)((uint8_t)h ^ 0xA5) : (uint8_t)((uint8_t)((o & 255) * 37 + 11) ^ (uint8_t)h);
    }
}
void NativeAdd(uint32_t h, uint64_t off, uint8_t* b, size_t n) {
    for (size_t i = 0; i < n; ++i) b[i] = (uint8_t)(b[i] + (uint8_t)(h ^ (uint32_t)(off + i)));
}

int Check(const std::string& path, void (*native)(uint32_t, uint64_t, uint8_t*, size_t), bool want_xor) {
    TpmPlugin p;
    std::string err;
    if (!p.Load(ReadFile(path), err)) { printf("FAIL %s: load: %s\n", path.c_str(), err.c_str()); return 1; }
    std::mt19937 rng(7);
    std::vector<std::pair<uint32_t, std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>> samples;
    for (int k = 0; k < 60; ++k) {
        size_t n = rng() % 20000;
        uint32_t h = rng();
        uint64_t off = k % 3 == 0 ? 0 : rng() % 1000000;
        std::vector<uint8_t> data(n);
        for (auto& c : data) c = (uint8_t)rng();
        std::vector<uint8_t> want = data, got = data;
        native(h, off, want.data(), n);
        // Kirikiri reads in pieces: split the call
        size_t cut = n ? rng() % n : 0;
        if (!p.Filter(h, off, got.data(), cut, err) || !p.Filter(h, off + cut, got.data() + cut, n - cut, err)) {
            printf("FAIL %s: filter: %s\n", path.c_str(), err.c_str());
            return 1;
        }
        if (got != want) { printf("FAIL %s: bytes differ (n=%zu hash=0x%08x off=%llu)\n", path.c_str(), n, h, (unsigned long long)off); return 1; }
        if (off == 0 && samples.size() < 4 && n < 8192) samples.push_back({h, {data, want}});
    }
    XorModel model;
    std::string why;
    bool is_xor = DetectXorModel(p, samples, model, why);
    if (is_xor != want_xor) {
        printf("FAIL %s: XOR model %s (%s)\n", path.c_str(), is_xor ? "found" : "not found", why.c_str());
        return 1;
    }
    printf("PASS %s: 60 random buffers agree, XOR model %s%s%s (%llu instructions)\n", path.c_str(),
           is_xor ? "found" : "rejected", why.empty() ? "" : ": ", why.c_str(), (unsigned long long)p.Steps());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) { fprintf(stderr, "usage: tpm_test <dir>\n"); return 2; }
    std::string dir = argv[1];
    int failures = 0;
    failures += Check(dir + "/filter_xor.tpm", NativeXor, true);
    failures += Check(dir + "/filter_table.tpm", NativeTable, true);
    failures += Check(dir + "/filter_add.tpm", NativeAdd, false);
    {
        TpmPlugin p;
        std::string err;
        bool ok = p.Load(ReadFile(dir + "/plugin_nofilter.tpm"), err);
        if (ok || err.find("did not set") == std::string::npos) { printf("FAIL plugin_nofilter: %s\n", ok ? "loaded as a filter" : err.c_str()); failures++; }
        else printf("PASS plugin_nofilter.tpm: %s\n", err.c_str());
    }
    return failures ? 1 : 0;
}
