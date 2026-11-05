// kk4s E-mote runtime: error reporting shared by every layer.
//
// Every public entry point returns a Status instead of throwing, so an unknown
// file or build is reported as a controlled error, never as garbage data.
#pragma once

#include <string>

namespace kk4s {
namespace emote {

enum class Error {
    None = 0,
    InvalidArgument,
    NotPsb,                  // no "PSB\0" / "mdf\0" magic
    Truncated,               // shorter than its own header says
    UnsupportedVersion,      // container version with no known layout
    UnsupportedLayout,       // header flags/offsets no known reader handles
    UnknownCipherProfile,    // no registered profile yields a valid container
    AmbiguousCipherProfile,  // more than one profile yields a valid container
    ProfileMismatch,         // the pinned profile does not fit this file
    CorruptStructure,        // tables/values out of bounds after decoding
    UnsupportedSchema,       // container is fine, no adapter knows its content
    NotFound,                // a named variable/timeline/motion does not exist
    UnsupportedFeature,      // known to exist, not implemented / not researched
};

const char* ErrorName(Error e);

struct Status {
    Error code = Error::None;
    std::string message;

    bool ok() const { return code == Error::None; }
    static Status Ok() { return Status(); }
    static Status Fail(Error e, std::string msg) {
        Status s;
        s.code = e;
        s.message = std::move(msg);
        return s;
    }
    // "<ErrorName>: <message>"
    std::string ToString() const;
};

}  // namespace emote
}  // namespace kk4s
