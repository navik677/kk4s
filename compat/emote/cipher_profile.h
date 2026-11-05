// kk4s E-mote runtime: PSB cipher profiles.
//
// Some PSB writers (the E-mote 3.0 reader in NekoPara Vol.1) encrypt part of
// the container in place; the header does not say so (its encrypt field is 0).
// A profile describes one such scheme completely: algorithm, key material,
// initialisation, keystream, region, refill rule, layout preconditions, and
// the evidence it was recovered from.
//
// Rules:
//  - The core never contains key material; profiles do.
//  - A profile is accepted only when its output passes full structural
//    validation. Binary hashes raise confidence, they never select alone.
//  - No key search: only registered, evidence-backed profiles are tried.
//  - New schemes are added by registering a profile, not by changing the core.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "emote/emote_status.h"

namespace kk4s {
namespace emote {

struct PsbHeader;

struct ProfileEvidence {
    // SHA-256 (lowercase hex) of the binaries the scheme was recovered from.
    std::vector<std::string> binary_sha256;
    // Free text: where it was recovered, how many real files were verified.
    std::string source;
    uint32_t real_files_verified = 0;
};

struct ProfileInfo {
    std::string id;           // stable, e.g. "emote.xorshift128.nekopara-vol1"
    std::string description;
    std::vector<uint16_t> psb_versions;  // container versions seen with this profile
    ProfileEvidence evidence;
};

struct ByteRegion {
    size_t begin = 0;
    size_t end = 0;  // exclusive
};

class CipherProfile {
public:
    virtual ~CipherProfile() {}
    virtual const ProfileInfo& info() const = 0;
    // Layout rules the scheme was verified with. A file that breaks them is
    // refused instead of being decrypted on a guess.
    virtual Status CheckPreconditions(const PsbHeader& h, size_t size) const = 0;
    virtual ByteRegion Region(const PsbHeader& h) const = 0;
    // Decrypts (or encrypts: the schemes here are XOR streams) `Region` in place.
    virtual void Apply(uint8_t* data, size_t size, const PsbHeader& h) const = 0;
};

// ---------------------------------------------------------------- xorshift128

// Marsaglia xorshift128 with a caller-supplied initial state, emitting the low
// byte of the current word first and drawing a new word only when the
// shifted word becomes zero (the E-mote reader's loop; see
// docs/emote_psb_research.md).
class Xorshift128Keystream {
public:
    Xorshift128Keystream(uint32_t x, uint32_t y, uint32_t z, uint32_t w);
    uint8_t Next();
    void XorInto(uint8_t* p, size_t n);
    uint64_t words_drawn() const { return words_; }

private:
    uint32_t x_, y_, z_, w_, cur_ = 0;
    uint64_t words_ = 0;
};

struct Xorshift128Params {
    uint32_t seed_x = 0, seed_y = 0, seed_z = 0;
    uint32_t key_w = 0;  // fourth state word
    // The only region rule observed so far: [header_len, off_chunk_offsets).
    enum class RegionRule { HeaderLenToChunkOffsets } region = RegionRule::HeaderLenToChunkOffsets;
    // Layout preconditions the scheme was verified under.
    bool require_header_len_equals_names = true;
};

// Generic implementation; concrete builds are instances with their own params.
class Xorshift128RegionProfile : public CipherProfile {
public:
    Xorshift128RegionProfile(ProfileInfo info, Xorshift128Params params);
    const ProfileInfo& info() const override { return info_; }
    Status CheckPreconditions(const PsbHeader& h, size_t size) const override;
    ByteRegion Region(const PsbHeader& h) const override;
    void Apply(uint8_t* data, size_t size, const PsbHeader& h) const override;
    const Xorshift128Params& params() const { return params_; }

private:
    ProfileInfo info_;
    Xorshift128Params params_;
};

// ---------------------------------------------------------------- registry

class CipherRegistry {
public:
    // Returns InvalidArgument for an empty or duplicate id.
    Status Register(std::unique_ptr<CipherProfile> profile);
    const CipherProfile* Find(const std::string& id) const;
    const std::vector<std::unique_ptr<CipherProfile>>& profiles() const { return profiles_; }

    // The profiles shipped with kk4s (compat/emote/profiles/).
    static const CipherRegistry& Builtin();

private:
    std::vector<std::unique_ptr<CipherProfile>> profiles_;
};

// Defined in compat/emote/profiles/builtin_profiles.cpp.
void RegisterBuiltinProfiles(CipherRegistry& r);

}  // namespace emote
}  // namespace kk4s
