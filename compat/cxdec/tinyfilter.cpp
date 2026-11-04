// kk4s: "TinyFilter" CXDEC variant. See tinyfilter.h.
#include "tinyfilter.h"

namespace kk4s {
namespace cxdec {

namespace {
const uint32_t kKeyConstant = 0x1548e29cu;
const uint8_t kZeroKey = 0xd7;
}  // namespace

uint8_t TinyFilterKey(uint32_t adlr) {
    uint32_t x = adlr ^ kKeyConstant;
    uint8_t k = (uint8_t)((x >> 24) ^ (x >> 16) ^ (x >> 8) ^ x);
    return k ? k : kZeroKey;
}

void TinyFilterDecrypt(uint32_t adlr, uint8_t* buf, size_t count) {
    const uint8_t k = TinyFilterKey(adlr);
    for (size_t i = 0; i < count; i++) buf[i] ^= k;
}

}  // namespace cxdec
}  // namespace kk4s
