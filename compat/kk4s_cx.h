// kk4s: CXDEC scheme discovery and per-archive binding (layers 2/3).
//
// The generic decoders live in compat/cxdec. This file only decides *which*
// filter an archive uses, from data: candidate schemes are loaded from
// <RootDir>/profiles/cx/*.cxscheme, and every candidate (including the
// scheme-free "TinyFilter" variant) is tested on a few entries of the archive
// (adler32 of the decrypted data must equal the entry's "adlr"). Only a filter
// that passes is bound; nothing is applied to archives without hashed names
// records, so ordinary XP3 games (FSN and its patches) never reach this code.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace kk4s {
namespace cxdec { class CxDecoder; struct CxScheme; }
namespace compat {

// What an archive stream needs from a bound filter: decrypt a range of an entry
// (identified by its XP3 adlr hash) in place. Implementations live until exit.
class ContentDecoder {
public:
    virtual ~ContentDecoder() {}
    virtual void Decrypt(uint32_t hash, uint64_t offset, uint8_t* buf, size_t count) = 0;
    // For logs only, e.g. "cx:<scheme file>" or "tinyfilter/adlr-xor".
    virtual const std::string& FamilyName() const = 0;
};

struct CxSample {
    uint32_t adlr;
    std::vector<uint8_t> raw;  // decompressed but still encrypted bytes
};

// Returns the filter for an archive with hnfn/XP3v2 eliF (Cx name) records, or
// nullptr when no known filter fits. `samples` are a few small entries of it.
// Filters are shared between archives of the same game and live until exit.
ContentDecoder* BindCxDecoder(const std::string& archive_name, const std::vector<CxSample>& samples);

// Desktop-only generic recovery from the game's TPM (or an embedded internal
// module). The recovered scheme is accepted only after every candidate is
// checked against archive samples; it is never persisted into the game folder.
bool RecoverCxSchemeFromGame(const std::string& game_dir,
    const std::vector<CxSample>& samples, cxdec::CxScheme& scheme, std::string& error);

// True once any archive of this run got a verified filter (positive detection
// of a Cx-protected game; used to gate Cx-specific startup handling).
bool AnyCxArchiveBound();

}  // namespace compat
}  // namespace kk4s
