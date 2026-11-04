// kk4s: CXDEC filter discovery and per-archive binding. See kk4s_cx.h.
#include "kk4s_cx.h"

#include "cxdec/cxdec.h"
#include "cxdec/tinyfilter.h"
#include "kk4s_platform.h"

#include <zlib.h>

#include <dirent.h>

#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <set>

namespace kk4s {
namespace compat {

namespace {

bool ReadAll(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + n);
    fclose(f);
    return true;
}

struct DirCtx { std::string dir; };
bool LoadRelative(const std::string& name, std::vector<uint8_t>& out, void* ctx) {
    if (name.find("..") != std::string::npos) return false;
    return ReadAll(static_cast<DirCtx*>(ctx)->dir + "/" + name, out);
}

// A saved scheme (the Cx program interpreter).
class CxContentDecoder : public ContentDecoder {
public:
    explicit CxContentDecoder(const cxdec::CxScheme& scheme)
        : decoder_(scheme), name_("cx:" + scheme.name) {}
    void Decrypt(uint32_t hash, uint64_t offset, uint8_t* buf, size_t count) override {
        decoder_.Decrypt(hash, offset, buf, count);
    }
    const std::string& FamilyName() const override { return name_; }

private:
    cxdec::CxDecoder decoder_;
    std::string name_;
};

// The scheme-free variant: one keystream byte per entry, derived from adlr.
class TinyContentDecoder : public ContentDecoder {
public:
    void Decrypt(uint32_t hash, uint64_t, uint8_t* buf, size_t count) override {
        cxdec::TinyFilterDecrypt(hash, buf, count);
    }
    const std::string& FamilyName() const override { return Name(); }
    static const std::string& Name() {
        static const std::string name = "tinyfilter/adlr-xor";
        return name;
    }
};

struct Registry {
    std::mutex mutex;
    bool loaded = false;
    std::vector<std::unique_ptr<CxContentDecoder>> decoders;  // one per scheme file
    std::unique_ptr<TinyContentDecoder> tiny;
    std::set<std::string> auto_attempted_archives;

    void LoadOnce() {
        if (loaded) return;
        loaded = true;
        std::string dir = ProfilesDir() + "/cx";
        DIR* d = opendir(dir.c_str());
        if (!d) return;
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n.size() < 9 || n.compare(n.size() - 9, 9, ".cxscheme") != 0) continue;
            std::vector<uint8_t> text;
            if (!ReadAll(dir + "/" + n, text)) continue;
            cxdec::CxScheme s;
            std::string err;
            DirCtx ctx{dir};
            if (!cxdec::ParseScheme(std::string(text.begin(), text.end()), LoadRelative, &ctx, s, err)) {
                Log("cxdec: ignoring %s: %s", n.c_str(), err.c_str());
                continue;
            }
            s.name = n;
            decoders.emplace_back(new CxContentDecoder(s));
        }
        closedir(d);
        Log("cxdec: %u scheme file(s) in %s", (unsigned)decoders.size(), dir.c_str());
    }
};

std::atomic<bool> g_any_bound(false);

Registry& Reg() {
    static Registry r;
    return r;
}

int CountValid(ContentDecoder& dec, const std::vector<CxSample>& samples) {
    int ok = 0;
    std::vector<uint8_t> buf;
    for (const auto& s : samples) {
        buf = s.raw;
        dec.Decrypt(s.adlr, 0, buf.data(), buf.size());
        if ((uint32_t)adler32(1, buf.data(), (uInt)buf.size()) == s.adlr) ok++;
    }
    return ok;
}

// Protectors insert decoy entries that never decrypt: accept a filter that
// explains clearly most samples.
int NeedCount(size_t samples) {
    int need = (int)samples * 4 / 5;
    if (need < 3) need = samples < 3 ? (int)samples : 3;
    return need;
}

}  // namespace

ContentDecoder* BindCxDecoder(const std::string& archive_name, const std::vector<CxSample>& samples) {
    Registry& r = Reg();
    std::lock_guard<std::mutex> lock(r.mutex);
    r.LoadOnce();
    if (samples.empty()) return nullptr;
    const int need = NeedCount(samples.size());

    CxContentDecoder* best = nullptr;
    int best_ok = 0;
    for (auto& d : r.decoders) {
        int ok = CountValid(*d, samples);
        if (ok > best_ok) { best_ok = ok; best = d.get(); }
    }
    if (best && best_ok >= need) {
        Log("cxdec: %s uses scheme %s (%d/%u sample entries verified)", archive_name.c_str(),
            best->FamilyName().c_str(), best_ok, (unsigned)samples.size());
        g_any_bound = true;
        return best;
    }

    // Scheme-free variant: some TPMs only XOR every entry with one byte derived
    // from its adlr. Validate it exactly like a scheme (adler32 == adlr on the
    // archive's own entries); decoys are tolerated by the same threshold.
    if (!r.tiny) r.tiny.reset(new TinyContentDecoder);
    int tiny_ok = CountValid(*r.tiny, samples);
    if (tiny_ok >= need) {
        Log("cxdec: %s uses %s (%d/%u sample entries verified)", archive_name.c_str(),
            r.tiny->FamilyName().c_str(), tiny_ok, (unsigned)samples.size());
        g_any_bound = true;
        return r.tiny.get();
    }

#if defined(KK4S_DESKTOP)
    // PC "drop the game folder and play": if no saved profile scheme fits,
    // recover a generic CX scheme from the game's TPM/embedded internal module.
    // This is data-driven and validated against this archive's adlr samples;
    // never apply an unverified candidate and never write into the game folder.
    const std::string game_dir = GameDir();
    // Different archives in one title can use different CX schemes, and an
    // archive's sample set can be dominated by decoys. Retry recovery for each
    // archive that no existing decoder validates, but never repeat it for the
    // same archive.
    if (!game_dir.empty() && r.auto_attempted_archives.insert(archive_name).second) {
        cxdec::CxScheme recovered;
        std::string why;
        if (RecoverCxSchemeFromGame(game_dir, samples, recovered, why)) {
            std::unique_ptr<CxContentDecoder> decoder(new CxContentDecoder(recovered));
            int verified = CountValid(*decoder, samples);
            if (verified >= need) {
                CxContentDecoder* result = decoder.get();
                r.decoders.emplace_back(std::move(decoder));
                Log("cxdec: recovered scheme %s from game files (%d/%u samples verified)",
                    result->FamilyName().c_str(), verified, (unsigned)samples.size());
                g_any_bound = true;
                return result;
            }
            Log("cxdec: recovered candidate for %s failed runtime validation (%d/%u)",
                game_dir.c_str(), verified, (unsigned)samples.size());
        } else {
            Log("cxdec: automatic scheme recovery for %s: %s", game_dir.c_str(), why.c_str());
        }
    }
#endif

    Log("cxdec: %s has hashed-name records (hnfn/eliF), but no filter validates "
        "(%u schemes, best %d/%u, tinyfilter %d/%u). "
        "Create a scheme with tools/cx_scheme_extract.py <game folder> %s/cx/<name>.cxscheme",
        archive_name.c_str(), (unsigned)r.decoders.size(), best_ok, (unsigned)samples.size(),
        tiny_ok, (unsigned)samples.size(), ProfilesDir().c_str());
    return nullptr;
}

bool AnyCxArchiveBound() { return g_any_bound; }

}  // namespace compat
}  // namespace kk4s
