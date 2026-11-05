// kk4s E-mote runtime: adapter registry and shared helpers. See model_adapter.h.
#include "emote/model_adapter.h"

namespace kk4s {
namespace emote {

Status AdapterRegistry::Register(std::unique_ptr<ModelAdapter> a) {
    if (!a || !a->id() || !*a->id()) return Status::Fail(Error::InvalidArgument, "adapter without id");
    for (const auto& have : adapters_)
        if (std::string(have->id()) == a->id())
            return Status::Fail(Error::InvalidArgument, std::string("duplicate adapter id ") + a->id());
    adapters_.push_back(std::move(a));
    return Status::Ok();
}

Status AdapterRegistry::BuildModel(const Value& root, CharacterModel& out) const {
    const ModelAdapter* chosen = nullptr;
    std::string why;
    for (const auto& a : adapters_) {
        Status p = a->Probe(root);
        if (p.ok()) {
            if (chosen)
                return Status::Fail(Error::UnsupportedSchema,
                                    std::string("several adapters claim the document: ") + chosen->id() + ", " + a->id());
            chosen = a.get();
        } else {
            why += std::string(" ") + a->id() + ": " + p.message + ";";
        }
    }
    if (!chosen) return Status::Fail(Error::UnsupportedSchema, "no adapter understands this document;" + why);
    out = CharacterModel();
    out.adapter_id = chosen->id();
    return chosen->Build(root, out);
}

const ModelAdapter* AdapterRegistry::Find(const std::string& id) const {
    for (const auto& a : adapters_)
        if (id == a->id()) return a.get();
    return nullptr;
}

const AdapterRegistry& AdapterRegistry::Builtin() {
    static const AdapterRegistry* r = [] {
        AdapterRegistry* reg = new AdapterRegistry();
        reg->Register(MakeEmoteV3Adapter());
        return reg;
    }();
    return *r;
}

namespace {
bool StartsWith(const std::string& s, const char* p, std::string& rest) {
    size_t n = std::char_traits<char>::length(p);
    if (s.compare(0, n, p) != 0) return false;
    rest = s.substr(n);
    return true;
}
}  // namespace

SourceRef ParseSourceRef(const std::string& src, const std::string* icon, const CharacterModel& model) {
    SourceRef r;
    r.raw = icon ? src + "|" + *icon : src;
    std::string rest;
    if (!icon) {
        if (src.empty()) {
            r.kind = SourceRef::Kind::None;
        } else if (src == "layout") {
            r.kind = SourceRef::Kind::Layout;
        } else if (StartsWith(src, "src/", rest) || StartsWith(src, "motion/", rest)) {
            size_t slash = rest.find('/');
            if (slash == std::string::npos) {
                r.kind = SourceRef::Kind::Unknown;
            } else {
                r.kind = src[0] == 's' ? SourceRef::Kind::Icon : SourceRef::Kind::Motion;
                r.a = rest.substr(0, slash);
                r.b = rest.substr(slash + 1);
            }
        } else if (StartsWith(src, "blank/", rest)) {
            r.kind = SourceRef::Kind::Blank;
            r.detail = rest;
        } else if (StartsWith(src, "shape/", rest)) {
            r.kind = SourceRef::Kind::Shape;
            r.detail = rest;
        } else {
            r.kind = SourceRef::Kind::Unknown;
        }
        return r;
    }
    if (src.empty()) {
        r.kind = SourceRef::Kind::None;
    } else if (src == "blank") {
        r.kind = SourceRef::Kind::Blank;
        r.detail = *icon;
    } else if (model.FindSource(src)) {
        r.kind = SourceRef::Kind::Icon;
        r.a = src;
        r.b = *icon;
    } else if (model.FindObject(src)) {
        r.kind = SourceRef::Kind::Motion;
        r.a = src;
        r.b = *icon;
    } else {
        r.kind = SourceRef::Kind::Unknown;
    }
    return r;
}

}  // namespace emote
}  // namespace kk4s
