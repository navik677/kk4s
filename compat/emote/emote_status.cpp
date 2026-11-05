// kk4s E-mote runtime: error names. See emote_status.h.
#include "emote/emote_status.h"

namespace kk4s {
namespace emote {

const char* ErrorName(Error e) {
    switch (e) {
    case Error::None: return "ok";
    case Error::InvalidArgument: return "invalid-argument";
    case Error::NotPsb: return "not-psb";
    case Error::Truncated: return "truncated";
    case Error::UnsupportedVersion: return "unsupported-version";
    case Error::UnsupportedLayout: return "unsupported-layout";
    case Error::UnknownCipherProfile: return "unknown-cipher-profile";
    case Error::AmbiguousCipherProfile: return "ambiguous-cipher-profile";
    case Error::ProfileMismatch: return "profile-mismatch";
    case Error::CorruptStructure: return "corrupt-structure";
    case Error::UnsupportedSchema: return "unsupported-schema";
    case Error::NotFound: return "not-found";
    case Error::UnsupportedFeature: return "unsupported-feature";
    }
    return "unknown-error";
}

std::string Status::ToString() const {
    if (ok()) return "ok";
    std::string s = ErrorName(code);
    if (!message.empty()) {
        s += ": ";
        s += message;
    }
    return s;
}

}  // namespace emote
}  // namespace kk4s
