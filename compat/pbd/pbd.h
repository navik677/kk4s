// kk4s: decoder for KiriKiri TJS binary data packs (`.pbd`, "TJS/ns0" and "TJS/4s0").
//
// A data pack is a serialized TJS value tree:
//   0 = void, 2 = UTF-16 string, 4 = int64, 5 = double, 0x81 = array,
//   0xc1 = dictionary.
// Every value is preceded by a type byte and a checker byte derived from the
// header seed, so a wrong key/stream is rejected instead of producing garbage.
//
//   TJS/ns0  header(16) + value stream + u32 checksum
//   TJS/4s0  header(16) + IV + [crypt] + framed LZ4 + value stream + u32 trailer
//
// The 4s0 cipher is BLAKE2s-256(seed as key, IV as message) -> XXH32 nonce ->
// ChaCha rounds -> PackinOne xorshift stream expansion. This is the container
// format used by PackinOne data packs (e.g. ATRI character layers); it is not
// tied to any game or file name.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace kk4s {
namespace pbd {

struct Value {
    enum Type { Void, String, Integer, Double, Array, Dictionary };

    Type type = Void;
    std::string string;  // UTF-8
    int64_t integer = 0;
    double real = 0.0;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> dictionary;
};

// True when the buffer starts with a supported data pack signature.
bool Sniff(const uint8_t* data, std::size_t size);

// Decodes a complete data pack. Returns false and fills `error` on failure;
// `out` is unspecified then.
bool Decode(const uint8_t* data, std::size_t size, Value& out, std::string& error);

}  // namespace pbd
}  // namespace kk4s
