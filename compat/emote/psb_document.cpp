// kk4s E-mote runtime: PSB core. See psb_document.h.
#include "emote/psb_document.h"

#include "emote/cipher_profile.h"
#include "psb/psb.h"

#include <zlib.h>

#include <cstdio>
#include <cstring>

namespace kk4s {
namespace emote {

namespace {

const size_t kMaxInflated = 512u << 20;  // same sanity limit as compat/psb

uint32_t U32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Builds a Value tree from psb::Reader callbacks.
class Builder : public psb::Visitor {
public:
    explicit Builder(std::vector<ResourceSpan>& res) : res_(res) {}
    Value root;

    void Undefined() override { Put(Value()); }
    void Null() override { Put(Value::MakeNull()); }
    void Bool(bool v) override { Put(Value::MakeBool(v)); }
    void Int(int64_t v) override { Put(Value::MakeInt(v)); }
    void Double(double v) override { Put(Value::MakeDouble(v)); }
    void String(const std::string& s) override { Put(Value::MakeString(s)); }
    void Resource(uint32_t index, const uint8_t* data, size_t size) override {
        if (index >= res_.size()) res_.resize((size_t)index + 1);
        res_[index].data = data;
        res_[index].size = size;
        Put(Value::MakeResource(index));
    }
    void Extra(uint32_t index) override { Put(Value::MakeExtra(index)); }
    void BeginObject(size_t count) override {
        Value* v = Put(Value::MakeObject());
        v->members().reserve(count);
        stack_.push_back(v);
    }
    void Key(const std::string& name) override { key_ = name; }
    void EndObject() override { stack_.pop_back(); }
    void BeginArray(size_t count) override {
        Value* v = Put(Value::MakeArray());
        v->items().reserve(count);
        stack_.push_back(v);
    }
    void EndArray() override { stack_.pop_back(); }

private:
    // Pointers on the stack stay valid: a container only grows while it is the
    // innermost open one, and none of its children is open at that time.
    Value* Put(Value v) {
        if (stack_.empty()) {
            root = std::move(v);
            return &root;
        }
        Value* top = stack_.back();
        if (top->is_array()) {
            top->items().push_back(std::move(v));
            return &top->items().back();
        }
        top->members().emplace_back(key_, std::move(v));
        return &top->members().back().second;
    }

    std::vector<ResourceSpan>& res_;
    std::vector<Value*> stack_;
    std::string key_;
};

std::string Lower(const std::string& s) {
    std::string o = s;
    for (char& c : o)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return o;
}

bool HasBinaryEvidence(const CipherProfile& p, const LoadOptions& opt) {
    for (const std::string& have : opt.binary_sha256)
        for (const std::string& known : p.info().evidence.binary_sha256)
            if (Lower(have) == Lower(known)) return true;
    return false;
}

}  // namespace

Status ParsePsbHeader(const uint8_t* data, size_t size, PsbHeader& h) {
    if (!data || size < 4 || std::memcmp(data, "PSB\0", 4) != 0)
        return Status::Fail(Error::NotPsb, "no PSB magic");
    if (size < kPsbFixedHeaderSize) return Status::Fail(Error::Truncated, "shorter than the PSB header");
    h.version = (uint16_t)(data[4] | (data[5] << 8));
    h.encrypt_flag = (uint16_t)(data[6] | (data[7] << 8));
    h.header_len = U32(data + 0x08);
    h.off_names = U32(data + 0x0C);
    h.off_strings = U32(data + 0x10);
    h.off_strdata = U32(data + 0x14);
    h.off_chunk_offsets = U32(data + 0x18);
    h.off_chunk_lengths = U32(data + 0x1C);
    h.off_chunk_data = U32(data + 0x20);
    h.off_entries = U32(data + 0x24);
    return Status::Ok();
}

VersionSupport ClassifyPsbVersion(uint16_t v) {
    if (v == 2) return VersionSupport::Verified;
    if (v == 3 || v == 4) return VersionSupport::ParsedUnverified;
    return VersionSupport::Unknown;
}

const char* VersionSupportName(VersionSupport s) {
    switch (s) {
    case VersionSupport::Verified: return "verified";
    case VersionSupport::ParsedUnverified: return "parsed-unverified";
    case VersionSupport::Unknown: return "unknown";
    }
    return "?";
}

const char* ConfidenceName(Confidence c) {
    switch (c) {
    case Confidence::None: return "none";
    case Confidence::PlainContainer: return "plain-container";
    case Confidence::StructureOnly: return "structure-only";
    case Confidence::BinaryEvidence: return "binary-evidence";
    case Confidence::Pinned: return "pinned";
    }
    return "?";
}

Status ParsePlainContainer(const uint8_t* data, size_t size, std::unique_ptr<psb::Reader>& reader,
                           Value& root, std::vector<ResourceSpan>& resources) {
    std::unique_ptr<psb::Reader> r(new psb::Reader());
    if (!r->Open(data, size)) return Status::Fail(Error::CorruptStructure, "container: " + r->error());
    std::vector<ResourceSpan> res;
    Builder b(res);
    if (!r->Walk(b)) return Status::Fail(Error::CorruptStructure, "value tree: " + r->error());
    if (!b.root.is_object()) return Status::Fail(Error::CorruptStructure, "root value is not an object");
    reader = std::move(r);
    root = std::move(b.root);
    resources = std::move(res);
    return Status::Ok();
}

Document::Document() {}
Document::~Document() {}
Document::Document(Document&&) noexcept = default;
Document& Document::operator=(Document&&) noexcept = default;

bool Document::Resource(uint32_t index, ResourceSpan& out) const {
    if (index >= resources_.size() || !resources_[index].data) return false;
    out = resources_[index];
    return true;
}

Status Document::Load(const uint8_t* data, size_t size, const LoadOptions& opt) {
    *this = Document();
    if (!data && size) return Status::Fail(Error::InvalidArgument, "null data");

    std::vector<uint8_t> buf;
    bool mdf = false;
    if (size >= 8 && std::memcmp(data, "mdf\0", 4) == 0) {
        uLongf raw = (uLongf)U32(data + 4);
        if (raw > kMaxInflated) return Status::Fail(Error::CorruptStructure, "mdf: declared size too large");
        buf.assign((size_t)raw, 0);
        uLongf got = raw;
        if (uncompress(buf.data(), &got, data + 8, (uLong)(size - 8)) != Z_OK || got != raw)
            return Status::Fail(Error::CorruptStructure, "mdf: zlib stream is corrupt");
        mdf = true;
    } else {
        buf.assign(data, data + size);
    }

    PsbHeader h;
    Status st = ParsePsbHeader(buf.data(), buf.size(), h);
    if (!st.ok()) return st;
    char msg[200];
    VersionSupport vs = ClassifyPsbVersion(h.version);
    if (vs == VersionSupport::Unknown || (vs == VersionSupport::ParsedUnverified && !opt.allow_unverified_versions)) {
        std::snprintf(msg, sizeof msg, "PSB container version %u (%s)", (unsigned)h.version, VersionSupportName(vs));
        return Status::Fail(Error::UnsupportedVersion, msg);
    }
    if (h.encrypt_flag != 0) {
        std::snprintf(msg, sizeof msg, "header encrypt field is %u; no known scheme uses it", (unsigned)h.encrypt_flag);
        return Status::Fail(Error::UnsupportedLayout, msg);
    }

    const CipherRegistry& reg = opt.registry ? *opt.registry : CipherRegistry::Builtin();
    CipherSelection sel;

    struct Result {
        const CipherProfile* profile = nullptr;
        Confidence confidence = Confidence::None;
        std::unique_ptr<psb::Reader> reader;
        Value root;
        std::vector<ResourceSpan> resources;
        uint32_t adler = 0;
    };
    std::vector<Result> accepted;

    auto attempt = [&](const CipherProfile* p, Confidence conf) -> Status {
        Result r;
        r.profile = p;
        r.confidence = conf;
        std::vector<uint8_t> plain = buf;
        if (p) {
            Status pre = p->CheckPreconditions(h, plain.size());
            if (!pre.ok()) return pre;
            p->Apply(plain.data(), plain.size(), h);
        }
        r.adler = (uint32_t)adler32(adler32(0L, Z_NULL, 0), plain.data(), (uInt)plain.size());
        Status ps = ParsePlainContainer(plain.data(), plain.size(), r.reader, r.root, r.resources);
        if (!ps.ok()) return ps;
        accepted.push_back(std::move(r));
        return Status::Ok();
    };

    if (!opt.pinned_profile.empty()) {
        const CipherProfile* p = reg.Find(opt.pinned_profile);
        if (!p) return Status::Fail(Error::UnknownCipherProfile, "pinned profile '" + opt.pinned_profile + "' is not registered");
        sel.tried.push_back(p->info().id);
        Status a = attempt(p, Confidence::Pinned);
        if (!a.ok()) return Status::Fail(Error::ProfileMismatch, p->info().id + ": " + a.message);
    } else {
        Status plain = attempt(nullptr, Confidence::PlainContainer);
        if (!plain.ok()) {
            sel.reasons.push_back("plain: " + plain.message);
            // Profiles with matching binary evidence first, then (optionally) the rest.
            for (int pass = 0; pass < 2; pass++) {
                for (const auto& up : reg.profiles()) {
                    const CipherProfile* p = up.get();
                    bool ev = HasBinaryEvidence(*p, opt);
                    if ((pass == 0) != ev) continue;
                    if (!ev && !opt.allow_structural_detection) {
                        sel.reasons.push_back(p->info().id + ": skipped (no binary evidence, structural detection off)");
                        continue;
                    }
                    sel.tried.push_back(p->info().id);
                    Status a = attempt(p, ev ? Confidence::BinaryEvidence : Confidence::StructureOnly);
                    sel.reasons.push_back(p->info().id + ": " + (a.ok() ? std::string("validates") : a.message));
                }
            }
        }
    }

    if (accepted.empty()) {
        std::string m = "no registered cipher profile yields a valid PSB container";
        if (!sel.tried.empty()) {
            m += " (tried:";
            for (const std::string& t : sel.tried) m += " " + t;
            m += ")";
        }
        return Status::Fail(Error::UnknownCipherProfile, m);
    }
    if (accepted.size() > 1) {
        std::string m = "several cipher profiles yield a valid container:";
        for (const Result& r : accepted) m += " " + (r.profile ? r.profile->info().id : std::string("plain"));
        return Status::Fail(Error::AmbiguousCipherProfile, m);
    }

    Result& r = accepted[0];
    header_ = h;
    version_support_ = vs;
    sel.profile_id = r.profile ? r.profile->info().id : std::string();
    sel.confidence = r.confidence;
    cipher_ = std::move(sel);
    reader_ = std::move(r.reader);
    root_ = std::move(r.root);
    resources_ = std::move(r.resources);
    plain_adler_ = r.adler;
    mdf_ = mdf;
    return Status::Ok();
}

}  // namespace emote
}  // namespace kk4s
