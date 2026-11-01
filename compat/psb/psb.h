// kk4s: reader of the PSB container (E-mote / M2 "PSB": .pimg, .scn, .psb, ...).
//
// Pure C++ (no engine, no platform code). It walks the value tree and reports it
// through a Visitor, so callers decide the representation (TJS values in the
// psbfile plugin, text in the tests). Layout and value encoding are documented
// in tools/psb_ref.py (the reference reader the tests compare against).
//
// Input is untrusted: every offset is bounds-checked and the nesting depth is
// limited. "mdf\0" files (zlib-compressed PSB) are inflated by Open().
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kk4s {
namespace psb {

class Visitor {
public:
    virtual ~Visitor() {}
    virtual void Undefined() = 0;  // type 0x00
    virtual void Null() = 0;       // type 0x01
    virtual void Bool(bool v) = 0;
    virtual void Int(int64_t v) = 0;
    virtual void Double(double v) = 0;
    virtual void String(const std::string& utf8) = 0;
    // Resource chunk (e.g. an embedded TLG image); data points into the reader's buffer.
    virtual void Resource(uint32_t index, const uint8_t* data, size_t size) = 0;
    virtual void Extra(uint32_t index) = 0;  // resource in an "extra" chunk (not supported: no data)
    virtual void BeginObject(size_t count) = 0;
    virtual void Key(const std::string& name) = 0;  // followed by exactly one value
    virtual void EndObject() = 0;
    virtual void BeginArray(size_t count) = 0;      // followed by `count` values
    virtual void EndArray() = 0;
};

class Reader {
public:
    // Takes a copy of the file bytes. Returns false and sets error() on any problem
    // (not a PSB, encrypted header, truncated/corrupt tables).
    bool Open(const uint8_t* data, size_t size);

    // Walks the root value. Returns false (error() set) if the tree is corrupt.
    bool Walk(Visitor& v) const;

    const std::string& error() const { return error_; }
    uint16_t version() const { return version_; }

private:
    bool Fail(const char* msg) const;
    bool Uint(size_t pos, unsigned n, uint64_t& out) const;
    bool ReadArray(size_t pos, std::vector<uint64_t>& out, size_t& end) const;
    bool LoadStrings();
    bool LoadNames();
    bool WalkValue(size_t pos, Visitor& v, int depth) const;

    std::vector<uint8_t> buf_;
    uint16_t version_ = 0;
    uint32_t off_names_ = 0, off_strings_ = 0, off_strdata_ = 0, off_choffs_ = 0, off_chlens_ = 0,
             off_chdata_ = 0, off_entries_ = 0;
    std::vector<std::string> strings_;
    std::vector<std::string> names_;
    std::vector<uint64_t> chunk_offs_, chunk_lens_;
    mutable std::string error_;
};

}  // namespace psb
}  // namespace kk4s
