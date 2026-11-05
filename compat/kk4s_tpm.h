// kk4s: run a game's own XP3 filter plugin (.tpm) in the x86 interpreter
// (compat/x86) instead of guessing its algorithm.
//
// Kirikiri 2 loads every *.tpm next to the exe (and in plugin/) at startup and
// calls its V2Link(exporter); a filter plugin asks the exporter for
// TVPSetXP3ArchiveExtractionFilter and hands it a callback that decrypts a
// buffer given (offset, buffer, size, adlr hash). kk4s loads the PE32 into the
// interpreter with a fake exporter and a small fake Win32 API, captures the
// callback, and binds it to an archive only when the decrypted sample entries
// match their adlr values - the same rule as kk4s_xp3crypt / kk4s_cx. The key
// stays in the game's file; nothing is written anywhere.
//
// Speed: when the filter is a XOR with a keystream that, past a prefix, repeats
// with a short period (checked on samples and at far offsets), the keystream
// prefix of each file is computed once by the interpreter and the rest is done
// natively. Other filters run in the interpreter for every read (slow).
#pragma once

#include "kk4s_cx.h"

#include <string>
#include <vector>

namespace kk4s {
namespace compat {

// nullptr when the game has no .tpm, none of them sets a filter, or the filter
// does not reproduce the samples' adlr values.
ContentDecoder* BindTpmFilter(const std::string& archive_name, const std::vector<CxSample>& samples);

// For tests and tools: load one .tpm and run its filter on a buffer. Returns
// false with a reason when the plugin cannot be loaded or the filter fails.
bool RunTpmFilterOnce(const std::string& tpm_path, uint32_t hash, uint64_t offset, std::vector<uint8_t>& buf,
                      std::string& error);

}  // namespace compat
}  // namespace kk4s
