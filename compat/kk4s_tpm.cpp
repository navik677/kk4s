// kk4s: a game's own XP3 filter plugin run in the x86 interpreter. See kk4s_tpm.h.
#include "kk4s_tpm.h"

#include "kk4s_platform.h"
#include "x86/tpm_plugin.h"

#include <dirent.h>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace kk4s {
namespace compat {

namespace {

constexpr uint32_t kKeyLen = 4096;  // keystream prefix computed per file (XOR filters)

uint32_t Adler(const uint8_t* p, size_t n) { return (uint32_t)adler32(1, p, (uInt)n); }

std::vector<uint8_t> ReadAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

struct Loaded {
    std::string name;
    std::unique_ptr<x86::TpmPlugin> plugin;
    std::mutex mutex;  // the interpreter is single-threaded
};

class TpmDecoder : public ContentDecoder {
public:
    TpmDecoder(Loaded* l, bool xor_model) : l_(l), xor_(xor_model), family_("tpm:" + l->name + (xor_model ? " (XOR keystream)" : " (interpreted)")) {}

    void Decrypt(uint32_t hash, uint64_t offset, uint8_t* buf, size_t count) override {
        if (xor_) {
            std::shared_ptr<const Key> k = KeyFor(hash);
            if (k && k->period) {
                for (size_t i = 0; i < count; ++i) {
                    uint64_t pos = offset + i;
                    buf[i] ^= pos < kKeyLen ? k->bytes[pos] : k->bytes[kKeyLen / 2 + (size_t)((pos - kKeyLen / 2) % k->period)];
                }
                return;
            }
        }
        std::string err;
        std::lock_guard<std::mutex> lock(l_->mutex);
        if (!l_->plugin->Filter(hash, offset, buf, count, err)) Report(err);
    }
    const std::string& FamilyName() const override { return family_; }

private:
    struct Key {
        std::vector<uint8_t> bytes;
        uint32_t period = 0;  // 0: this file's keystream does not repeat, interpret it
    };

    std::shared_ptr<const Key> KeyFor(uint32_t hash) {
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            auto it = cache_.find(hash);
            if (it != cache_.end()) return it->second;
        }
        auto k = std::make_shared<Key>();
        std::string err;
        {
            std::lock_guard<std::mutex> lock(l_->mutex);
            if (!x86::Keystream(*l_->plugin, hash, kKeyLen, k->bytes, err)) { Report(err); return nullptr; }
        }
        k->period = x86::PeriodOf(k->bytes);
        std::lock_guard<std::mutex> lock(cache_mutex_);
        if (cache_.size() >= 2048) cache_.clear();  // 8 MiB at most
        cache_[hash] = k;
        return k;
    }

    void Report(const std::string& err) {
        std::lock_guard<std::mutex> lock(report_mutex_);
        if (reported_++ < 3) Log("tpm: %s: the filter failed: %s", l_->name.c_str(), err.c_str());
    }

    Loaded* l_;
    bool xor_;
    std::string family_;
    std::mutex cache_mutex_, report_mutex_;
    std::unordered_map<uint32_t, std::shared_ptr<const Key>> cache_;
    int reported_ = 0;
};

std::mutex g_mutex;
std::string g_scanned_dir;
bool g_scanned = false;
std::vector<std::unique_ptr<Loaded>> g_plugins;
std::vector<std::unique_ptr<ContentDecoder>> g_decoders;

void ScanDir(const std::string& dir, std::vector<std::string>& out) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name, lower = n;
        for (char& c : lower) c = (char)tolower((unsigned char)c);
        if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".tpm") == 0) out.push_back(dir + n);
    }
    closedir(d);
}

void LoadGamePlugins() {
    std::string dir = GameDir();
    if (g_scanned && dir == g_scanned_dir) return;
    g_scanned = true;
    g_scanned_dir = dir;
    g_plugins.clear();
    if (dir.empty()) return;
    if (dir.back() != '/') dir += '/';
    std::vector<std::string> files;
    ScanDir(dir, files);
    ScanDir(dir + "plugin/", files);
    std::sort(files.begin(), files.end());
    for (const std::string& f : files) {
        std::string name = f.substr(f.rfind('/') + 1);
        std::unique_ptr<Loaded> l(new Loaded);
        l->name = name;
        l->plugin.reset(new x86::TpmPlugin);
        std::string err;
        if (!l->plugin->Load(ReadAll(f), err)) {
            Log("tpm: %s: %s", name.c_str(), err.c_str());
            continue;
        }
        std::string notes;
        for (const std::string& n : l->plugin->Notes()) notes += (notes.empty() ? "" : "; ") + n;
        Log("tpm: %s sets an XP3 filter (%s)", name.c_str(), notes.c_str());
        g_plugins.push_back(std::move(l));
    }
}

}  // namespace

ContentDecoder* BindTpmFilter(const std::string& archive_name, const std::vector<CxSample>& all) {
    std::vector<const CxSample*> samples;
    for (const auto& s : all)
        if (s.adlr != 0 && !s.raw.empty()) samples.push_back(&s);
    if (samples.size() < 2) return nullptr;
    int need = (int)samples.size() * 4 / 5;
    if (need < 3) need = samples.size() < 3 ? (int)samples.size() : 3;

    std::lock_guard<std::mutex> lock(g_mutex);
    LoadGamePlugins();
    for (auto& l : g_plugins) {
        std::vector<std::pair<uint32_t, std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>> verified;
        std::string err;
        int ok = 0;
        {
            std::lock_guard<std::mutex> plock(l->mutex);
            for (const CxSample* s : samples) {
                std::vector<uint8_t> dec = s->raw;
                if (!l->plugin->Filter(s->adlr, 0, dec.data(), dec.size(), err)) break;
                if (Adler(dec.data(), dec.size()) == s->adlr) {
                    ok++;
                    if (verified.size() < 4 && dec.size() <= 16384) verified.push_back({s->adlr, {s->raw, dec}});
                }
            }
        }
        if (!err.empty()) {
            Log("tpm: %s: the filter failed on %s: %s", l->name.c_str(), archive_name.c_str(), err.c_str());
            continue;
        }
        if (ok < need) {
            Log("tpm: %s does not decrypt %s (%d/%u samples match their adlr)", l->name.c_str(), archive_name.c_str(), ok,
                (unsigned)samples.size());
            continue;
        }
        x86::XorModel model;
        std::string why;
        bool fast;
        {
            std::lock_guard<std::mutex> plock(l->mutex);
            fast = !verified.empty() && x86::DetectXorModel(*l->plugin, verified, model, why);
        }
        g_decoders.emplace_back(new TpmDecoder(l.get(), fast));
        if (fast)
            Log("tpm: %s uses %s (%d/%u samples verified by adlr; XOR keystream, decrypted natively)",
                archive_name.c_str(), l->name.c_str(), ok, (unsigned)samples.size());
        else
            Log("tpm: %s uses %s (%d/%u samples verified by adlr; %s: every read runs in the x86 interpreter, slow)",
                archive_name.c_str(), l->name.c_str(), ok, (unsigned)samples.size(), why.c_str());
        return g_decoders.back().get();
    }
    return nullptr;
}

bool RunTpmFilterOnce(const std::string& tpm_path, uint32_t hash, uint64_t offset, std::vector<uint8_t>& buf,
                      std::string& error) {
    x86::TpmPlugin p;
    if (!p.Load(ReadAll(tpm_path), error)) return false;
    return p.Filter(hash, offset, buf.data(), buf.size(), error);
}

}  // namespace compat
}  // namespace kk4s
