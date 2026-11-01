// Generic PC-side CXDEC scheme recovery. This is a runtime port of the data
// analysis in tools/cx_scheme_extract.py: PE/TPM structure + x86 generator
// shape are detected, then candidates must decrypt real archive samples whose
// Adler-32 equals XP3's adlr. No game name, offset, key, or scheme is embedded.
#include "../kk4s_cx.h"
#include "cxdec.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <limits>
#include <set>
#include <string>
#include <vector>

#if defined(KK4S_DESKTOP)
#include <capstone/capstone.h>
#include <capstone/x86.h>
#endif

namespace kk4s {
namespace compat {

#if defined(KK4S_DESKTOP)
namespace {

constexpr uint32_t kCode = 0x20000000;
constexpr size_t kMaxPeImage = 128u * 1024u * 1024u;
constexpr size_t kMaxFile = 256u * 1024u * 1024u;
constexpr uint32_t kLcg = 1103515245u;
const uint8_t kLcgBytes[4] = {0x6d, 0x4e, 0xc6, 0x41};
const char kInternalModule[] = "internal module\0";
const char kControlSignature[] = " Encryption control block";

uint16_t U16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
uint32_t U32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
        (static_cast<uint32_t>(p[1]) << 8) |
        (static_cast<uint32_t>(p[2]) << 16) |
        (static_cast<uint32_t>(p[3]) << 24);
}

bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long size = ftell(f);
    if (size <= 0 || static_cast<unsigned long>(size) > kMaxFile || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    out.resize(static_cast<size_t>(size));
    bool ok = fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    if (!ok) out.clear();
    return ok;
}

bool HasBytes(const std::vector<uint8_t>& bytes, const uint8_t* needle, size_t n) {
    return n <= bytes.size() && std::search(bytes.begin(), bytes.end(), needle, needle + n) != bytes.end();
}

struct Section {
    uint32_t rva = 0;
    uint32_t virtual_size = 0;
    uint32_t raw_size = 0;
    uint32_t raw_offset = 0;
    uint32_t characteristics = 0;
};

struct PeImage {
    uint32_t base = 0;
    uint32_t size_headers = 0;
    std::vector<uint8_t> mapped;
    std::vector<Section> sections;

    bool Open(const std::vector<uint8_t>& raw) {
        if (raw.size() < 0x40 || raw[0] != 'M' || raw[1] != 'Z') return false;
        uint32_t pe = U32(raw.data() + 0x3c);
        if (pe > raw.size() || raw.size() - pe < 24 || U32(raw.data() + pe) != 0x00004550) return false;
        const uint8_t* coff = raw.data() + pe + 4;
        uint16_t section_count = U16(coff + 2);
        uint16_t opt_size = U16(coff + 16);
        size_t opt = static_cast<size_t>(pe) + 24;
        if (!section_count || section_count > 96 || opt > raw.size() || opt_size > raw.size() - opt || opt_size < 64)
            return false;
        const uint8_t* optional = raw.data() + opt;
        if (U16(optional) != 0x10b) return false; // x86 PE32 TPM
        base = U32(optional + 28);
        uint32_t size_image = U32(optional + 56);
        size_headers = U32(optional + 60);
        if (!size_image || size_image > kMaxPeImage || size_headers > raw.size() || size_headers > size_image)
            return false;
        size_t section_table = opt + opt_size;
        if (section_table > raw.size() || static_cast<size_t>(section_count) * 40 > raw.size() - section_table)
            return false;
        mapped.assign(size_image, 0);
        memcpy(mapped.data(), raw.data(), size_headers);
        for (uint16_t i = 0; i < section_count; ++i) {
            const uint8_t* sh = raw.data() + section_table + static_cast<size_t>(i) * 40;
            Section s;
            s.virtual_size = U32(sh + 8);
            s.rva = U32(sh + 12);
            s.raw_size = U32(sh + 16);
            s.raw_offset = U32(sh + 20);
            s.characteristics = U32(sh + 36);
            if (s.rva >= size_image) return false;
            size_t copy = std::min<size_t>(s.raw_size, size_image - s.rva);
            if (s.raw_offset > raw.size() || copy > raw.size() - s.raw_offset) return false;
            if (copy) memcpy(mapped.data() + s.rva, raw.data() + s.raw_offset, copy);
            sections.push_back(s);
        }
        return true;
    }

    bool ContainsData(uint32_t va, size_t size) const {
        if (va < base) return false;
        uint64_t rva = static_cast<uint64_t>(va) - base;
        for (const Section& s : sections) {
            if (s.characteristics & kCode) continue;
            uint64_t extent = std::max(s.virtual_size, s.raw_size);
            if (rva >= s.rva && rva + size <= static_cast<uint64_t>(s.rva) + extent) return true;
        }
        return false;
    }
};

struct TpmSource {
    std::string name;
    std::vector<uint8_t> image;
};

void AddInternalModules(const std::string& name, const std::vector<uint8_t>& file,
                        std::vector<TpmSource>& out) {
    const size_t marker_len = sizeof(kInternalModule) - 1;
    size_t pos = 0;
    while (pos + marker_len < file.size()) {
        auto it = std::search(file.begin() + pos, file.end(),
            reinterpret_cast<const uint8_t*>(kInternalModule),
            reinterpret_cast<const uint8_t*>(kInternalModule) + marker_len);
        if (it == file.end()) break;
        size_t p = static_cast<size_t>(it - file.begin()) + marker_len;
        bool found = false;
        for (size_t skip = 0; skip <= 32 && p + skip + 8 <= file.size(); skip += 4) {
            uint32_t raw_size = U32(file.data() + p + skip);
            uint32_t z_size = U32(file.data() + p + skip + 4);
            size_t zpos = p + skip + 8;
            if (!raw_size || raw_size > kMaxPeImage || !z_size || z_size > file.size() - zpos) continue;
            std::vector<uint8_t> image(raw_size);
            uLongf out_size = raw_size;
            int zr = uncompress(image.data(), &out_size, file.data() + zpos, z_size);
            if (zr == Z_OK && out_size == raw_size && raw_size >= 2 && image[0] == 'M' && image[1] == 'Z') {
                if (HasBytes(image, kLcgBytes, sizeof(kLcgBytes))) {
                    out.push_back({name + "#internal", std::move(image)});
                    found = true;
                    break;
                }
            }
        }
        pos = p;
        if (found && pos < file.size()) continue; // multiple modules may be embedded
    }
}

std::string Lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return s;
}

void ScanDirectory(const std::string& dir, std::vector<TpmSource>& out, bool root) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        std::string entry = e->d_name;
        std::string full = dir + "/" + entry;
        std::string lower = Lower(entry);
        if (root && lower == "plugin") {
            ScanDirectory(full, out, false);
            continue;
        }
        bool tpm = lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".tpm") == 0;
        bool pe_container = lower.size() > 4 &&
            (lower.compare(lower.size() - 4, 4, ".exe") == 0 ||
             lower.compare(lower.size() - 4, 4, ".dll") == 0);
        if (!tpm && !pe_container) continue;
        std::vector<uint8_t> bytes;
        if (!ReadFile(full, bytes)) continue;
        if (tpm) out.push_back({full, std::move(bytes)});
        else AddInternalModules(full, bytes, out);
    }
    closedir(d);
}

struct Insn {
    uint64_t address = 0;
    unsigned id = 0;
    std::string mnemonic;
    std::string operands;
    std::vector<cs_x86_op> ops;
};

class TpmAnalyzer {
public:
    explicit TpmAnalyzer(const PeImage& pe) : pe_(pe) {
        if (cs_open(CS_ARCH_X86, CS_MODE_32, &handle_) != CS_ERR_OK) return;
        cs_option(handle_, CS_OPT_DETAIL, CS_OPT_ON);
        valid_ = true;
    }
    ~TpmAnalyzer() { if (valid_) cs_close(&handle_); }
    bool valid() const { return valid_; }

    std::vector<std::vector<uint8_t>> ControlBlocks() const {
        std::vector<std::vector<uint8_t>> out;
        const auto& mem = pe_.mapped;
        auto sig = std::search(mem.begin(), mem.end(),
            reinterpret_cast<const uint8_t*>(kControlSignature),
            reinterpret_cast<const uint8_t*>(kControlSignature) + sizeof(kControlSignature) - 1);
        if (sig != mem.end()) {
            size_t at = static_cast<size_t>(sig - mem.begin());
            if (mem.size() - at >= 4096) out.emplace_back(mem.begin() + at, mem.begin() + at + 4096);
        }
        for (const Section& s : pe_.sections) {
            if (!(s.characteristics & kCode)) continue;
            size_t n = std::min<size_t>(std::max(s.virtual_size, s.raw_size), mem.size() - s.rva);
            const uint8_t* code = mem.data() + s.rva;
            for (size_t i = 0; i + 6 <= n; ++i) {
                if (code[i] != 0xb8 || code[i + 5] != 0xc3) continue;
                uint32_t va = U32(code + i + 1);
                if (!pe_.ContainsData(va, 4096) || (va & 3)) continue;
                size_t rva = static_cast<size_t>(va - pe_.base);
                std::vector<uint8_t> cb(mem.begin() + rva, mem.begin() + rva + 4096);
                if (std::find(out.begin(), out.end(), cb) == out.end()) out.push_back(std::move(cb));
            }
        }
        return out;
    }

    std::vector<std::pair<uint32_t, uint32_t>> MaskOffsets() const {
        std::vector<std::pair<uint32_t, uint32_t>> out;
        for (const Section& s : pe_.sections) {
            if (!(s.characteristics & kCode)) continue;
            size_t n = std::min<size_t>(std::max(s.virtual_size, s.raw_size), pe_.mapped.size() - s.rva);
            const uint8_t* p = pe_.mapped.data() + s.rva;
            for (size_t i = 0; i + 12 <= n; ++i) {
                if (p[i] != 0x81 || p[i + 1] < 0xe0 || p[i + 1] > 0xe7 ||
                    p[i + 6] != 0x81 || p[i + 7] < 0xc0 || p[i + 7] > 0xc7 ||
                    ((p[i + 1] & 7) != (p[i + 7] & 7))) continue;
                uint32_t mask = U32(p + i + 2), offset = U32(p + i + 8);
                if (mask >= 0x10000 || offset >= 0x100000) continue;
                auto v = std::make_pair(mask, offset);
                if (std::find(out.begin(), out.end(), v) == out.end()) out.push_back(v);
            }
        }
        return out;
    }

    bool Orders(std::vector<uint8_t>& prolog, std::vector<uint8_t>& odd,
                std::vector<uint8_t>& even, std::string& error) {
        bool have_prolog = false, have_odd = false, have_even = false;
        for (const Section& s : pe_.sections) {
            if (!(s.characteristics & kCode)) continue;
            size_t n = std::min<size_t>(std::max(s.virtual_size, s.raw_size), pe_.mapped.size() - s.rva);
            auto ins = Disassemble(pe_.mapped.data() + s.rva, n, pe_.base + s.rva);
            if (ins.empty()) continue;
            for (size_t k = 0; k < ins.size(); ++k) {
                if (IsJumpTable(ins[k])) {
                    size_t first = k > 8 ? k - 8 : 0;
                    size_t count = 0;
                    bool div = false, has_six = false, has_and7 = false, has_lcg = false;
                    uint64_t stop = 0;
                    for (size_t j = first; j < k; ++j) {
                        div |= ins[j].id == X86_INS_DIV;
                        has_six |= HasImmediateMove(ins[j], 6);
                        has_and7 |= HasImmediateOp(ins[j], X86_INS_AND, 7);
                        if (ins[j].id == X86_INS_IMUL && HasImmediate(ins[j], kLcg)) has_lcg = true;
                        if (ins[j].id == X86_INS_JA && DirectTarget(ins[j], stop)) {}
                    }
                    const char* kind = nullptr;
                    if (div && has_six) { kind = "odd"; count = 6; }
                    else {
                        size_t f = k > 16 ? k - 16 : 0;
                        for (size_t j = f; j < k; ++j)
                            if (ins[j].id == X86_INS_IMUL && HasImmediate(ins[j], kLcg)) has_lcg = true;
                        if (has_and7 && has_lcg) { kind = "even"; count = 8; }
                    }
                    if (kind) {
                        std::vector<uint32_t> targets;
                        if (!ReadTable(ins[k], count, targets)) continue;
                        std::vector<uint8_t> order;
                        for (uint32_t va : targets) order.push_back(Classify(kind, Emitted(va, stop, 40)));
                        if (IsPermutation(order)) {
                            if (!strcmp(kind, "odd")) { odd = std::move(order); have_odd = true; }
                            else { even = std::move(order); have_even = true; }
                        }
                    }
                }
                if (ins[k].id == X86_INS_DIV && HasDivisor3(ins, k)) {
                    std::vector<uint64_t> targets;
                    for (size_t j = k + 1; j < ins.size() && targets.size() < 3; ++j) {
                        uint64_t target = 0;
                        if (ins[j].id == X86_INS_JE && DirectTarget(ins[j], target)) targets.push_back(target);
                        else if (ins[j].id == X86_INS_JNE && DirectTarget(ins[j], target)) {
                            if (j + 1 < ins.size()) targets.push_back(ins[j + 1].address);
                            break;
                        }
                        if (IsReturn(ins[j])) break;
                    }
                    if (targets.size() == 3) {
                        std::vector<uint8_t> order;
                        for (uint64_t va : targets) order.push_back(Classify("prolog", Emitted(va, 0, 25)));
                        if (IsPermutation(order)) { prolog = std::move(order); have_prolog = true; }
                    } else {
                        for (size_t j = k; j < ins.size() && j < k + 6; ++j) {
                            if (!IsJumpTable(ins[j])) continue;
                            std::vector<uint32_t> table;
                            if (!ReadTable(ins[j], 3, table)) continue;
                            std::vector<uint8_t> order;
                            for (uint32_t va : table) order.push_back(Classify("prolog", Emitted(va, 0, 25)));
                            if (IsPermutation(order)) { prolog = std::move(order); have_prolog = true; }
                            break;
                        }
                    }
                }
            }
        }
        if (have_prolog && have_odd && have_even) return true;
        error = "could not recover all CXDEC branch orders";
        return false;
    }

private:
    std::vector<Insn> Disassemble(const uint8_t* code, size_t size, uint64_t address) const {
        std::vector<Insn> out;
        cs_insn* raw = nullptr;
        size_t n = cs_disasm(handle_, code, size, address, 0, &raw);
        out.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            Insn in;
            in.address = raw[i].address;
            in.id = raw[i].id;
            in.mnemonic = raw[i].mnemonic;
            in.operands = raw[i].op_str;
            if (raw[i].detail) {
                const cs_x86& x = raw[i].detail->x86;
                for (uint8_t j = 0; j < x.op_count; ++j) in.ops.push_back(x.operands[j]);
            }
            out.push_back(std::move(in));
        }
        if (raw) cs_free(raw, n);
        return out;
    }
    static bool HasImmediate(const Insn& in, uint32_t value) {
        for (const auto& op : in.ops)
            if (op.type == X86_OP_IMM && static_cast<uint32_t>(op.imm) == value) return true;
        return false;
    }
    static bool HasImmediateMove(const Insn& in, uint32_t value) {
        return in.id == X86_INS_MOV && HasImmediate(in, value);
    }
    static bool HasImmediateOp(const Insn& in, unsigned id, uint32_t value) {
        return in.id == id && HasImmediate(in, value);
    }
    static bool DirectTarget(const Insn& in, uint64_t& target) {
        if (!in.ops.empty() && in.ops[0].type == X86_OP_IMM) {
            target = static_cast<uint64_t>(in.ops[0].imm);
            return true;
        }
        return false;
    }
    static bool IsJumpTable(const Insn& in) {
        if (in.id != X86_INS_JMP || in.ops.size() != 1 || in.ops[0].type != X86_OP_MEM) return false;
        const x86_op_mem& m = in.ops[0].mem;
        return m.scale == 4 && m.disp > 0 && m.index != X86_REG_INVALID;
    }
    static bool IsReturn(const Insn& in) { return in.id == X86_INS_RET || in.id == X86_INS_RETF; }
    static bool HasDivisor3(const std::vector<Insn>& ins, size_t k) {
        size_t first = k > 12 ? k - 12 : 0;
        for (size_t i = first; i < k; ++i) {
            if (ins[i].id != X86_INS_MOV || ins[i].ops.size() < 2 ||
                ins[i].ops[0].type != X86_OP_REG || ins[i].ops[0].reg != X86_REG_ECX ||
                ins[i].ops[1].type != X86_OP_IMM || ins[i].ops[1].imm != 3) continue;
            return true;
        }
        return false;
    }
    bool ReadTable(const Insn& jump, size_t count, std::vector<uint32_t>& out) const {
        if (!IsJumpTable(jump)) return false;
        int64_t disp = jump.ops[0].mem.disp;
        if (disp < pe_.base) return false;
        uint64_t rva = static_cast<uint64_t>(disp) - pe_.base;
        if (rva > pe_.mapped.size() || count * 4 > pe_.mapped.size() - static_cast<size_t>(rva)) return false;
        out.clear();
        for (size_t i = 0; i < count; ++i) out.push_back(U32(pe_.mapped.data() + rva + i * 4));
        return true;
    }
    std::vector<uint32_t> Emitted(uint64_t va, uint64_t stop, size_t limit) const {
        std::vector<uint32_t> values;
        std::set<uint64_t> seen;
        for (unsigned hop = 0; hop < 4; ++hop) {
            if (va < pe_.base || va - pe_.base >= pe_.mapped.size()) break;
            size_t rva = static_cast<size_t>(va - pe_.base);
            auto ins = Disassemble(pe_.mapped.data() + rva,
                std::min<size_t>(0x200, pe_.mapped.size() - rva), va);
            bool jumped = false;
            for (const Insn& in : ins) {
                if (seen.count(in.address) || in.address == stop || seen.size() > limit) return values;
                seen.insert(in.address);
                if (!in.ops.empty() && in.id == X86_INS_PUSH && in.ops[0].type == X86_OP_IMM)
                    values.push_back(static_cast<uint32_t>(in.ops[0].imm));
                if (in.id == X86_INS_MOV && in.ops.size() >= 2 && in.ops[0].type == X86_OP_MEM &&
                    in.ops[1].type == X86_OP_IMM && (in.ops[0].size == 1 || in.ops[0].size == 4))
                    values.push_back(static_cast<uint32_t>(in.ops[1].imm));
                if (in.id == X86_INS_JMP) {
                    uint64_t target = 0;
                    if (DirectTarget(in, target)) { va = target; jumped = true; break; }
                    return values;
                }
                if (IsReturn(in)) return values;
            }
            if (!jumped) return values;
        }
        return values;
    }
    static uint8_t Classify(const char* kind, const std::vector<uint32_t>& vals) {
        std::set<uint32_t> s(vals.begin(), vals.end());
        if (!strcmp(kind, "prolog")) {
            if (s.count(0xbe)) return 2;
            if (s.count(0xc7)) return 1;
            if (s.count(0xb8)) return 0;
        } else if (!strcmp(kind, "odd")) {
            if (s.count(0xd3) && s.count(0xe8)) return 0;
            if (s.count(0xd3) && s.count(0xe0)) return 1;
            if (s.count(0xaf)) return 4;
            if (s.count(0x29)) return 5;
            if (s.count(0xf7)) return 3;
            if (s.count(0x01)) return 2;
        } else {
            if (s.count(0xaaaaaaaa) || s.count(0xd1)) return 5;
            if (s.count(0xbe) || s.count(0x3ff)) return 4;
            if (s.count(0x35)) return 6;
            if (s.count(0x05) || s.count(0x2d)) return 7;
            if (s.count(0x48)) return 1;
            if (s.count(0x40)) return 3;
            if (s.count(0xf7) && s.count(0xd0)) return 0;
            if (s.count(0xf7) && s.count(0xd8)) return 2;
        }
        return 0xff;
    }
    static bool IsPermutation(const std::vector<uint8_t>& v) {
        std::vector<uint8_t> s = v;
        if (std::find(s.begin(), s.end(), 0xff) != s.end()) return false;
        std::sort(s.begin(), s.end());
        for (size_t i = 0; i < s.size(); ++i) if (s[i] != i) return false;
        return true;
    }

    const PeImage& pe_;
    csh handle_ = 0;
    bool valid_ = false;
};

int Verify(cxdec::CxScheme& scheme, const std::vector<CxSample>& samples) {
    cxdec::CxDecoder decoder(scheme);
    int good = 0;
    for (const CxSample& sample : samples) {
        if (sample.raw.empty()) continue;
        std::vector<uint8_t> plain = sample.raw;
        decoder.Decrypt(sample.adlr, 0, plain.data(), plain.size());
        if (static_cast<uint32_t>(adler32(1, plain.data(), static_cast<uInt>(plain.size()))) == sample.adlr)
            ++good;
    }
    return good;
}

} // namespace

bool RecoverCxSchemeFromGame(const std::string& game_dir, const std::vector<CxSample>& samples,
                             cxdec::CxScheme& scheme, std::string& error) {
    std::vector<TpmSource> sources;
    ScanDirectory(game_dir, sources, true);
    if (sources.empty()) { error = "no CX TPM or embedded internal module found"; return false; }
    int best_ok = 0;
    // Keep the same acceptance threshold as BindCxDecoder: ≥80%, with at
    // least three checks where that many samples exist (but do not reject a
    // tiny archive merely because it contains fewer than four candidates).
    int required = static_cast<int>(samples.size()) * 4 / 5;
    if (required < 3) required = std::min(static_cast<int>(samples.size()), 3);
    for (const TpmSource& src : sources) {
        PeImage pe;
        if (!pe.Open(src.image) || !HasBytes(pe.mapped, kLcgBytes, sizeof(kLcgBytes))) continue;
        TpmAnalyzer analyzer(pe);
        if (!analyzer.valid()) continue;
        std::vector<uint8_t> prolog, odd, even;
        std::string why;
        if (!analyzer.Orders(prolog, odd, even, why)) continue;
        auto masks = analyzer.MaskOffsets();
        auto blocks = analyzer.ControlBlocks();
        for (auto [mask, offset] : masks) for (const auto& cb : blocks) {
            cxdec::CxScheme candidate;
            candidate.mask = mask;
            candidate.offset = offset;
            candidate.name = "auto:" + src.name;
            std::copy(prolog.begin(), prolog.end(), candidate.prolog_order);
            std::copy(odd.begin(), odd.end(), candidate.odd_branch_order);
            std::copy(even.begin(), even.end(), candidate.even_branch_order);
            if (cb.size() != sizeof(candidate.control_block) || !candidate.IsValid()) continue;
            memcpy(candidate.control_block, cb.data(), cb.size());
            int good = Verify(candidate, samples);
            if (good > best_ok) best_ok = good;
            if (good >= required) {
                scheme = candidate;
                return true;
            }
        }
    }
    error = "no recovered CX scheme validated (best " + std::to_string(best_ok) + "/" +
        std::to_string(samples.size()) + ")";
    return false;
}

#else

bool RecoverCxSchemeFromGame(const std::string&, const std::vector<CxSample>&,
                             cxdec::CxScheme&, std::string& error) {
    error = "automatic CX scheme recovery is available on desktop builds only";
    return false;
}

#endif

} // namespace compat
} // namespace kk4s
