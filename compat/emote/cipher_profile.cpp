// kk4s E-mote runtime: cipher profiles and registry. See cipher_profile.h.
#include "emote/cipher_profile.h"

#include "emote/psb_document.h"

#include <algorithm>
#include <cstdio>

namespace kk4s {
namespace emote {

// ---------------------------------------------------------------- keystream

Xorshift128Keystream::Xorshift128Keystream(uint32_t x, uint32_t y, uint32_t z, uint32_t w)
    : x_(x), y_(y), z_(z), w_(w) {}

uint8_t Xorshift128Keystream::Next() {
    if (cur_ == 0) {
        uint32_t t = x_ ^ (x_ << 11);
        x_ = y_;
        y_ = z_;
        z_ = w_;
        w_ = w_ ^ (w_ >> 19) ^ t ^ (t >> 8);
        cur_ = w_;
        words_++;
    }
    uint8_t b = (uint8_t)(cur_ & 0xFF);
    cur_ >>= 8;  // unsigned: logical shift
    return b;
}

void Xorshift128Keystream::XorInto(uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) p[i] ^= Next();
}

// ---------------------------------------------------------------- xorshift profile

Xorshift128RegionProfile::Xorshift128RegionProfile(ProfileInfo info, Xorshift128Params params)
    : info_(std::move(info)), params_(params) {}

Status Xorshift128RegionProfile::CheckPreconditions(const PsbHeader& h, size_t size) const {
    char buf[160];
    if (!info_.psb_versions.empty() &&
        std::find(info_.psb_versions.begin(), info_.psb_versions.end(), h.version) == info_.psb_versions.end()) {
        std::snprintf(buf, sizeof buf, "container version %u was never seen with this profile", (unsigned)h.version);
        return Status::Fail(Error::ProfileMismatch, buf);
    }
    if (h.header_len < kPsbFixedHeaderSize)
        return Status::Fail(Error::ProfileMismatch, "header length is smaller than the fixed header");
    if (params_.require_header_len_equals_names && h.header_len != h.off_names) {
        std::snprintf(buf, sizeof buf, "header length 0x%x != names offset 0x%x (layout not verified)",
                      (unsigned)h.header_len, (unsigned)h.off_names);
        return Status::Fail(Error::ProfileMismatch, buf);
    }
    const uint32_t order[] = {h.off_names, h.off_strings, h.off_strdata, h.off_chunk_offsets,
                              h.off_chunk_lengths, h.off_chunk_data};
    for (size_t i = 1; i < sizeof order / sizeof order[0]; i++)
        if (order[i - 1] >= order[i])
            return Status::Fail(Error::ProfileMismatch, "section offsets are not strictly increasing");
    if (h.off_chunk_data > size) return Status::Fail(Error::ProfileMismatch, "chunk data offset past the end");
    if (h.off_entries == 0 || h.off_entries >= size)
        return Status::Fail(Error::ProfileMismatch, "root entry offset outside the file");
    return Status::Ok();
}

ByteRegion Xorshift128RegionProfile::Region(const PsbHeader& h) const {
    ByteRegion r;
    switch (params_.region) {
    case Xorshift128Params::RegionRule::HeaderLenToChunkOffsets:
        r.begin = h.header_len;
        r.end = h.off_chunk_offsets;
        break;
    }
    return r;
}

void Xorshift128RegionProfile::Apply(uint8_t* data, size_t size, const PsbHeader& h) const {
    ByteRegion r = Region(h);
    if (r.end > size) r.end = size;
    if (r.begin >= r.end) return;
    Xorshift128Keystream ks(params_.seed_x, params_.seed_y, params_.seed_z, params_.key_w);
    ks.XorInto(data + r.begin, r.end - r.begin);
}

// ---------------------------------------------------------------- registry

Status CipherRegistry::Register(std::unique_ptr<CipherProfile> profile) {
    if (!profile || profile->info().id.empty()) return Status::Fail(Error::InvalidArgument, "profile without id");
    if (Find(profile->info().id))
        return Status::Fail(Error::InvalidArgument, "duplicate profile id " + profile->info().id);
    profiles_.push_back(std::move(profile));
    return Status::Ok();
}

const CipherProfile* CipherRegistry::Find(const std::string& id) const {
    for (const auto& p : profiles_)
        if (p->info().id == id) return p.get();
    return nullptr;
}

const CipherRegistry& CipherRegistry::Builtin() {
    static const CipherRegistry* r = [] {
        CipherRegistry* reg = new CipherRegistry();
        RegisterBuiltinProfiles(*reg);
        return reg;
    }();
    return *r;
}

}  // namespace emote
}  // namespace kk4s
