// kk4s: generic CXDEC (KiriKiri "Cx" XP3 content filter).
//
// Portable C++ (no engine, platform or game code). The algorithm is the one of
// the CX TPM plugin, as documented by GARbro's CxEncryption; see docs/cxdec.md.
// A game is described only by data (CxScheme); nothing here is game-specific.
#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace kk4s {
namespace cxdec {

struct CxScheme {
    uint32_t mask = 0;
    uint32_t offset = 0;
    uint8_t prolog_order[3] = {0, 1, 2};
    uint8_t odd_branch_order[6] = {0, 1, 2, 3, 4, 5};
    uint8_t even_branch_order[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint32_t control_block[1024] = {};  // raw little-endian values as the TPM holds them
    std::string name;                   // where it came from (for logs)

    // True when the three orders are permutations (a malformed scheme is rejected).
    bool IsValid() const;
};

// Parses the text form written by tools/cx_scheme_extract.py:
//   mask=0x1a7 / offset=0x369 / prolog=0,1,2 / odd=... / even=... / control_block=<file>
// `load_file` reads the control block file named in the text (relative names are
// the caller's business). Returns false and fills `error` on any problem.
bool ParseScheme(const std::string& text,
                 bool (*load_file)(const std::string& name, std::vector<uint8_t>& out, void* ctx),
                 void* ctx, CxScheme& out, std::string& error);

class CxDecoder {
public:
    explicit CxDecoder(const CxScheme& scheme);
    ~CxDecoder();

    // Decrypts `count` bytes that start at file offset `offset` of the entry whose
    // XP3 "adlr" value is `hash`. Thread-safe.
    void Decrypt(uint32_t hash, uint64_t offset, uint8_t* buf, size_t count);

    const CxScheme& Scheme() const { return scheme_; }

    struct Program;  // generated per seed (hash & 0x7f), cached

private:
    void Decode(uint32_t key, uint64_t offset, uint8_t* buf, size_t count);
    const Program& GetProgram(uint32_t seed);

    CxScheme scheme_;
    std::unique_ptr<Program> programs_[128];
    std::mutex mutex_;
};

}  // namespace cxdec
}  // namespace kk4s
