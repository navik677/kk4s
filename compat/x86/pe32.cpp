// kk4s: PE32 DLL loader for the x86 interpreter. See pe32.h.
#include "pe32.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace kk4s {
namespace x86 {

namespace {

[[noreturn]] void Bad(const std::string& why) { throw Fault{"not a loadable PE32 DLL: " + why}; }

uint16_t U16(const std::vector<uint8_t>& f, size_t o) {
    if (o + 2 > f.size()) Bad("truncated header");
    return (uint16_t)(f[o] | (f[o + 1] << 8));
}
uint32_t U32(const std::vector<uint8_t>& f, size_t o) {
    if (o + 4 > f.size()) Bad("truncated header");
    return (uint32_t)f[o] | ((uint32_t)f[o + 1] << 8) | ((uint32_t)f[o + 2] << 16) | ((uint32_t)f[o + 3] << 24);
}

std::string Lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

}  // namespace

uint32_t PeImage::Export(const std::string& name) const {
    auto it = exports.find(name);
    if (it != exports.end()) return it->second;
    for (const auto& e : exports)  // _V2Link@4 and friends
        if (e.first.size() > name.size() + 1 && e.first[0] == '_' && e.first.compare(1, name.size(), name) == 0 &&
            e.first[name.size() + 1] == '@')
            return e.second;
    return 0;
}

PeImage LoadPe32(Machine& m, const std::vector<uint8_t>& f, const ImportResolver& resolve) {
    if (f.size() < 0x40 || f[0] != 'M' || f[1] != 'Z') Bad("no MZ header");
    uint32_t pe = U32(f, 0x3C);
    if (U32(f, pe) != 0x00004550) Bad("no PE signature");
    if (U16(f, pe + 4) != 0x014C) Bad("not i386");
    uint16_t nsect = U16(f, pe + 6);
    uint16_t optsize = U16(f, pe + 20);
    uint16_t characteristics = U16(f, pe + 22);
    size_t opt = pe + 24;
    if (U16(f, opt) != 0x010B) Bad("not PE32");
    if (!(characteristics & 0x2000)) Bad("not a DLL");
    uint32_t entry_rva = U32(f, opt + 16);
    uint32_t pref_base = U32(f, opt + 28);
    uint32_t size_image = U32(f, opt + 56);
    uint32_t size_headers = U32(f, opt + 60);
    uint32_t ndirs = U32(f, opt + 92);
    auto dir = [&](uint32_t i, uint32_t& rva, uint32_t& size) {
        rva = size = 0;
        if (i < ndirs) {
            rva = U32(f, opt + 96 + 8 * i);
            size = U32(f, opt + 96 + 8 * i + 4);
        }
    };
    if (!size_image || size_image > (64u << 20)) Bad("image size");

    // where to put it: the preferred base when free, else somewhere low
    // keep clear of the interpreter's own heap, thunks, stack and TIB
    auto usable = [&](uint32_t b) {
        uint64_t e = (uint64_t)b + size_image;
        bool hits_heap = b < Machine::kHeapLimit && e > Machine::kHeapBase;
        return b && !(b & 0xFFFF) && e <= Machine::kHostBase && !hits_heap && m.mem.IsRangeFree(b, size_image);
    };
    uint32_t base = pref_base;
    if (!usable(base)) {
        base = 0;
        for (uint32_t b = 0x10000000; b < 0x3F000000; b += 0x01000000)
            if (usable(b)) { base = b; break; }
        if (!base) Bad("no room for the image");
    }
    m.mem.Map(base, size_image);
    m.mem.Write(base, f.data(), std::min<size_t>(std::min<size_t>(size_headers, f.size()), size_image));

    size_t sect = opt + optsize;
    for (uint16_t i = 0; i < nsect; ++i, sect += 40) {
        uint32_t vsize = U32(f, sect + 8), va = U32(f, sect + 12);
        uint32_t rawsize = U32(f, sect + 16), rawptr = U32(f, sect + 20);
        uint32_t n = std::min(rawsize, vsize ? vsize : rawsize);
        if ((uint64_t)va + n > size_image) Bad("section outside the image");
        if (n && rawptr < f.size()) m.mem.Write(base + va, f.data() + rawptr, std::min<size_t>(n, f.size() - rawptr));
    }

    // relocations
    uint32_t rva, size;
    if (base != pref_base) {
        dir(5, rva, size);
        if (!rva) Bad("the preferred base is taken and there are no relocations");
        uint32_t delta = base - pref_base;
        uint32_t p = rva, end = rva + size;
        while (p + 8 <= end) {
            uint32_t page = m.mem.Read32(base + p), block = m.mem.Read32(base + p + 4);
            if (block < 8) break;
            for (uint32_t k = 8; k + 2 <= block; k += 2) {
                uint16_t e = m.mem.Read16(base + p + k);
                int type = e >> 12;
                uint32_t at = base + page + (e & 0xFFF);
                if (type == 3) m.mem.Write32(at, m.mem.Read32(at) + delta);
                else if (type != 0) Bad("relocation type " + std::to_string(type));
            }
            p += block;
        }
    }

    PeImage img;
    img.base = base;
    img.size = size_image;
    img.entry = entry_rva ? base + entry_rva : 0;

    // imports
    dir(1, rva, size);
    if (rva) {
        for (uint32_t d = base + rva;; d += 20) {
            uint32_t oft = m.mem.Read32(d), name_rva = m.mem.Read32(d + 12), ft = m.mem.Read32(d + 16);
            if (!name_rva && !ft) break;
            std::string dll = Lower(m.mem.ReadCString(base + name_rva, 260));
            uint32_t lookup = oft ? oft : ft;
            for (uint32_t k = 0;; ++k) {
                uint32_t t = m.mem.Read32(base + lookup + 4 * k);
                if (!t) break;
                std::string name;
                uint16_t ordinal = 0;
                if (t & 0x80000000u) ordinal = (uint16_t)t;
                else name = m.mem.ReadCString(base + t + 2, 512);
                img.imports.push_back(dll + "!" + (name.empty() ? "#" + std::to_string(ordinal) : name));
                m.mem.Write32(base + ft + 4 * k, resolve(dll, name, ordinal));
            }
        }
    }

    // exports
    dir(0, rva, size);
    if (rva) {
        uint32_t e = base + rva;
        uint32_t nfuncs = m.mem.Read32(e + 20), nnames = m.mem.Read32(e + 24);
        uint32_t funcs = base + m.mem.Read32(e + 28), names = base + m.mem.Read32(e + 32);
        uint32_t ords = base + m.mem.Read32(e + 36);
        for (uint32_t i = 0; i < nnames && i < 65536; ++i) {
            std::string n = m.mem.ReadCString(base + m.mem.Read32(names + 4 * i), 512);
            uint16_t o = m.mem.Read16(ords + 2 * i);
            if (o < nfuncs) {
                uint32_t frva = m.mem.Read32(funcs + 4 * o);
                // a forwarder points inside the export directory: not supported, skip
                if (frva >= rva && frva < rva + size) continue;
                img.exports[n] = base + frva;
            }
        }
    }

    dir(9, rva, size);
    img.has_tls = rva != 0;
    return img;
}

}  // namespace x86
}  // namespace kk4s
