// kk4s: PSB reader. See psb.h and tools/psb_ref.py.
#include "psb.h"

#include <zlib.h>

#include <cstring>

namespace kk4s {
namespace psb {

namespace {
const int kMaxDepth = 64;
const size_t kMaxInflated = 512u << 20;  // sanity limit for "mdf" files
}  // namespace

bool Reader::Fail(const char* msg) const {
    error_ = msg;
    return false;
}

bool Reader::Uint(size_t pos, unsigned n, uint64_t& out) const {
    if (n == 0 || n > 8 || pos > buf_.size() || buf_.size() - pos < n) return false;
    uint64_t v = 0;
    for (unsigned i = 0; i < n; i++) v |= (uint64_t)buf_[pos + i] << (8 * i);
    out = v;
    return true;
}

// Compact uint array: type 0x0D..0x10 (count width 1..4), count, entry width byte
// 0x0D..0x14 (1..8), entries.
bool Reader::ReadArray(size_t pos, std::vector<uint64_t>& out, size_t& end) const {
    if (pos >= buf_.size()) return false;
    unsigned t = buf_[pos];
    if (t < 0x0D || t > 0x10) return false;
    unsigned cw = t - 0x0C;
    uint64_t count = 0;
    if (!Uint(pos + 1, cw, count)) return false;
    size_t epos = pos + 1 + cw;
    if (epos >= buf_.size()) return false;
    unsigned et = buf_[epos];
    size_t p = epos + 1;
    if (count == 0) {  // empty arrays may carry any entry-width byte
        out.clear();
        end = p;
        return true;
    }
    if (et < 0x0D || et > 0x14) return false;
    unsigned ew = et - 0x0C;
    if (count > (buf_.size() - p) / ew) return false;  // also stops absurd counts
    out.resize((size_t)count);
    for (size_t i = 0; i < (size_t)count; i++) {
        uint64_t v = 0;
        Uint(p + i * ew, ew, v);
        out[i] = v;
    }
    end = p + (size_t)count * ew;
    return true;
}

bool Reader::Open(const uint8_t* data, size_t size) {
    error_.clear();
    strings_.clear();
    names_.clear();
    if (size >= 8 && std::memcmp(data, "mdf\0", 4) == 0) {
        uLongf raw = (uLongf)data[4] | ((uLongf)data[5] << 8) | ((uLongf)data[6] << 16) | ((uLongf)data[7] << 24);
        if (raw > kMaxInflated) return Fail("invalid psb file.");
        buf_.assign(raw, 0);
        uLongf got = raw;
        if (uncompress(buf_.data(), &got, data + 8, (uLong)(size - 8)) != Z_OK || got != raw) return Fail("invalid psb file.");
    } else {
        buf_.assign(data, data + size);
    }
    if (buf_.size() < 0x28 || std::memcmp(buf_.data(), "PSB\0", 4) != 0) return Fail("invalid psb file.");
    uint64_t v = 0;
    Uint(4, 2, v);
    version_ = (uint16_t)v;
    Uint(6, 2, v);
    if (v != 0) return Fail("encrypted psb file is not supported.");
    uint32_t* fields[] = {nullptr, &off_names_, &off_strings_, &off_strdata_, &off_choffs_, &off_chlens_, &off_chdata_, &off_entries_};
    for (int i = 1; i < 8; i++) {
        Uint(8 + i * 4, 4, v);
        *fields[i] = (uint32_t)v;
        // the chunk data area may start exactly at the end of a file without resources
        size_t limit = (fields[i] == &off_chdata_) ? buf_.size() + 1 : buf_.size();
        if (*fields[i] >= limit) return Fail("invalid psb file.");
    }
    if (!LoadStrings() || !LoadNames()) return false;
    // Files without resources may carry no valid chunk tables: keep them empty then
    // (a Resource value would be rejected as out of range).
    size_t end = 0;
    if (!ReadArray(off_choffs_, chunk_offs_, end) || !ReadArray(off_chlens_, chunk_lens_, end) ||
        chunk_offs_.size() != chunk_lens_.size()) {
        chunk_offs_.clear();
        chunk_lens_.clear();
    }
    return true;
}

bool Reader::LoadStrings() {
    std::vector<uint64_t> offs;
    size_t end = 0;
    if (!ReadArray(off_strings_, offs, end)) return Fail("invalid psb file.");
    strings_.reserve(offs.size());
    for (uint64_t o : offs) {
        size_t s = (size_t)off_strdata_ + (size_t)o;
        if (o >= buf_.size() || s >= buf_.size()) return Fail("invalid psb file.");
        size_t e = s;
        while (e < buf_.size() && buf_[e] != 0) e++;
        if (e >= buf_.size()) return Fail("invalid psb file.");
        strings_.emplace_back((const char*)&buf_[s], e - s);
    }
    return true;
}

// Names are a trie in three arrays: charset, tree, leaves (one per name).
// From a leaf: c = tree[idx]; ch = c - charset[tree[c]]; idx = c; c = tree[idx] ...
bool Reader::LoadNames() {
    std::vector<uint64_t> charset, tree, leaves;
    size_t p = 0;
    if (!ReadArray(off_names_, charset, p) || !ReadArray(p, tree, p) || !ReadArray(p, leaves, p)) return Fail("invalid psb file.");
    names_.reserve(leaves.size());
    for (uint64_t leaf : leaves) {
        std::string rev;
        uint64_t idx = leaf;
        if (idx >= tree.size()) return Fail("invalid psb file.");
        uint64_t c = tree[(size_t)idx];
        while (c != 0) {
            if (c >= tree.size() || tree[(size_t)c] >= charset.size() || rev.size() > 4096) return Fail("invalid psb file.");
            rev.push_back((char)(uint8_t)(c - charset[(size_t)tree[(size_t)c]]));
            idx = c;
            c = tree[(size_t)idx];
        }
        names_.emplace_back(rev.rbegin(), rev.rend());
    }
    return true;
}

bool Reader::Walk(Visitor& v) const {
    error_.clear();
    return WalkValue(off_entries_, v, 0);
}

bool Reader::WalkValue(size_t pos, Visitor& v, int depth) const {
    if (depth > kMaxDepth) return Fail("psb: nesting too deep.");
    if (pos >= buf_.size()) return Fail("invalid psb file.");
    unsigned t = buf_[pos];
    uint64_t u = 0;
    switch (t) {
    case 0x00: v.Undefined(); return true;
    case 0x01: v.Null(); return true;
    case 0x02: v.Bool(false); return true;
    case 0x03: v.Bool(true); return true;
    case 0x1D: v.Double(0.0); return true;
    default: break;
    }
    if (t >= 0x04 && t <= 0x0C) {  // signed int of (t-4) bytes
        unsigned n = t - 4;
        int64_t val = 0;
        if (n) {
            if (!Uint(pos + 1, n, u)) return Fail("invalid psb file.");
            val = (int64_t)(u << (64 - 8 * n)) >> (64 - 8 * n);  // sign-extend
        }
        v.Int(val);
        return true;
    }
    if (t >= 0x0D && t <= 0x10) {  // compact int array -> array of ints
        std::vector<uint64_t> vals;
        size_t end = 0;
        if (!ReadArray(pos, vals, end)) return Fail("invalid psb file.");
        v.BeginArray(vals.size());
        for (uint64_t x : vals) v.Int((int64_t)x);
        v.EndArray();
        return true;
    }
    if (t >= 0x15 && t <= 0x18) {
        if (!Uint(pos + 1, t - 0x14, u) || u >= strings_.size()) return Fail("invalid psb file.");
        v.String(strings_[(size_t)u]);
        return true;
    }
    if (t >= 0x19 && t <= 0x1C) {
        if (!Uint(pos + 1, t - 0x18, u) || u >= chunk_offs_.size()) return Fail("invalid psb file.");
        uint64_t o = chunk_offs_[(size_t)u], l = chunk_lens_[(size_t)u];
        uint64_t base = off_chdata_;
        if (base + o > buf_.size() || l > buf_.size() - (size_t)(base + o)) return Fail("invalid psb file.");
        v.Resource((uint32_t)u, buf_.data() + base + o, (size_t)l);
        return true;
    }
    if (t == 0x1E) {
        if (!Uint(pos + 1, 4, u)) return Fail("invalid psb file.");
        uint32_t bits = (uint32_t)u;
        float f;
        std::memcpy(&f, &bits, 4);
        v.Double(f);
        return true;
    }
    if (t == 0x1F) {
        if (!Uint(pos + 1, 8, u)) return Fail("invalid psb file.");
        double d;
        std::memcpy(&d, &u, 8);
        v.Double(d);
        return true;
    }
    if (t == 0x20) {  // array of values
        std::vector<uint64_t> offs;
        size_t base = 0;
        if (!ReadArray(pos + 1, offs, base)) return Fail("invalid psb file.");
        v.BeginArray(offs.size());
        for (uint64_t o : offs) {
            if (o > buf_.size() || !WalkValue(base + (size_t)o, v, depth + 1)) return error_.empty() ? Fail("invalid psb file.") : false;
        }
        v.EndArray();
        return true;
    }
    if (t == 0x21) {  // object: name indexes + offsets
        std::vector<uint64_t> nidx, offs;
        size_t p = 0, base = 0;
        if (!ReadArray(pos + 1, nidx, p) || !ReadArray(p, offs, base) || nidx.size() != offs.size()) return Fail("invalid psb file.");
        v.BeginObject(nidx.size());
        for (size_t i = 0; i < nidx.size(); i++) {
            if (nidx[i] >= names_.size() || offs[i] > buf_.size()) return Fail("invalid psb file.");
            v.Key(names_[(size_t)nidx[i]]);
            if (!WalkValue(base + (size_t)offs[i], v, depth + 1)) return error_.empty() ? Fail("invalid psb file.") : false;
        }
        v.EndObject();
        return true;
    }
    if (t >= 0x22 && t <= 0x25) {
        if (!Uint(pos + 1, t - 0x21, u)) return Fail("invalid psb file.");
        v.Extra((uint32_t)u);
        return true;
    }
    return Fail("psb: internal error: unknown internal type detected.");
}

}  // namespace psb
}  // namespace kk4s
