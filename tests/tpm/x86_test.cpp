// Differential test of the x86 interpreter (compat/x86): the functions of
// tests/tpm/src/ops.c run natively and inside the interpreter from DLLs built
// by i686-w64-mingw32-gcc at several optimisation levels; results must agree.
//   x86_test ops-O0.dll ops-O2.dll ...
#include "x86/pe32.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>

extern "C" {
#include "../../tests/tpm/src/ops.c"
}

using namespace kk4s::x86;

int main(int argc, char** argv) {
    int failures = 0;
    for (int i = 1; i < argc; ++i) {
        std::ifstream in(argv[i], std::ios::binary);
        std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        Machine m;
        PeImage img;
        try {
            img = LoadPe32(m, file, [&](const std::string& dll, const std::string& name, uint16_t) -> uint32_t {
                return m.AddHost(dll + "!" + name, 0, Machine::Conv::Cdecl, [dll, name](Machine&) -> uint32_t {
                    throw Fault{"unexpected import " + dll + "!" + name};
                });
            });
            if (img.entry) m.Call(img.entry, {img.base, 1, 0}, 1000000);
        } catch (const Fault& f) {
            printf("FAIL %s: load: %s\n", argv[i], f.what.c_str());
            failures++;
            continue;
        }
        std::mt19937 rng(12345);
        struct F { const char* name; uint32_t (*fn)(uint32_t, uint32_t); };
        const F fns[] = {{"op_arith", op_arith}, {"op_u64", op_u64}, {"op_bytes", op_bytes}, {"op_bits", op_bits},
                         {"op_switch", op_switch}, {"op_cmp", op_cmp}};
        int bad = 0, total = 0;
        for (const F& f : fns) {
            uint32_t addr = img.Export(f.name);
            if (!addr) { printf("FAIL %s: no export %s\n", argv[i], f.name); bad++; continue; }
            for (int k = 0; k < 2000; ++k) {
                uint32_t a = rng(), b = rng();
                if (k < 64) { a = (uint32_t)(k * 0x04000000u) ^ (k & 1 ? 0xFFFFFFFFu : 0); b = (uint32_t)k - 32; }
                uint32_t want = f.fn(a, b), got;
                try { got = m.Call(addr, {a, b}, 10000000); }
                catch (const Fault& e) { printf("FAIL %s %s(0x%08x, 0x%08x): %s\n", argv[i], f.name, a, b, e.what.c_str()); bad++; break; }
                total++;
                if (got != want) {
                    if (bad < 10) printf("FAIL %s %s(0x%08x, 0x%08x) = 0x%08x, want 0x%08x\n", argv[i], f.name, a, b, got, want);
                    bad++;
                }
            }
        }
        // buffer + string functions
        uint32_t gbuf = m.Alloc(4096);
        uint32_t fbuf = img.Export("op_buf"), fstr = img.Export("op_str");
        for (int k = 0; k < 200 && fbuf && fstr; ++k) {
            uint32_t n = rng() % 3000, hash = rng(), off = rng() % 100000;
            std::vector<uint8_t> data(n);
            for (auto& c : data) c = (uint8_t)rng();
            std::vector<uint8_t> want = data;
            op_buf(want.data(), n, hash, off);
            m.mem.Write(gbuf, data.data(), n);
            try { m.Call(fbuf, {gbuf, n, hash, off}, 50000000); }
            catch (const Fault& e) { printf("FAIL %s op_buf: %s\n", argv[i], e.what.c_str()); bad++; break; }
            std::vector<uint8_t> got(n);
            m.mem.Read(gbuf, got.data(), n);
            total++;
            if (got != want) { if (bad < 10) printf("FAIL %s op_buf n=%u hash=0x%08x off=%u\n", argv[i], n, hash, off); bad++; }
            char s[32];
            snprintf(s, sizeof(s), "kk4s-%u-%u", hash, off);
            m.mem.Write(gbuf, s, strlen(s) + 1);
            uint32_t g = 0;
            try { g = m.Call(fstr, {gbuf}, 1000000); } catch (const Fault& e) { printf("FAIL op_str: %s\n", e.what.c_str()); bad++; break; }
            total++;
            if (g != op_str(s)) { bad++; printf("FAIL %s op_str(%s)\n", argv[i], s); }
        }
        printf("%s %s: %d/%d calls agree (%llu instructions)\n", bad ? "FAIL" : "PASS", argv[i], total - bad, total,
               (unsigned long long)m.Steps());
        failures += bad ? 1 : 0;
    }
    return failures ? 1 : 0;
}
