// Synthetic PSB writer for the E-mote runtime tests (no game data).
//
// Encodes an emote::Value tree as a PSB container with the section order seen
// in real E-mote files: header (0x28), name trie, root value, string offsets,
// string data, chunk offsets, chunk lengths, chunk data. The value encoding and
// the name trie follow tests/psb/run_tests.py (the reference writer for
// compat/psb).
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "emote/psb_value.h"

namespace psbw {

using kk4s::emote::Value;
using Bytes = std::vector<uint8_t>;

inline unsigned MinBytes(uint64_t v) {
    unsigned n = 1;
    while (n < 8 && (v >> (8 * n)) != 0) n++;
    return n;
}

inline void PutLE(Bytes& b, uint64_t v, unsigned n) {
    for (unsigned i = 0; i < n; i++) b.push_back((uint8_t)(v >> (8 * i)));
}

inline Bytes EncArray(const std::vector<uint64_t>& vals) {
    Bytes b;
    unsigned cw = MinBytes(vals.size());
    unsigned ew = 1;
    for (uint64_t v : vals) ew = std::max(ew, MinBytes(v));
    b.push_back((uint8_t)(0x0C + cw));
    PutLE(b, vals.size(), cw);
    b.push_back((uint8_t)(0x0C + ew));
    for (uint64_t v : vals) PutLE(b, v, ew);
    return b;
}

class Writer {
public:
    std::vector<std::string> strings, names;
    std::vector<Bytes> resources;

    size_t StringIndex(const std::string& s) {
        for (size_t i = 0; i < strings.size(); i++)
            if (strings[i] == s) return i;
        strings.push_back(s);
        return strings.size() - 1;
    }
    size_t NameIndex(const std::string& s) {
        for (size_t i = 0; i < names.size(); i++)
            if (names[i] == s) return i;
        names.push_back(s);
        return names.size() - 1;
    }

    Bytes Encode(const Value& v) {
        Bytes b;
        switch (v.type()) {
        case Value::Type::Undefined: b.push_back(0x00); break;
        case Value::Type::Null: b.push_back(0x01); break;
        case Value::Type::Bool: b.push_back(v.bool_value() ? 0x03 : 0x02); break;
        case Value::Type::Int: {
            int64_t i = v.int_value();
            if (i == 0) {
                b.push_back(0x04);
                break;
            }
            unsigned n = 1;
            while (n < 8) {
                int64_t lo = -((int64_t)1 << (8 * n - 1)), hi = ((int64_t)1 << (8 * n - 1)) - 1;
                if (i >= lo && i <= hi) break;
                n++;
            }
            b.push_back((uint8_t)(4 + n));
            PutLE(b, (uint64_t)i, n);
            break;
        }
        case Value::Type::Double: {
            double d = v.double_value();
            if (d == 0.0) {
                b.push_back(0x1D);
                break;
            }
            float f = (float)d;
            if ((double)f == d) {
                uint32_t bits;
                std::memcpy(&bits, &f, 4);
                b.push_back(0x1E);
                PutLE(b, bits, 4);
            } else {
                uint64_t bits;
                std::memcpy(&bits, &d, 8);
                b.push_back(0x1F);
                PutLE(b, bits, 8);
            }
            break;
        }
        case Value::Type::String: {
            size_t idx = StringIndex(v.string_value());
            unsigned n = MinBytes(idx);
            b.push_back((uint8_t)(0x14 + n));
            PutLE(b, idx, n);
            break;
        }
        case Value::Type::Resource: {
            unsigned n = MinBytes(v.resource_index());
            b.push_back((uint8_t)(0x18 + n));
            PutLE(b, v.resource_index(), n);
            break;
        }
        case Value::Type::Extra: {
            unsigned n = MinBytes(v.resource_index());
            b.push_back((uint8_t)(0x21 + n));
            PutLE(b, v.resource_index(), n);
            break;
        }
        case Value::Type::Array: {
            std::vector<Bytes> items;
            std::vector<uint64_t> offs;
            uint64_t pos = 0;
            for (const Value& x : v.items()) {
                items.push_back(Encode(x));
                offs.push_back(pos);
                pos += items.back().size();
            }
            b.push_back(0x20);
            Bytes a = EncArray(offs);
            b.insert(b.end(), a.begin(), a.end());
            for (const Bytes& it : items) b.insert(b.end(), it.begin(), it.end());
            break;
        }
        case Value::Type::Object: {
            std::vector<Bytes> items;
            std::vector<uint64_t> offs, nidx;
            uint64_t pos = 0;
            for (const Value::Member& m : v.members()) {
                nidx.push_back(NameIndex(m.first));
                items.push_back(Encode(m.second));
                offs.push_back(pos);
                pos += items.back().size();
            }
            b.push_back(0x21);
            Bytes a = EncArray(nidx), c = EncArray(offs);
            b.insert(b.end(), a.begin(), a.end());
            b.insert(b.end(), c.begin(), c.end());
            for (const Bytes& it : items) b.insert(b.end(), it.begin(), it.end());
            break;
        }
        }
        return b;
    }

    // Trie: child id = charset[parent] + byte; tree[child] = parent; byte 0 ends a name.
    Bytes NameTables() {
        std::map<uint64_t, uint64_t> charset, tree;
        std::map<std::pair<uint64_t, unsigned>, uint64_t> child;
        uint64_t next = 512;
        charset[0] = 256;
        auto get = [&](uint64_t parent, unsigned ch) {
            auto key = std::make_pair(parent, ch);
            auto it = child.find(key);
            if (it != child.end()) return it->second;
            if (!charset.count(parent)) {
                charset[parent] = next;
                next += 256;
            }
            uint64_t cid = charset[parent] + ch;
            tree[cid] = parent;
            child[key] = cid;
            return cid;
        };
        std::vector<uint64_t> leaves;
        for (const std::string& n : names) {
            uint64_t node = 0;
            for (unsigned char c : n) node = get(node, c);
            leaves.push_back(get(node, 0));
        }
        uint64_t size = 1;
        if (!tree.empty()) size = std::max(size, tree.rbegin()->first + 1);
        size = std::max(size, charset.rbegin()->first + 1);
        std::vector<uint64_t> cs(size, 0), tr(size, 0);
        for (auto& kv : charset) cs[kv.first] = kv.second;
        for (auto& kv : tree) tr[kv.first] = kv.second;
        Bytes b = EncArray(cs), t = EncArray(tr), l = EncArray(leaves);
        b.insert(b.end(), t.begin(), t.end());
        b.insert(b.end(), l.begin(), l.end());
        return b;
    }
};

struct Built {
    Bytes bytes;
    uint32_t header_len = 0, off_chunk_offsets = 0;
};

// Plain (unencrypted) container in the E-mote section order.
inline Built Build(const Value& root, uint16_t version = 2, const std::vector<Bytes>& resources = {}) {
    Writer w;
    w.resources = resources;
    Bytes entries = w.Encode(root);
    Bytes names = w.NameTables();
    std::vector<uint64_t> soffs;
    Bytes sdata;
    for (const std::string& s : w.strings) {
        soffs.push_back(sdata.size());
        sdata.insert(sdata.end(), s.begin(), s.end());
        sdata.push_back(0);
    }
    Bytes strarr = EncArray(soffs);
    std::vector<uint64_t> coffs, clens;
    Bytes cdata;
    for (const Bytes& r : w.resources) {
        coffs.push_back(cdata.size());
        clens.push_back(r.size());
        cdata.insert(cdata.end(), r.begin(), r.end());
    }
    Bytes ca = EncArray(coffs), cl = EncArray(clens);
    uint32_t hdr = 0x28;
    uint32_t o_names = hdr;
    uint32_t o_entries = o_names + (uint32_t)names.size();
    uint32_t o_str = o_entries + (uint32_t)entries.size();
    uint32_t o_sdata = o_str + (uint32_t)strarr.size();
    uint32_t o_choffs = o_sdata + (uint32_t)sdata.size();
    uint32_t o_chlens = o_choffs + (uint32_t)ca.size();
    uint32_t o_chdata = o_chlens + (uint32_t)cl.size();
    Built out;
    Bytes& b = out.bytes;
    b.insert(b.end(), {'P', 'S', 'B', 0});
    PutLE(b, version, 2);
    PutLE(b, 0, 2);
    for (uint32_t f : {hdr, o_names, o_str, o_sdata, o_choffs, o_chlens, o_chdata, o_entries}) PutLE(b, f, 4);
    for (const Bytes* part : {&names, &entries, &strarr, &sdata, &ca, &cl, &cdata}) b.insert(b.end(), part->begin(), part->end());
    out.header_len = hdr;
    out.off_chunk_offsets = o_choffs;
    return out;
}

// ---- small Value construction helpers
inline Value S(const std::string& s) { return Value::MakeString(s); }
inline Value I(int64_t i) { return Value::MakeInt(i); }
inline Value D(double d) { return Value::MakeDouble(d); }
inline Value N() { return Value::MakeNull(); }
inline Value R(uint32_t i) { return Value::MakeResource(i); }
inline Value A(std::vector<Value> items) {
    Value v = Value::MakeArray();
    v.items() = std::move(items);
    return v;
}
inline Value O(std::vector<Value::Member> members) {
    Value v = Value::MakeObject();
    v.members() = std::move(members);
    return v;
}

}  // namespace psbw
