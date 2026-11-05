// kk4s E-mote runtime: PSB core.
//
// Input: the bytes of one PSB file, *after* the archive layer (XP3 index,
// zlib segments, TinyFilter or any other archive filter) has been removed.
// The core does not know where the bytes came from.
//
// Pipeline:  header -> cipher profile selection -> compat/psb reader -> Value
//
// The container walk itself is compat/psb (psb::Reader, shared with the
// psbfile.dll plugin); this layer adds the header model, version policy,
// cipher-profile selection with evidence, and the normalized Value tree.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "emote/emote_status.h"
#include "emote/psb_value.h"

namespace kk4s {
namespace psb {
class Reader;
}
namespace emote {

class CipherRegistry;

struct PsbHeader {
    uint16_t version = 0;
    uint16_t encrypt_flag = 0;  // header field at 0x06
    uint32_t header_len = 0;    // 0x08
    uint32_t off_names = 0;     // 0x0C
    uint32_t off_strings = 0;   // 0x10
    uint32_t off_strdata = 0;   // 0x14
    uint32_t off_chunk_offsets = 0;  // 0x18
    uint32_t off_chunk_lengths = 0;  // 0x1C
    uint32_t off_chunk_data = 0;     // 0x20
    uint32_t off_entries = 0;        // 0x24 (root value)
};

constexpr size_t kPsbFixedHeaderSize = 0x28;

// Parses the plain header fields. Checks magic and size only; layout rules
// belong to whoever uses the offsets (cipher profiles, the reader).
Status ParsePsbHeader(const uint8_t* data, size_t size, PsbHeader& out);

// What kk4s knows about a container version. Accepting a header is not the
// same as supporting the version: only Verified was seen in real files.
enum class VersionSupport {
    Verified,          // parsed and checked on real files (v2: 157 NekoPara PSB)
    ParsedUnverified,  // same layout assumed, only synthetic tests (v3, v4)
    Unknown,           // no layout known: refused
};
VersionSupport ClassifyPsbVersion(uint16_t version);
const char* VersionSupportName(VersionSupport s);

// How a cipher profile was chosen. Ordered from weakest to strongest.
enum class Confidence {
    None,
    PlainContainer,   // the file is a valid PSB without any decryption
    StructureOnly,    // a registered profile's output validates; no binary evidence
    BinaryEvidence,   // + a binary hash listed in the profile's evidence was supplied
    Pinned,           // the caller named the profile and its output validates
};
const char* ConfidenceName(Confidence c);

struct LoadOptions {
    // Profile id to use. Empty = detect among the registered profiles.
    std::string pinned_profile;
    // SHA-256 (lowercase hex) of binaries that came with the file, e.g. the
    // game's emoteplayer.dll. Raises confidence; never selects a profile alone.
    std::vector<std::string> binary_sha256;
    // When no binary evidence matches, still try the registered profiles and
    // accept one whose output validates (reported as StructureOnly).
    bool allow_structural_detection = true;
    // Accept container versions classified ParsedUnverified.
    bool allow_unverified_versions = true;
    // Registry to use; nullptr = CipherRegistry::Builtin().
    const CipherRegistry* registry = nullptr;
};

struct CipherSelection {
    std::string profile_id;  // empty for PlainContainer
    Confidence confidence = Confidence::None;
    std::vector<std::string> tried;    // profile ids that were tried
    std::vector<std::string> reasons;  // why candidates were rejected / accepted
};

struct ResourceSpan {
    const uint8_t* data = nullptr;
    size_t size = 0;
};

class Document {
public:
    Document();
    ~Document();
    Document(Document&&) noexcept;
    Document& operator=(Document&&) noexcept;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    // Loads one PSB file. On failure the document stays empty.
    Status Load(const uint8_t* data, size_t size, const LoadOptions& opt = LoadOptions());

    const PsbHeader& header() const { return header_; }
    VersionSupport version_support() const { return version_support_; }
    const CipherSelection& cipher() const { return cipher_; }
    const Value& root() const { return root_; }
    bool mdf_wrapped() const { return mdf_; }

    size_t resource_count() const { return resources_.size(); }
    // Bytes of resource chunk `index` (as referenced by Value::Resource).
    bool Resource(uint32_t index, ResourceSpan& out) const;

    // adler32 of the decrypted container, for cross-checks with other tools.
    uint32_t plain_adler32() const { return plain_adler_; }

private:
    PsbHeader header_;
    VersionSupport version_support_ = VersionSupport::Unknown;
    CipherSelection cipher_;
    Value root_;
    bool mdf_ = false;
    uint32_t plain_adler_ = 0;
    std::unique_ptr<psb::Reader> reader_;  // owns the bytes resources point into
    std::vector<ResourceSpan> resources_;
};

// Parses already-plain bytes into a Value tree (no cipher logic). Used by the
// loader to validate candidates; exposed for tests and tools.
Status ParsePlainContainer(const uint8_t* data, size_t size, std::unique_ptr<psb::Reader>& reader,
                           Value& root, std::vector<ResourceSpan>& resources);

}  // namespace emote
}  // namespace kk4s
