// kk4s: discovery of simple XP3 content encryption (the .tpm families that need
// no game-specific key), validated by the archive's own adler32 values.
//
// Many Kirikiri games ship a .tpm plugin that decrypts archive contents with a
// tiny keyless function of the entry's adlr hash and offset (one XOR byte per
// entry derived from the hash, offset-dependent XOR/add patterns, bit rotations).
// Instead of carrying any game's key, kk4s tries these algorithm families on a few
// small entries and binds one only when adler32 of the decrypted data equals each
// entry's "adlr" field for (nearly) all samples - the same rule as the CXDEC
// binder (kk4s_cx.h). Algorithms follow GARbro's KiriKiri crypt implementations
// (MIT); schemes that need a table or secret constant of one particular game are
// deliberately not included.
#pragma once

#include "kk4s_cx.h"

#include <string>
#include <vector>

namespace kk4s {
namespace compat {

// samples: small entries of an archive without hashed names, raw = decompressed
// but possibly encrypted bytes. Returns nullptr when the archive is plain, when
// the samples cannot tell (adlr all zero), or when no family is verified.
// Decoders live until exit.
ContentDecoder* BindSimpleCrypt(const std::string& archive_name, const std::vector<CxSample>& samples);

}  // namespace compat
}  // namespace kk4s
