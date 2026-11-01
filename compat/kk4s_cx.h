// kk4s: CXDEC scheme discovery and per-archive binding (layers 2/3).
//
// The generic decoder is compat/cxdec. This file only decides *which* scheme an
// archive uses, from data: candidate schemes are loaded from
// <RootDir>/profiles/cx/*.cxscheme and each is tested on a few entries of the
// archive (adler32 of the decrypted data must equal the entry's "adlr"). Only a
// scheme that passes is bound; nothing is applied to archives without hnfn
// records, so ordinary XP3 games (FSN and its patches) never reach this code.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace kk4s {
namespace cxdec { class CxDecoder; }
namespace compat {

struct CxSample {
    uint32_t adlr;
    std::vector<uint8_t> raw;  // decompressed but still encrypted bytes
};

// Returns the decoder for an archive whose index has hnfn (Cx name) records, or
// nullptr when no known scheme fits. `samples` are a few small entries of it.
// Decoders are shared between archives of the same game and live until exit.
cxdec::CxDecoder* BindCxDecoder(const std::string& archive_name, const std::vector<CxSample>& samples);

// True once any archive of this run got a verified scheme (positive detection
// of a Cx-protected game; used to gate Cx-specific startup handling).
bool AnyCxArchiveBound();

}  // namespace compat
}  // namespace kk4s
