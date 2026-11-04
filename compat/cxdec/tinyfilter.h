// kk4s: "TinyFilter" CXDEC variant (the key routine of a KiriKiri TPM plugin).
//
// Not every Cx TPM generates and interprets x86 programs. Some ("TinyFilterCore"
// in the plugin's own RTTI) derive one byte from the entry's XP3 "adlr" hash and
// XOR the whole content of that entry with it. The derivation, as found in the
// TPM's key routine, is:
//
//     x = adlr ^ 0x1548e29c
//     k = (x >> 24) ^ (x >> 16) ^ (x >> 8) ^ x      (low 8 bits)
//     if k == 0: k = 0xd7                          (the TPM never applies a no-op)
//
// and the transform is position independent (the TPM XORs buffer ranges with the
// same byte, including dword-wise in the middle).
//
// Nothing here is game specific: the constant belongs to the filter format. As
// with the Cx interpreter, a candidate filter is only used for an archive after
// its own entries validated (adler32 of the decrypted data == adlr), see
// compat/kk4s_cx.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

namespace kk4s {
namespace cxdec {

// Keystream byte for the entry whose XP3 adlr value is `adlr`.
uint8_t TinyFilterKey(uint32_t adlr);

// XORs `count` bytes in place. `offset` does not matter for this filter; the
// signature matches a stream filter so callers can treat it like the Cx one.
void TinyFilterDecrypt(uint32_t adlr, uint8_t* buf, size_t count);

}  // namespace cxdec
}  // namespace kk4s
