// kk4s: simple XP3 content encryption discovery. See kk4s_xp3crypt.h.
//
// Two kinds of candidates:
//  - fixed keyless families (functions of the entry's adlr hash and offset),
//    each validated directly: adler32(decrypt(raw)) == adlr;
//  - "one XOR byte per entry": for each sample the byte is recovered by trying
//    all 256 values against adlr, then a formula k = f(hash) is fitted that
//    explains the recovered bytes (a constant; ((hash >> s) + a) ^ c; or
//    (hash >> s1) ^ (hash >> s2) ^ c). The fitted formula decrypts every entry,
//    including the ones that were not sampled.
// Family definitions follow GARbro (ArcFormats/KiriKiri/CryptAlgorithms.cs, MIT).
#include "kk4s_xp3crypt.h"

#include "cxdec/tinyfilter.h"
#include "kk4s_platform.h"

#include <zlib.h>

#include <map>
#include <memory>
#include <mutex>

namespace kk4s {
namespace compat {

namespace {

using Fn = void (*)(uint32_t h, uint64_t off, uint8_t* b, size_t n);

inline uint8_t RotR(uint8_t v, int s) { s &= 7; return s ? (uint8_t)((v >> s) | (v << (8 - s))) : v; }
inline uint8_t RotL(uint8_t v, int s) { s &= 7; return s ? (uint8_t)((v << s) | (v >> (8 - s))) : v; }

// ---- fixed keyless families (decrypt direction)

void FlyingShine(uint32_t h, uint64_t, uint8_t* b, size_t n) {
    int shift = (int)(h & 0xff);
    if (!shift) shift = 0x0f;
    uint8_t key = (uint8_t)(h >> 8);
    if (!key) key = 0xf0;
    for (size_t i = 0; i < n; ++i) b[i] = RotR((uint8_t)(b[i] ^ key), shift);
}

void Seiten(uint32_t h, uint64_t off, uint8_t* b, size_t n) {
    for (size_t i = 0; i < n; ++i, ++off) {
        uint32_t key = h ^ (uint32_t)off;
        uint8_t v = b[i];
        if (key & 2) {
            int sh = (int)(key & 0x18);
            uint32_t ebx = key >> sh;
            sh &= 8;
            v ^= (uint8_t)(ebx | (key >> sh));
        }
        if (key & 4) v += (uint8_t)key;
        if (key & 8) v -= (uint8_t)(key >> (int)(key & 0x10));
        b[i] = v;
    }
}

void Okiba(uint32_t h, uint64_t off, uint8_t* b, size_t n) {
    size_t i = 0;
    if (off < 0x65) {
        uint8_t key = (uint8_t)(h >> 4);
        size_t limit = n < (size_t)(0x65 - off) ? n : (size_t)(0x65 - off);
        for (; i < limit; ++i, ++off) b[i] ^= key;
    }
    if (i < n) {
        off -= 0x65;
        uint32_t key = ((h & 0xff0000) << 8) | ((h & 0xff000000) >> 8) | ((h & 0xff00) >> 8) | ((h & 0xff) << 8);
        for (; i < n; ++i, ++off) b[i] ^= (uint8_t)(key >> (8 * ((int)off & 3)));
    }
}

void Dieselmine(uint32_t h, uint64_t off, uint8_t* b, size_t n) {
    int key = (uint8_t)h;
    for (size_t i = 0; i < n && off <= 0xffffffffULL; ++i, ++off) {
        if (off < 123) b[i] ^= (uint8_t)(21 * key);
        else if (off < 246) b[i] += (uint8_t)(-32 * key);
        else if (off < 369) b[i] ^= (uint8_t)(43 * key);
        else b[i] += (uint8_t)(-54 * key);
    }
}

void Damegane(uint32_t h, uint64_t off, uint8_t* b, size_t n) {
    for (size_t i = 0; i < n; ++i, ++off) b[i] ^= (off & 1) ? (uint8_t)h : (uint8_t)off;
}

void Nephrite(uint32_t h, uint64_t off, uint8_t* b, size_t n) {
    for (size_t i = 0; i < n; ++i, ++off) b[i] ^= (off & 1) ? (uint8_t)off : (uint8_t)h;
}

void Exa(uint32_t h, uint64_t off, uint8_t* b, size_t n) {
    int shift = (int)(off % 5);
    for (size_t i = 0; i < n; ++i) b[i] ^= (uint8_t)(h >> ((shift + (int)(i % 5)) % 5));
}

void HighRunning(uint32_t h, uint64_t off, uint8_t* b, size_t n) {
    uint8_t key = (uint8_t)h;
    if (!key) return;
    for (size_t i = 0; i < n; ++i)
        if ((off + i) % key != 0) b[i] ^= key;
}

void Hibiki(uint32_t h, uint64_t off, uint8_t* b, size_t n) {
    uint8_t k1 = (uint8_t)(h >> 5), k2 = (uint8_t)(h >> 8);
    for (size_t i = 0; i < n; ++i, ++off) b[i] ^= ((off & 4) || off <= 0x64) ? k1 : k2;
}

void PinPoint(uint32_t, uint64_t, uint8_t* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        uint8_t v = b[i];
        int bits = __builtin_popcount(v);
        if (bits) b[i] = RotL(v, bits);
    }
}

void PuCa(uint32_t h, uint64_t off, uint8_t* b, size_t n) {
    uint8_t table[32];
    uint32_t hash = h;
    for (int k = 0; k < 32; k += 4) {
        hash = (hash & 1) ? (hash | 0x80000000u) : (hash & 0x7fffffffu);
        table[k] = (uint8_t)hash; table[k + 1] = (uint8_t)(hash >> 8);
        table[k + 2] = (uint8_t)(hash >> 16); table[k + 3] = (uint8_t)(hash >> 24);
        hash >>= 1;
    }
    for (size_t i = 0; i < n; ++i) {
        size_t idx = (size_t)((off + i) & 0x3ff);
        b[i] ^= (uint8_t)(~table[idx / 32] ^ table[idx % 32]);
    }
}

void TinyFilter(uint32_t h, uint64_t, uint8_t* b, size_t n) { cxdec::TinyFilterDecrypt(h, b, n); }

struct Family { const char* name; Fn fn; };
const Family kFamilies[] = {
    {"flyingshine", FlyingShine}, {"seiten", Seiten}, {"okiba", Okiba}, {"dieselmine", Dieselmine},
    {"damegane", Damegane}, {"nephrite", Nephrite}, {"exa", Exa}, {"highrunning", HighRunning},
    {"hibiki", Hibiki}, {"pinpoint", PinPoint}, {"puca", PuCa}, {"tinyfilter/adlr-xor", TinyFilter},
};

class FamilyDecoder : public ContentDecoder {
public:
    FamilyDecoder(Fn fn, std::string name) : fn_(fn), name_(std::move(name)) {}
    void Decrypt(uint32_t hash, uint64_t offset, uint8_t* buf, size_t count) override { fn_(hash, offset, buf, count); }
    const std::string& FamilyName() const override { return name_; }
private:
    Fn fn_;
    std::string name_;
};

// ---- one XOR byte per entry, k = f(hash)

struct XorModel {
    enum Kind { Constant, ShiftAdd, TwoShift } kind = Constant;
    int skip = 0;            // bytes at the start of an entry left as they are
    int s = 0, a = 0, s2 = 0;
    uint8_t c = 0;
    uint8_t Key(uint32_t h) const {
        switch (kind) {
        case Constant: return c;
        case ShiftAdd: return (uint8_t)((((h >> s) & 0xff) + a) ^ c);
        default: return (uint8_t)(((h >> s) ^ (h >> s2)) ^ c);
        }
    }
    std::string Describe() const {
        char buf[96];
        switch (kind) {
        case Constant: snprintf(buf, sizeof(buf), "xor/const 0x%02x", c); break;
        case ShiftAdd: snprintf(buf, sizeof(buf), "xor/((adlr>>%d)+%d)^0x%02x", s, a, c); break;
        default: snprintf(buf, sizeof(buf), "xor/(adlr>>%d)^(adlr>>%d)^0x%02x", s, s2, c); break;
        }
        std::string out = buf;
        if (skip) out += "/skip" + std::to_string(skip);
        return out;
    }
};

class XorModelDecoder : public ContentDecoder {
public:
    explicit XorModelDecoder(const XorModel& m) : m_(m), name_(m.Describe()) {}
    void Decrypt(uint32_t hash, uint64_t offset, uint8_t* buf, size_t count) override {
        uint8_t k = m_.Key(hash);
        for (size_t i = 0; i < count; ++i)
            if (offset + i >= (uint64_t)m_.skip) buf[i] ^= k;
    }
    const std::string& FamilyName() const override { return name_; }
private:
    XorModel m_;
    std::string name_;
};

uint32_t Adler(const uint8_t* p, size_t n) { return (uint32_t)adler32(1, p, (uInt)n); }

int CountValidFn(Fn fn, const std::vector<CxSample>& samples) {
    int ok = 0;
    std::vector<uint8_t> buf;
    for (const auto& s : samples) {
        buf = s.raw;
        fn(s.adlr, 0, buf.data(), buf.size());
        if (Adler(buf.data(), buf.size()) == s.adlr) ok++;
    }
    return ok;
}

int NeedCount(size_t samples) {
    int need = (int)samples * 4 / 5;
    if (need < 3) need = samples < 3 ? (int)samples : 3;
    return need;
}

// The XOR byte of one sample (bytes from `skip` on), or -1.
int RecoverXorByte(const CxSample& s, int skip) {
    if ((int)s.raw.size() <= skip) return -1;
    std::vector<uint8_t> buf(s.raw.size());
    for (int k = 0; k < 256; ++k) {
        for (size_t i = 0; i < buf.size(); ++i) buf[i] = (int)i < skip ? s.raw[i] : (uint8_t)(s.raw[i] ^ k);
        if (Adler(buf.data(), buf.size()) == s.adlr) return k;
    }
    return -1;
}

// Fit k = f(hash) to (hash, k) pairs: the most common c for each shape, accepted
// when it explains at least `need` pairs. Simplest shapes first.
bool FitModel(const std::vector<std::pair<uint32_t, uint8_t>>& pairs, int need, int skip, XorModel& out) {
    auto try_shape = [&](XorModel m, auto&& base) -> bool {
        std::map<uint8_t, int> votes;
        for (const auto& p : pairs) votes[(uint8_t)(p.second ^ base(p.first))]++;
        for (const auto& v : votes) {
            if (v.second >= need) {
                m.c = v.first;
                m.skip = skip;
                out = m;
                return true;
            }
        }
        return false;
    };
    XorModel m;
    m.kind = XorModel::Constant;
    if (try_shape(m, [](uint32_t) { return (uint8_t)0; })) return true;
    m.kind = XorModel::ShiftAdd;
    for (int s = 0; s <= 24; ++s)
        for (int a = 0; a <= 1; ++a) {
            m.s = s; m.a = a;
            if (try_shape(m, [s, a](uint32_t h) { return (uint8_t)(((h >> s) & 0xff) + a); })) return true;
        }
    m.kind = XorModel::TwoShift;
    for (int s1 = 0; s1 < 24; ++s1)
        for (int s2 = s1 + 1; s2 <= 24; ++s2) {
            m.s = s1; m.s2 = s2;
            if (try_shape(m, [s1, s2](uint32_t h) { return (uint8_t)((h >> s1) ^ (h >> s2)); })) return true;
        }
    return false;
}

std::mutex g_mutex;
std::vector<std::unique_ptr<ContentDecoder>>& Decoders() {
    static std::vector<std::unique_ptr<ContentDecoder>> d;
    return d;
}

}  // namespace

ContentDecoder* BindSimpleCrypt(const std::string& archive_name, const std::vector<CxSample>& all) {
    std::vector<CxSample> samples;
    for (const auto& s : all)
        if (s.adlr != 0 && !s.raw.empty()) samples.push_back(s);
    if (samples.size() < 2) return nullptr;  // nothing to verify against
    const int need = NeedCount(samples.size());
    int plain = 0;
    for (const auto& s : samples)
        if (Adler(s.raw.data(), s.raw.size()) == s.adlr) plain++;
    if (plain >= need) return nullptr;

    std::lock_guard<std::mutex> lock(g_mutex);
    for (const Family& f : kFamilies) {
        int ok = CountValidFn(f.fn, samples);
        if (ok >= need) {
            Decoders().emplace_back(new FamilyDecoder(f.fn, f.name));
            Log("xp3crypt: %s uses %s (%d/%u sample entries verified)", archive_name.c_str(), f.name, ok,
                (unsigned)samples.size());
            return Decoders().back().get();
        }
    }
    for (int skip : {0, 5}) {
        std::vector<std::pair<uint32_t, uint8_t>> pairs;
        for (const auto& s : samples) {
            int k = RecoverXorByte(s, skip);
            if (k >= 0) pairs.emplace_back(s.adlr, (uint8_t)k);
        }
        if ((int)pairs.size() < need) continue;
        XorModel m;
        if (FitModel(pairs, need, skip, m)) {
            Decoders().emplace_back(new XorModelDecoder(m));
            Log("xp3crypt: %s uses %s (%u/%u sample entries recovered and fitted)", archive_name.c_str(),
                Decoders().back()->FamilyName().c_str(), (unsigned)pairs.size(), (unsigned)samples.size());
            return Decoders().back().get();
        }
        Log("xp3crypt: %s: every sample is XOR-ed with one byte, but no formula of adlr explains them "
            "(%u/%u recovered)", archive_name.c_str(), (unsigned)pairs.size(), (unsigned)samples.size());
    }
    Log("xp3crypt: %s looks encrypted (%d/%u samples match their adlr as plain data) and no simple "
        "scheme fits; it needs an xp3filter.tjs (Kirikiroid2) or a CX scheme", archive_name.c_str(), plain,
        (unsigned)samples.size());
    return nullptr;
}

}  // namespace compat
}  // namespace kk4s
