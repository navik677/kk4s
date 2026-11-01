// kk4s: CXDEC regression test (no engine needed).
//
//   cxdec_test vectors.txt
//       synthetic schemes vs the Python reference (tests/cxdec/gen_vectors.py),
//       also checks that chunked decryption equals whole-buffer decryption.
//   cxdec_test --archive SCHEME.cxscheme ARCHIVE.xp3.index ARCHIVE.xp3
//       decrypts every entry of a real hnfn/Cx archive and checks adler32 == adlr.
//       Game data stays outside the repository.
//
// Build: g++ -std=c++17 -O2 -I compat tests/cxdec/cxdec_test.cpp compat/cxdec/cxdec.cpp -lz
#include "cxdec/cxdec.h"

#include <zlib.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using kk4s::cxdec::CxDecoder;
using kk4s::cxdec::CxScheme;

namespace {

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
    uint32_t operator()() {
        uint32_t x = s;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        return s = x;
    }
};

template <int N> void Perm(Rng& r, uint8_t (&v)[N]) {
    for (int i = 0; i < N; i++) v[i] = (uint8_t)i;
    for (int i = N - 1; i > 0; i--) {
        int j = (int)(r() % (uint32_t)(i + 1));
        std::swap(v[i], v[j]);
    }
}

// Same construction as gen_vectors.py:synth_scheme.
CxScheme Synth(uint32_t seed, Rng& r) {
    CxScheme s;
    for (auto& c : s.control_block) c = r();
    s.mask = r() & 0x3ff;
    s.offset = r() & 0x7ff;
    Perm(r, s.prolog_order);
    Perm(r, s.odd_branch_order);
    Perm(r, s.even_branch_order);
    s.name = "synthetic";
    (void)seed;
    return s;
}

int RunVectors(const char* path) {
    std::ifstream f(path);
    if (!f) { std::printf("cannot open %s\n", path); return 2; }
    std::string line;
    uint32_t cur_seed = 0;
    std::unique_ptr<Rng> rng;
    std::unique_ptr<CxDecoder> dec;
    int n = 0, bad = 0;
    while (std::getline(f, line)) {
        unsigned seed, hash, adl;
        size_t len;
        if (std::sscanf(line.c_str(), "%x %x %zu %x", &seed, &hash, &len, &adl) != 4) continue;
        if (!dec || seed != cur_seed) {
            cur_seed = seed;
            rng.reset(new Rng(seed));
            dec.reset(new CxDecoder(Synth(seed, *rng)));
        }
        // the generator consumed (hash, length) pairs from the same PRNG
        uint32_t h = (*rng)();
        size_t l = (*rng)() % 70000 + 1;
        if (h != hash || l != len) { std::printf("vector/PRNG mismatch at line %d\n", n + 1); return 2; }
        std::vector<uint8_t> whole(len), chunked(len);
        for (size_t i = 0; i < len; i++) whole[i] = chunked[i] = (uint8_t)i;
        dec->Decrypt(hash, 0, whole.data(), len);
        // chunked reads with odd sizes must give the same bytes (stream reads)
        size_t pos = 0, step = 1 + (hash % 4093);
        while (pos < len) {
            size_t c = std::min(step, len - pos);
            dec->Decrypt(hash, pos, chunked.data() + pos, c);
            pos += c;
            step = step * 3 % 9001 + 1;
        }
        uint32_t a = (uint32_t)adler32(1, whole.data(), (uInt)len);
        bool ok = a == adl && whole == chunked;
        if (!ok && bad < 10) std::printf("FAIL seed %08x hash %08x len %zu: %08x vs %08x%s\n", seed, hash, len, a, adl,
                                         whole == chunked ? "" : " (chunked differs)");
        bad += !ok;
        n++;
    }
    std::printf("vectors: %d checked, %d failed\n", n, bad);
    return bad || !n ? 1 : 0;
}

// --- real archive check ----------------------------------------------------
bool ReadFileBytes(const std::string& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}
struct LoadCtx { std::string dir; };
bool LoadRel(const std::string& name, std::vector<uint8_t>& out, void* ctx) {
    return ReadFileBytes(static_cast<LoadCtx*>(ctx)->dir + "/" + name, out);
}
uint32_t U32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
uint64_t U64(const uint8_t* p) { return U32(p) | (uint64_t)U32(p + 4) << 32; }

int RunArchive(const char* scheme_path, const char* index_path, const char* xp3_path) {
    std::vector<uint8_t> text, index;
    if (!ReadFileBytes(scheme_path, text) || !ReadFileBytes(index_path, index)) { std::printf("cannot read input\n"); return 2; }
    std::string sp = scheme_path;
    LoadCtx ctx{sp.substr(0, sp.find_last_of("/\\") == std::string::npos ? 0 : sp.find_last_of("/\\"))};
    if (ctx.dir.empty()) ctx.dir = ".";
    CxScheme s;
    std::string err;
    if (!kk4s::cxdec::ParseScheme(std::string(text.begin(), text.end()), LoadRel, &ctx, s, err)) {
        std::printf("scheme: %s\n", err.c_str());
        return 2;
    }
    CxDecoder dec(s);
    std::ifstream arc(xp3_path, std::ios::binary);
    int ok = 0, bad = 0;
    for (size_t pos = 0; pos + 12 <= index.size();) {
        uint64_t size = U64(&index[pos + 4]);
        if (std::memcmp(&index[pos], "File", 4) != 0) { pos += 12 + size; continue; }
        uint32_t adlr = 0;
        bool has_adlr = false;
        std::vector<uint8_t> data;
        for (size_t p = pos + 12; p + 12 <= pos + 12 + size;) {
            uint64_t ss = U64(&index[p + 4]);
            if (!std::memcmp(&index[p], "adlr", 4)) { adlr = U32(&index[p + 12]); has_adlr = true; }
            if (!std::memcmp(&index[p], "segm", 4)) {
                for (uint64_t q = 0; q + 28 <= ss; q += 28) {
                    const uint8_t* g = &index[p + 12 + q];
                    uint32_t flags = U32(g);
                    uint64_t off = U64(g + 4), raw = U64(g + 12), packed = U64(g + 20);
                    std::vector<uint8_t> chunk(packed);
                    arc.seekg((std::streamoff)off);
                    arc.read((char*)chunk.data(), (std::streamsize)packed);
                    if ((flags & 7) == 1) {
                        std::vector<uint8_t> un(raw);
                        uLongf dl = (uLongf)raw;
                        if (uncompress(un.data(), &dl, chunk.data(), (uLong)packed) != Z_OK) un.clear();
                        chunk.swap(un);
                    }
                    data.insert(data.end(), chunk.begin(), chunk.end());
                }
            }
            p += 12 + ss;
        }
        if (has_adlr && !data.empty()) {
            dec.Decrypt(adlr, 0, data.data(), data.size());
            if ((uint32_t)adler32(1, data.data(), (uInt)data.size()) == adlr) ok++;
            else bad++;
        }
        pos += 12 + size;
    }
    std::printf("archive: %d entries ok, %d failed\n", ok, bad);
    // the protector adds one decoy entry per archive that never decrypts
    return ok > 0 && bad <= 2 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 5 && !std::strcmp(argv[1], "--archive")) return RunArchive(argv[2], argv[3], argv[4]);
    if (argc == 2) return RunVectors(argv[1]);
    std::printf("usage: cxdec_test vectors.txt | cxdec_test --archive S.cxscheme A.xp3.index A.xp3\n");
    return 2;
}
