// kk4s: generic CXDEC implementation. See cxdec.h and docs/cxdec.md.
//
// The TPM generates 128 small x86 functions at run time. kk4s generates the same
// programs as a list of operations and interprets them (no JIT, so it runs on
// Switch/Linux/Windows alike). The generator must follow the original exactly,
// including the order of length checks and random draws: an opcode is checked
// first, and the random immediate is drawn only after the opcode fitted.
#include "cxdec.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace kk4s {
namespace cxdec {

namespace {

enum Op : uint8_t {
    MOV_EDI_ARG, PUSH_EBX, POP_EBX, PUSH_ECX, POP_ECX,
    MOV_EBX_EAX, MOV_EAX_EDI, MOV_ECX_EBX,
    AND_ECX_0F, SHR_EBX_1, SHL_EAX_1, SHR_EAX_CL, SHL_EAX_CL, OR_EAX_EBX,
    NOT_EAX, NEG_EAX, DEC_EAX, INC_EAX, ADD_EAX_EBX, SUB_EAX_EBX, IMUL_EAX_EBX,
    MOV_EAX_INDIRECT,
    MOV_EAX_IMMED, AND_EBX_IMMED, AND_EAX_IMMED, XOR_EAX_IMMED, ADD_EAX_IMMED, SUB_EAX_IMMED,
    RETN,
};

const int kLengthLimit = 0x80;  // size of one generated x86 function in bytes

}  // namespace

struct CxDecoder::Program {
    struct Ins { uint8_t op; uint32_t imm; };
    std::vector<Ins> code;
    bool ok = false;
};

namespace {

// Builds the program for one seed. Lengths are those of the x86 encoding, because
// the 128-byte limit decides when a stage is abandoned.
class Generator {
public:
    Generator(const CxScheme& s, uint32_t seed) : s_(s), seed_(seed) {}

    bool Build(CxDecoder::Program& p) {
        for (int stage = 5; stage > 0; --stage) {  // the RNG is not reset between stages
            code_.clear();
            len_ = 0;
            if (EmitNop(5) && Emit(MOV_EDI_ARG, 4) && Body(stage) && EmitNop(5) && Emit(RETN, 1)) {
                p.code = code_;
                p.ok = true;
                return true;
            }
        }
        return false;
    }

private:
    uint32_t Random() {
        uint32_t old = seed_;
        seed_ = 1103515245u * old + 12345u;
        return seed_ ^ (old << 16) ^ (old >> 16);
    }
    bool EmitNop(int n) {
        if (len_ + n > kLengthLimit) return false;
        len_ += n;
        return true;
    }
    bool Emit(uint8_t op, int n) {
        if (len_ + n > kLengthLimit) return false;
        len_ += n;
        code_.push_back({op, 0});
        return true;
    }
    bool EmitImm(uint32_t v) {  // EmitUInt32: patches the immediate of the last op
        if (len_ + 4 > kLengthLimit) return false;
        len_ += 4;
        code_.back().imm = v;
        return true;
    }
    bool EmitRandom() { return EmitImm(Random()); }

    bool Body(int stage) {
        if (stage == 1) return Prolog();
        if (!Emit(PUSH_EBX, 1)) return false;
        if (!((Random() & 1) ? Body(stage - 1) : Body2(stage - 1))) return false;
        if (!Emit(MOV_EBX_EAX, 2)) return false;
        if (!((Random() & 1) ? Body(stage - 1) : Body2(stage - 1))) return false;
        return OddBranch() && Emit(POP_EBX, 1);
    }
    bool Body2(int stage) {
        if (stage == 1) return Prolog();
        if (!((Random() & 1) ? Body(stage - 1) : Body2(stage - 1))) return false;
        return EvenBranch();
    }
    bool Prolog() {
        switch (s_.prolog_order[Random() % 3]) {
        case 2:  // mov esi, control_block ; mov eax, [esi + (Random() & 0x3ff) * 4]
            return EmitNop(5) && Emit(MOV_EAX_IMMED, 2) && EmitImm(Random() & 0x3ff) && Emit(MOV_EAX_INDIRECT, 0);
        case 1:
            return Emit(MOV_EAX_EDI, 2);
        default:
            return Emit(MOV_EAX_IMMED, 1) && EmitRandom();
        }
    }
    bool EvenBranch() {
        switch (s_.even_branch_order[Random() & 7]) {
        case 0: return Emit(NOT_EAX, 2);
        case 1: return Emit(DEC_EAX, 1);
        case 2: return Emit(NEG_EAX, 2);
        case 3: return Emit(INC_EAX, 1);
        case 4:
            return EmitNop(5) && Emit(AND_EAX_IMMED, 1) && EmitImm(0x3ff) && Emit(MOV_EAX_INDIRECT, 3);
        case 5:
            return Emit(PUSH_EBX, 1) && Emit(MOV_EBX_EAX, 2) && Emit(AND_EBX_IMMED, 2) && EmitImm(0xaaaaaaaa) &&
                   Emit(AND_EAX_IMMED, 1) && EmitImm(0x55555555) && Emit(SHR_EBX_1, 2) && Emit(SHL_EAX_1, 2) &&
                   Emit(OR_EAX_EBX, 2) && Emit(POP_EBX, 1);
        case 6: return Emit(XOR_EAX_IMMED, 1) && EmitRandom();
        default: {
            uint8_t op = (Random() & 1) ? ADD_EAX_IMMED : SUB_EAX_IMMED;
            return Emit(op, 1) && EmitRandom();
        }
        }
    }
    bool OddBranch() {
        switch (s_.odd_branch_order[Random() % 6]) {
        case 0:
            return Emit(PUSH_ECX, 1) && Emit(MOV_ECX_EBX, 2) && Emit(AND_ECX_0F, 3) && Emit(SHR_EAX_CL, 2) &&
                   Emit(POP_ECX, 1);
        case 1:
            return Emit(PUSH_ECX, 1) && Emit(MOV_ECX_EBX, 2) && Emit(AND_ECX_0F, 3) && Emit(SHL_EAX_CL, 2) &&
                   Emit(POP_ECX, 1);
        case 2: return Emit(ADD_EAX_EBX, 2);
        case 3: return Emit(NEG_EAX, 2) && Emit(ADD_EAX_EBX, 2);
        case 4: return Emit(IMUL_EAX_EBX, 3);
        default: return Emit(SUB_EAX_EBX, 2);
        }
    }

    const CxScheme& s_;
    uint32_t seed_;
    int len_ = 0;
    std::vector<CxDecoder::Program::Ins> code_;
};

uint32_t Execute(const CxDecoder::Program& p, const uint32_t* cb, uint32_t arg) {
    uint32_t eax = 0, ebx = 0, ecx = 0, edi = 0;
    uint32_t stack[64];
    int sp = 0;
    for (const auto& i : p.code) {
        switch (i.op) {
        case MOV_EDI_ARG: edi = arg; break;
        case PUSH_EBX: if (sp < 64) stack[sp++] = ebx; break;
        case POP_EBX: if (sp > 0) ebx = stack[--sp]; break;
        case PUSH_ECX: if (sp < 64) stack[sp++] = ecx; break;
        case POP_ECX: if (sp > 0) ecx = stack[--sp]; break;
        case MOV_EBX_EAX: ebx = eax; break;
        case MOV_EAX_EDI: eax = edi; break;
        case MOV_ECX_EBX: ecx = ebx; break;
        case AND_ECX_0F: ecx &= 0x0f; break;
        case SHR_EBX_1: ebx >>= 1; break;
        case SHL_EAX_1: eax <<= 1; break;
        case SHR_EAX_CL: eax >>= (ecx & 31); break;
        case SHL_EAX_CL: eax <<= (ecx & 31); break;
        case OR_EAX_EBX: eax |= ebx; break;
        case NOT_EAX: eax = ~eax; break;
        case NEG_EAX: eax = 0u - eax; break;
        case DEC_EAX: eax--; break;
        case INC_EAX: eax++; break;
        case ADD_EAX_EBX: eax += ebx; break;
        case SUB_EAX_EBX: eax -= ebx; break;
        case IMUL_EAX_EBX: eax *= ebx; break;
        case MOV_EAX_INDIRECT: eax = cb[eax & 0x3ff]; break;  // indices are always < 0x400
        case MOV_EAX_IMMED: eax = i.imm; break;
        case AND_EBX_IMMED: ebx &= i.imm; break;
        case AND_EAX_IMMED: eax &= i.imm; break;
        case XOR_EAX_IMMED: eax ^= i.imm; break;
        case ADD_EAX_IMMED: eax += i.imm; break;
        case SUB_EAX_IMMED: eax -= i.imm; break;
        case RETN: return eax;
        }
    }
    return eax;
}

bool IsPermutation(const uint8_t* v, int n) {
    unsigned seen = 0;
    for (int i = 0; i < n; i++) {
        if (v[i] >= n || (seen & (1u << v[i]))) return false;
        seen |= 1u << v[i];
    }
    return true;
}

}  // namespace

bool CxScheme::IsValid() const {
    return IsPermutation(prolog_order, 3) && IsPermutation(odd_branch_order, 6) &&
           IsPermutation(even_branch_order, 8);
}

CxDecoder::CxDecoder(const CxScheme& scheme) : scheme_(scheme) {}
CxDecoder::~CxDecoder() = default;

const CxDecoder::Program& CxDecoder::GetProgram(uint32_t seed) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& slot = programs_[seed & 0x7f];
    if (!slot) {
        slot.reset(new Program);
        Generator(scheme_, seed & 0x7f).Build(*slot);
    }
    return *slot;
}

void CxDecoder::Decode(uint32_t key, uint64_t offset, uint8_t* buf, size_t count) {
    const Program& p = GetProgram(key & 0x7f);
    uint32_t arg = key >> 7;
    uint32_t ret1 = Execute(p, scheme_.control_block, arg);
    uint32_t ret2 = Execute(p, scheme_.control_block, ~arg);
    uint32_t key1 = ret2 >> 16;
    uint32_t key2 = ret2 & 0xffff;
    uint8_t key3 = (uint8_t)ret1;
    if (key1 == key2) key2 += 1;
    if (key3 == 0) key3 = 1;
    if (key2 >= offset && key2 < offset + count) buf[key2 - offset] ^= (uint8_t)(ret1 >> 16);
    if (key1 >= offset && key1 < offset + count) buf[key1 - offset] ^= (uint8_t)(ret1 >> 8);
    for (size_t i = 0; i < count; i++) buf[i] ^= key3;
}

void CxDecoder::Decrypt(uint32_t hash, uint64_t offset, uint8_t* buf, size_t count) {
    uint32_t key = hash;
    uint64_t base = (uint64_t)(hash & scheme_.mask) + scheme_.offset;
    if (offset < base) {
        size_t n = (size_t)std::min<uint64_t>(base - offset, count);
        Decode(key, offset, buf, n);
        offset += n;
        buf += n;
        count -= n;
    }
    if (count > 0) Decode((key >> 16) ^ key, offset, buf, count);
}

// ---------------------------------------------------------------------------
namespace {
bool ParseList(const std::string& v, uint8_t* out, int n) {
    std::stringstream ss(v);
    std::string item;
    int i = 0;
    while (std::getline(ss, item, ',')) {
        if (i >= n) return false;
        out[i++] = (uint8_t)std::strtoul(item.c_str(), nullptr, 0);
    }
    return i == n;
}
std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
}  // namespace

bool ParseScheme(const std::string& text,
                 bool (*load_file)(const std::string&, std::vector<uint8_t>&, void*), void* ctx,
                 CxScheme& out, std::string& error) {
    std::stringstream ss(text);
    std::string line, cb_name;
    bool have_mask = false, have_offset = false, p = false, o = false, e = false;
    while (std::getline(ss, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = Trim(line.substr(0, eq)), v = Trim(line.substr(eq + 1));
        if (k == "mask") { out.mask = (uint32_t)std::strtoul(v.c_str(), nullptr, 0); have_mask = true; }
        else if (k == "offset") { out.offset = (uint32_t)std::strtoul(v.c_str(), nullptr, 0); have_offset = true; }
        else if (k == "prolog") p = ParseList(v, out.prolog_order, 3);
        else if (k == "odd") o = ParseList(v, out.odd_branch_order, 6);
        else if (k == "even") e = ParseList(v, out.even_branch_order, 8);
        else if (k == "control_block") cb_name = v;
    }
    if (!have_mask || !have_offset || !p || !o || !e || cb_name.empty()) {
        error = "missing field (need mask, offset, prolog, odd, even, control_block)";
        return false;
    }
    if (!out.IsValid()) {
        error = "branch orders are not permutations";
        return false;
    }
    std::vector<uint8_t> cb;
    if (!load_file || !load_file(cb_name, cb, ctx) || cb.size() < 4096) {
        error = "cannot read 4096-byte control block '" + cb_name + "'";
        return false;
    }
    for (int i = 0; i < 1024; i++)
        out.control_block[i] = (uint32_t)cb[i * 4] | ((uint32_t)cb[i * 4 + 1] << 8) |
                               ((uint32_t)cb[i * 4 + 2] << 16) | ((uint32_t)cb[i * 4 + 3] << 24);
    return true;
}

}  // namespace cxdec
}  // namespace kk4s
