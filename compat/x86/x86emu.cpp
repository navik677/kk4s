// kk4s: IA-32 interpreter for XP3 filter plugins. See x86emu.h.
#include "x86emu.h"

#include <cstdio>
#include <cstring>

namespace kk4s {
namespace x86 {

namespace {

[[noreturn]] void Throw(const char* fmt, uint32_t a = 0, uint32_t b = 0) {
    char buf[160];
    snprintf(buf, sizeof(buf), fmt, a, b);
    throw Fault{buf};
}

inline uint32_t Mask(int sz) { return sz == 1 ? 0xffu : sz == 2 ? 0xffffu : 0xffffffffu; }
inline uint32_t Sign(int sz) { return sz == 1 ? 0x80u : sz == 2 ? 0x8000u : 0x80000000u; }
inline int32_t Sx(uint32_t v, int sz) {
    return sz == 1 ? (int32_t)(int8_t)v : sz == 2 ? (int32_t)(int16_t)v : (int32_t)v;
}

}  // namespace

// ---------------------------------------------------------------- memory

uint8_t* Memory::Page(uint32_t a) const {
    const auto& d = dir_[a >> 22];
    if (!d) return nullptr;
    return d[(a >> 12) & 1023].get();
}

uint8_t* Memory::Need(uint32_t a) const {
    uint8_t* p = Page(a);
    if (!p) Throw("memory fault at 0x%08x", a);
    return p + (a & (kPage - 1));
}

void Memory::Map(uint32_t addr, uint32_t size) {
    if (!size) return;
    uint64_t end = (uint64_t)addr + size;
    for (uint64_t p = addr & ~(uint64_t)(kPage - 1); p < end; p += kPage) {
        auto& d = dir_[(uint32_t)p >> 22];
        if (!d) d.reset(new std::unique_ptr<uint8_t[]>[1024]);
        auto& pg = d[((uint32_t)p >> 12) & 1023];
        if (!pg) {
            pg.reset(new uint8_t[kPage]());
            pages_++;
        }
    }
}

bool Memory::IsMapped(uint32_t a) const { return Page(a) != nullptr; }

bool Memory::IsRangeFree(uint32_t addr, uint32_t size) const {
    uint64_t end = (uint64_t)addr + size;
    if (end > 0x100000000ULL) return false;
    for (uint64_t p = addr & ~(uint64_t)(kPage - 1); p < end; p += kPage)
        if (Page((uint32_t)p)) return false;
    return true;
}

uint8_t Memory::Read8(uint32_t a) const { return *Need(a); }
uint16_t Memory::Read16(uint32_t a) const {
    if ((a & (kPage - 1)) <= kPage - 2) {
        const uint8_t* p = Need(a);
        return (uint16_t)(p[0] | (p[1] << 8));
    }
    return (uint16_t)(Read8(a) | (Read8(a + 1) << 8));
}
uint32_t Memory::Read32(uint32_t a) const {
    if ((a & (kPage - 1)) <= kPage - 4) {
        const uint8_t* p = Need(a);
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    return (uint32_t)Read16(a) | ((uint32_t)Read16(a + 2) << 16);
}
void Memory::Write8(uint32_t a, uint8_t v) { *Need(a) = v; }
void Memory::Write16(uint32_t a, uint16_t v) {
    Write8(a, (uint8_t)v);
    Write8(a + 1, (uint8_t)(v >> 8));
}
void Memory::Write32(uint32_t a, uint32_t v) {
    if ((a & (kPage - 1)) <= kPage - 4) {
        uint8_t* p = Need(a);
        p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
        return;
    }
    Write16(a, (uint16_t)v);
    Write16(a + 2, (uint16_t)(v >> 16));
}
void Memory::Read(uint32_t a, void* dst, size_t n) const {
    uint8_t* d = (uint8_t*)dst;
    while (n) {
        size_t chunk = kPage - (a & (kPage - 1));
        if (chunk > n) chunk = n;
        memcpy(d, Need(a), chunk);
        d += chunk; a += (uint32_t)chunk; n -= chunk;
    }
}
void Memory::Write(uint32_t a, const void* src, size_t n) {
    const uint8_t* s = (const uint8_t*)src;
    while (n) {
        size_t chunk = kPage - (a & (kPage - 1));
        if (chunk > n) chunk = n;
        memcpy(Need(a), s, chunk);
        s += chunk; a += (uint32_t)chunk; n -= chunk;
    }
}
std::string Memory::ReadCString(uint32_t a, size_t max) const {
    std::string s;
    for (size_t i = 0; i < max; ++i) {
        char c = (char)Read8(a + (uint32_t)i);
        if (!c) break;
        s += c;
    }
    return s;
}
std::u16string Memory::ReadWString(uint32_t a, size_t max) const {
    std::u16string s;
    for (size_t i = 0; i < max; ++i) {
        char16_t c = (char16_t)Read16(a + 2 * (uint32_t)i);
        if (!c) break;
        s += c;
    }
    return s;
}

// ---------------------------------------------------------------- machine

Machine::Machine() {
    mem.Map(kStackTop - kStackSize, kStackSize);
    mem.Map(kHostBase, 0x00800000);  // thunks + the return stop (never executed as code)
    mem.Map(kTib, 0x2000);           // TIB + a zeroed PEB
    fs_base = kTib;
    mem.Write32(kTib + 0x00, 0xFFFFFFFF);         // SEH chain end
    mem.Write32(kTib + 0x04, kStackTop);          // stack base
    mem.Write32(kTib + 0x08, kStackTop - kStackSize);
    mem.Write32(kTib + 0x18, kTib);               // self
    mem.Write32(kTib + 0x20, 1);                  // process id
    mem.Write32(kTib + 0x24, 1);                  // thread id
    mem.Write32(kTib + 0x30, kTib + 0x1000);      // PEB
    r[ESP] = kStackTop - 16;
}

uint32_t Machine::AddHost(const std::string& name, int nargs, Conv conv, HostFn fn) {
    hosts_.push_back(Host{name, nargs, conv, std::move(fn)});
    return kHostBase + 16 * (uint32_t)(hosts_.size() - 1);
}

bool Machine::IsHost(uint32_t a) const {
    return a >= kHostBase && a < kHostBase + 16 * (uint32_t)hosts_.size() && (a & 15) == 0;
}

const std::string& Machine::HostName(uint32_t a) const {
    static const std::string none;
    return IsHost(a) ? hosts_[(a - kHostBase) / 16].name : none;
}

uint32_t Machine::Alloc(uint32_t size) {
    size = (size + 15) & ~15u;
    if (!size) size = 16;
    if (heap_next_ + size > kHeapLimit || heap_next_ + size < heap_next_) Throw("guest heap exhausted (%u bytes)", size);
    uint32_t a = heap_next_;
    mem.Map(a, size);
    heap_next_ += size;
    return a;
}

void Machine::RunHost(uint32_t addr) {
    Host& h = hosts_[(addr - kHostBase) / 16];
    const int nargs = h.nargs;
    const Conv conv = h.conv;
    uint32_t ret_addr = mem.Read32(r[ESP]);
    uint32_t eax = h.fn(*this);
    r[EAX] = eax;
    // emulate "ret" / "ret n"
    bool callee_pops = conv == Conv::Stdcall;
    if (conv == Conv::Auto) {
        // cdecl callers clean up right after the call: add esp, imm8/imm32
        uint8_t b0 = mem.IsMapped(ret_addr) ? mem.Read8(ret_addr) : 0;
        uint8_t b1 = mem.IsMapped(ret_addr + 1) ? mem.Read8(ret_addr + 1) : 0;
        bool add_esp = (b0 == 0x83 || b0 == 0x81) && b1 == 0xC4;
        callee_pops = !add_esp;
    }
    r[ESP] += 4;
    if (callee_pops) r[ESP] += 4 * (uint32_t)nargs;
    eip = ret_addr;
}

uint32_t Machine::Call(uint32_t fn, const std::vector<uint32_t>& args, uint64_t max_steps) {
    uint32_t saved_esp = r[ESP];
    uint64_t saved_limit = limit_;
    for (size_t i = args.size(); i-- > 0;) Push(args[i]);
    Push(kReturnStop);
    uint32_t saved_eip = eip;
    eip = fn;
    limit_ = steps_ + max_steps;
    if (saved_limit && saved_limit < limit_) limit_ = saved_limit;
    while (eip != kReturnStop) {
        if (steps_ >= limit_) Throw("step limit reached at 0x%08x", eip);
        if (eip >= kHostBase && eip < kHostBase + 0x00800000) {
            if (!IsHost(eip)) Throw("jump into the host area at 0x%08x", eip);
            RunHost(eip);
            steps_++;
            continue;
        }
        Step();
    }
    uint32_t ret = r[EAX];
    r[ESP] = saved_esp;
    eip = saved_eip;
    limit_ = saved_limit;
    return ret;
}

// ---------------------------------------------------------------- decoding helpers

uint8_t Machine::Fetch8() { return mem.Read8(eip++); }
uint16_t Machine::Fetch16() { uint16_t v = mem.Read16(eip); eip += 2; return v; }
uint32_t Machine::Fetch32() { uint32_t v = mem.Read32(eip); eip += 4; return v; }

Machine::Op Machine::ModRM(uint8_t m) {
    int mod = m >> 6, rm = m & 7;
    if (mod == 3) return Op{true, rm, 0};
    uint32_t ea;
    if (rm == 4) {
        uint8_t sib = Fetch8();
        int scale = sib >> 6, idx = (sib >> 3) & 7, base = sib & 7;
        ea = idx == 4 ? 0 : (r[idx] << scale);
        if (base == 5 && mod == 0) ea += Fetch32();
        else ea += r[base];
    } else if (rm == 5 && mod == 0) {
        ea = Fetch32();
    } else {
        ea = r[rm];
    }
    if (mod == 1) ea += (uint32_t)(int32_t)(int8_t)Fetch8();
    else if (mod == 2) ea += Fetch32();
    if (fs_) ea += fs_base;
    return Op{false, 0, ea};
}

uint32_t Machine::GetR(int idx, int sz) const {
    if (sz == 4) return r[idx];
    if (sz == 2) return r[idx] & 0xffff;
    return idx < 4 ? (r[idx] & 0xff) : ((r[idx - 4] >> 8) & 0xff);
}

void Machine::SetR(int idx, int sz, uint32_t v) {
    if (sz == 4) r[idx] = v;
    else if (sz == 2) r[idx] = (r[idx] & 0xffff0000u) | (v & 0xffff);
    else if (idx < 4) r[idx] = (r[idx] & 0xffffff00u) | (v & 0xff);
    else r[idx - 4] = (r[idx - 4] & 0xffff00ffu) | ((v & 0xff) << 8);
}

uint32_t Machine::Get(const Op& o, int sz) {
    if (o.reg) return GetR(o.idx, sz);
    return sz == 4 ? mem.Read32(o.ea) : sz == 2 ? mem.Read16(o.ea) : mem.Read8(o.ea);
}

void Machine::Set(const Op& o, int sz, uint32_t v) {
    if (o.reg) return SetR(o.idx, sz, v);
    if (sz == 4) mem.Write32(o.ea, v);
    else if (sz == 2) mem.Write16(o.ea, (uint16_t)v);
    else mem.Write8(o.ea, (uint8_t)v);
}

void Machine::Push(uint32_t v) {
    r[ESP] -= 4;
    mem.Write32(r[ESP], v);
}

uint32_t Machine::Pop() {
    uint32_t v = mem.Read32(r[ESP]);
    r[ESP] += 4;
    return v;
}

bool Machine::Cond(int cc) const {
    bool cf = eflags & CF, zf = eflags & ZF, sf = eflags & SF, of = eflags & OF, pf = eflags & PF;
    bool v;
    switch (cc >> 1) {
    case 0: v = of; break;
    case 1: v = cf; break;
    case 2: v = zf; break;
    case 3: v = cf || zf; break;
    case 4: v = sf; break;
    case 5: v = pf; break;
    case 6: v = sf != of; break;
    default: v = zf || (sf != of); break;
    }
    return (cc & 1) ? !v : v;
}

void Machine::SetSZP(uint32_t res, int sz) {
    res &= Mask(sz);
    if (!res) eflags |= ZF;
    if (res & Sign(sz)) eflags |= SF;
    if (!__builtin_parity(res & 0xff)) eflags |= PF;
}

uint32_t Machine::Alu(int op, uint32_t a, uint32_t b, int sz) {
    const uint32_t m = Mask(sz), sb = Sign(sz);
    a &= m;
    b &= m;
    uint32_t res;
    uint32_t carry_in = eflags & CF ? 1 : 0;
    eflags &= ~(CF | PF | AF | ZF | SF | OF);
    switch (op) {
    case 0:  // add
    case 2: {  // adc
        uint64_t c = op == 2 ? carry_in : 0;
        uint64_t w = (uint64_t)a + b + c;
        res = (uint32_t)w & m;
        if (w > m) eflags |= CF;
        if ((a ^ res) & (b ^ res) & sb) eflags |= OF;
        if ((a ^ b ^ res) & 0x10) eflags |= AF;
        break;
    }
    case 3:  // sbb
    case 5:  // sub
    case 7: {  // cmp
        uint64_t c = op == 3 ? carry_in : 0;
        res = (uint32_t)((uint64_t)a - b - c) & m;
        if ((uint64_t)a < (uint64_t)b + c) eflags |= CF;
        if ((a ^ b) & (a ^ res) & sb) eflags |= OF;
        if ((a ^ b ^ res) & 0x10) eflags |= AF;
        break;
    }
    case 1: res = a | b; break;
    case 4: res = a & b; break;
    default: res = a ^ b; break;  // 6
    }
    SetSZP(res, sz);
    return res;
}

uint32_t Machine::IncDec(uint32_t a, bool dec, int sz) {
    const uint32_t m = Mask(sz), sb = Sign(sz);
    a &= m;
    uint32_t res = (dec ? a - 1 : a + 1) & m;
    eflags &= ~(PF | AF | ZF | SF | OF);  // CF kept
    if (!dec && res == sb) eflags |= OF;
    if (dec && a == sb) eflags |= OF;
    if ((a ^ res) & 0x10) eflags |= AF;
    SetSZP(res, sz);
    return res;
}

uint32_t Machine::Shift(int op, uint32_t a, uint32_t cnt, int sz) {
    const int bits = sz * 8;
    const uint32_t m = Mask(sz), sb = Sign(sz);
    a &= m;
    cnt &= 31;
    if (!cnt) return a;
    uint32_t res = a;
    bool cf = eflags & CF;
    switch (op) {
    case 0: {  // rol
        int c = (int)(cnt % bits);
        res = c ? ((a << c) | (a >> (bits - c))) & m : a;
        cf = res & 1;
        eflags = (eflags & ~(CF | OF)) | (cf ? CF : 0);
        if (((res & sb) != 0) != cf) eflags |= OF;
        return res;
    }
    case 1: {  // ror
        int c = (int)(cnt % bits);
        res = c ? ((a >> c) | (a << (bits - c))) & m : a;
        cf = res & sb;
        eflags = (eflags & ~(CF | OF)) | (cf ? CF : 0);
        if (((res ^ (res << 1)) & sb) != 0) eflags |= OF;
        return res;
    }
    case 2: {  // rcl
        int c = (int)(cnt % (bits + 1));
        for (int i = 0; i < c; ++i) {
            bool out = res & sb;
            res = ((res << 1) | (cf ? 1 : 0)) & m;
            cf = out;
        }
        eflags = (eflags & ~(CF | OF)) | (cf ? CF : 0);
        if (((res & sb) != 0) != cf) eflags |= OF;
        return res;
    }
    case 3: {  // rcr
        int c = (int)(cnt % (bits + 1));
        bool of = ((res & sb) != 0) != cf;
        for (int i = 0; i < c; ++i) {
            bool out = res & 1;
            res = (res >> 1) | (cf ? sb : 0);
            cf = out;
        }
        eflags = (eflags & ~(CF | OF)) | (cf ? CF : 0) | (of ? OF : 0);
        return res;
    }
    case 4:
    case 6: {  // shl / sal
        uint64_t w = (uint64_t)a << cnt;
        res = (uint32_t)w & m;
        cf = cnt <= (uint32_t)bits ? (w >> bits) & 1 : 0;
        eflags &= ~(CF | PF | AF | ZF | SF | OF);
        if (cf) eflags |= CF;
        if (((res & sb) != 0) != cf) eflags |= OF;
        SetSZP(res, sz);
        return res;
    }
    case 5: {  // shr
        cf = cnt <= (uint32_t)bits ? (a >> (cnt - 1)) & 1 : 0;
        res = cnt < (uint32_t)bits ? a >> cnt : 0;
        eflags &= ~(CF | PF | AF | ZF | SF | OF);
        if (cf) eflags |= CF;
        if (a & sb) eflags |= OF;
        SetSZP(res, sz);
        return res;
    }
    default: {  // 7 sar
        int32_t s = Sx(a, sz);
        if (cnt >= (uint32_t)bits) {
            cf = s < 0;
            res = s < 0 ? m : 0;
        } else {
            cf = (s >> (cnt - 1)) & 1;
            res = (uint32_t)(s >> cnt) & m;
        }
        eflags &= ~(CF | PF | AF | ZF | SF | OF);
        if (cf) eflags |= CF;
        SetSZP(res, sz);
        return res;
    }
    }
}

void Machine::Group3(const Op& o, int sub, int sz) {
    const uint32_t m = Mask(sz);
    uint32_t v = Get(o, sz);
    switch (sub) {
    case 0:
    case 1: {  // test r/m, imm
        uint32_t imm = sz == 1 ? Fetch8() : sz == 2 ? Fetch16() : Fetch32();
        Alu(4, v, imm, sz);
        return;
    }
    case 2: Set(o, sz, ~v & m); return;  // not
    case 3: {  // neg
        uint32_t res = Alu(5, 0, v, sz);
        Set(o, sz, res);
        if (v & m) eflags |= CF; else eflags &= ~CF;
        return;
    }
    case 4:
    case 5: {  // mul / imul (one operand)
        uint64_t hi, lo;
        bool overflow;
        if (sub == 4) {
            uint64_t p = (uint64_t)GetR(EAX, sz) * v;
            lo = p & m; hi = (p >> (sz * 8)) & m;
            overflow = hi != 0;
        } else {
            int64_t p = (int64_t)Sx(GetR(EAX, sz), sz) * Sx(v, sz);
            lo = (uint64_t)p & m; hi = ((uint64_t)p >> (sz * 8)) & m;
            overflow = p != (int64_t)Sx((uint32_t)lo, sz);
        }
        if (sz == 1) SetR(EAX, 2, (uint32_t)(lo | (hi << 8)));
        else { SetR(EAX, sz, (uint32_t)lo); SetR(EDX, sz, (uint32_t)hi); }
        eflags &= ~(CF | OF);
        if (overflow) eflags |= CF | OF;
        return;
    }
    case 6: {  // div
        if (!v) Throw("divide by zero at 0x%08x", start_eip_);
        uint64_t n = sz == 1 ? GetR(EAX, 2) : ((uint64_t)GetR(EDX, sz) << (sz * 8)) | GetR(EAX, sz);
        uint64_t q = n / v, rem = n % v;
        if (q > m) Throw("divide overflow at 0x%08x", start_eip_);
        if (sz == 1) SetR(EAX, 2, (uint32_t)(q | (rem << 8)));
        else { SetR(EAX, sz, (uint32_t)q); SetR(EDX, sz, (uint32_t)rem); }
        return;
    }
    default: {  // 7 idiv
        if (!(v & m)) Throw("divide by zero at 0x%08x", start_eip_);
        int64_t n = sz == 1 ? (int64_t)(int16_t)GetR(EAX, 2)
                  : sz == 2 ? (int64_t)(int32_t)((GetR(EDX, 2) << 16) | GetR(EAX, 2))
                            : (int64_t)(((uint64_t)r[EDX] << 32) | r[EAX]);
        int64_t d = Sx(v, sz);
        if (d == -1 && n == INT64_MIN) Throw("divide overflow at 0x%08x", start_eip_);
        int64_t q = n / d, rem = n % d;
        if (q != (int64_t)Sx((uint32_t)q & m, sz)) Throw("divide overflow at 0x%08x", start_eip_);
        if (sz == 1) SetR(EAX, 2, ((uint32_t)q & 0xff) | (((uint32_t)rem & 0xff) << 8));
        else { SetR(EAX, sz, (uint32_t)q); SetR(EDX, sz, (uint32_t)rem); }
        return;
    }
    }
}

void Machine::StringOp(uint8_t op, int sz, int rep) {
    const int step = (eflags & DF) ? -sz : sz;
    auto once = [&]() {
        switch (op) {
        case 0xA4: case 0xA5: {  // movs
            uint32_t v = sz == 4 ? mem.Read32(r[ESI]) : sz == 2 ? mem.Read16(r[ESI]) : mem.Read8(r[ESI]);
            if (sz == 4) mem.Write32(r[EDI], v); else if (sz == 2) mem.Write16(r[EDI], (uint16_t)v); else mem.Write8(r[EDI], (uint8_t)v);
            r[ESI] += step; r[EDI] += step;
            break;
        }
        case 0xA6: case 0xA7: {  // cmps
            uint32_t a = sz == 4 ? mem.Read32(r[ESI]) : sz == 2 ? mem.Read16(r[ESI]) : mem.Read8(r[ESI]);
            uint32_t b = sz == 4 ? mem.Read32(r[EDI]) : sz == 2 ? mem.Read16(r[EDI]) : mem.Read8(r[EDI]);
            Alu(7, a, b, sz);
            r[ESI] += step; r[EDI] += step;
            break;
        }
        case 0xAA: case 0xAB: {  // stos
            uint32_t v = GetR(EAX, sz);
            if (sz == 4) mem.Write32(r[EDI], v); else if (sz == 2) mem.Write16(r[EDI], (uint16_t)v); else mem.Write8(r[EDI], (uint8_t)v);
            r[EDI] += step;
            break;
        }
        case 0xAC: case 0xAD: {  // lods
            uint32_t v = sz == 4 ? mem.Read32(r[ESI]) : sz == 2 ? mem.Read16(r[ESI]) : mem.Read8(r[ESI]);
            SetR(EAX, sz, v);
            r[ESI] += step;
            break;
        }
        default: {  // scas
            uint32_t b = sz == 4 ? mem.Read32(r[EDI]) : sz == 2 ? mem.Read16(r[EDI]) : mem.Read8(r[EDI]);
            Alu(7, GetR(EAX, sz), b, sz);
            r[EDI] += step;
            break;
        }
        }
    };
    if (!rep) { once(); return; }
    const bool cmp = op == 0xA6 || op == 0xA7 || op == 0xAE || op == 0xAF;
    while (r[ECX]) {
        // fast path: forward rep stosb/stosd / movsb/movsd within single pages is common in CRT code
        once();
        r[ECX]--;
        steps_++;
        if (cmp) {
            bool zf = eflags & ZF;
            if (rep == 0xF3 && !zf) break;  // repe
            if (rep == 0xF2 && zf) break;   // repne
        }
        if (steps_ >= limit_) Throw("step limit reached in a string op at 0x%08x", start_eip_);
    }
}

// ---------------------------------------------------------------- execution

void Machine::Step() {
    steps_++;
    start_eip_ = eip;
    opsize_ = 4;
    fs_ = false;
    int rep = 0;
    uint8_t b;
    for (;;) {
        b = Fetch8();
        if (b == 0x66) opsize_ = 2;
        else if (b == 0xF2 || b == 0xF3) rep = b;
        else if (b == 0x64) fs_ = true;
        else if (b == 0x2E || b == 0x3E || b == 0x26 || b == 0x36 || b == 0xF0) continue;
        else if (b == 0x65 || b == 0x67) Throw("unsupported prefix 0x%02x at 0x%08x", b, start_eip_);
        else break;
    }
    const int osz = opsize_;

    // ALU block: 00-3F except the x6/x7/xE/xF columns
    if (b < 0x40 && (b & 7) < 6) {
        int op = b >> 3;
        int form = b & 7;
        if (form < 4) {
            int sz = (form & 1) ? osz : 1;
            uint8_t m = Fetch8();
            Op o = ModRM(m);
            int reg = (m >> 3) & 7;
            if (form < 2) {  // r/m op= reg
                uint32_t res = Alu(op, Get(o, sz), GetR(reg, sz), sz);
                if (op != 7) Set(o, sz, res);
            } else {  // reg op= r/m
                uint32_t res = Alu(op, GetR(reg, sz), Get(o, sz), sz);
                if (op != 7) SetR(reg, sz, res);
            }
        } else {
            int sz = form == 4 ? 1 : osz;
            uint32_t imm = sz == 1 ? Fetch8() : sz == 2 ? Fetch16() : Fetch32();
            uint32_t res = Alu(op, GetR(EAX, sz), imm, sz);
            if (op != 7) SetR(EAX, sz, res);
        }
        return;
    }

    switch (b) {
    case 0x0F: TwoByte(); return;
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
        SetR(b & 7, osz, IncDec(GetR(b & 7, osz), false, osz)); return;
    case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
        SetR(b & 7, osz, IncDec(GetR(b & 7, osz), true, osz)); return;
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
        Push(r[b & 7]); return;
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        r[b & 7] = Pop(); return;
    case 0x60: {  // pusha
        uint32_t sp = r[ESP];
        for (int i = 0; i < 8; ++i) Push(i == ESP ? sp : r[i]);
        return;
    }
    case 0x61: {  // popa
        for (int i = 7; i >= 0; --i) { uint32_t v = Pop(); if (i != ESP) r[i] = v; }
        return;
    }
    case 0x68: Push(osz == 2 ? Fetch16() : Fetch32()); return;
    case 0x6A: Push((uint32_t)(int32_t)(int8_t)Fetch8()); return;
    case 0x69:
    case 0x6B: {  // imul r, r/m, imm
        uint8_t m = Fetch8();
        Op o = ModRM(m);
        int32_t a = Sx(Get(o, osz), osz);
        int32_t imm = b == 0x6B ? (int8_t)Fetch8() : (osz == 2 ? (int16_t)Fetch16() : (int32_t)Fetch32());
        int64_t p = (int64_t)a * imm;
        uint32_t lo = (uint32_t)p & Mask(osz);
        SetR((m >> 3) & 7, osz, lo);
        eflags &= ~(CF | OF);
        if (p != (int64_t)Sx(lo, osz)) eflags |= CF | OF;
        return;
    }
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
        int32_t d = (int8_t)Fetch8();
        if (Cond(b & 15)) eip += (uint32_t)d;
        return;
    }
    case 0x80: case 0x81: case 0x83: {
        int sz = b == 0x80 ? 1 : osz;
        uint8_t m = Fetch8();
        Op o = ModRM(m);
        uint32_t imm = b == 0x81 ? (osz == 2 ? Fetch16() : Fetch32()) : (uint32_t)(int32_t)(int8_t)Fetch8();
        if (b == 0x80) imm &= 0xff;
        int op = (m >> 3) & 7;
        uint32_t res = Alu(op, Get(o, sz), imm, sz);
        if (op != 7) Set(o, sz, res);
        return;
    }
    case 0x84: case 0x85: {
        int sz = b == 0x84 ? 1 : osz;
        uint8_t m = Fetch8();
        Op o = ModRM(m);
        Alu(4, Get(o, sz), GetR((m >> 3) & 7, sz), sz);
        return;
    }
    case 0x86: case 0x87: {
        int sz = b == 0x86 ? 1 : osz;
        uint8_t m = Fetch8();
        Op o = ModRM(m);
        uint32_t t = Get(o, sz);
        Set(o, sz, GetR((m >> 3) & 7, sz));
        SetR((m >> 3) & 7, sz, t);
        return;
    }
    case 0x88: case 0x89: {
        int sz = b == 0x88 ? 1 : osz;
        uint8_t m = Fetch8();
        Op o = ModRM(m);
        Set(o, sz, GetR((m >> 3) & 7, sz));
        return;
    }
    case 0x8A: case 0x8B: {
        int sz = b == 0x8A ? 1 : osz;
        uint8_t m = Fetch8();
        Op o = ModRM(m);
        SetR((m >> 3) & 7, sz, Get(o, sz));
        return;
    }
    case 0x8D: {  // lea (no segment base)
        uint8_t m = Fetch8();
        bool f = fs_;
        fs_ = false;
        Op o = ModRM(m);
        fs_ = f;
        if (o.reg) Throw("lea with a register operand at 0x%08x", start_eip_);
        SetR((m >> 3) & 7, osz, o.ea);
        return;
    }
    case 0x8F: {
        uint8_t m = Fetch8();
        uint32_t v = Pop();
        Op o = ModRM(m);  // address computed after the pop, as the CPU does
        Set(o, 4, v);
        return;
    }
    case 0x90: return;
    case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
        uint32_t t = GetR(EAX, osz);
        SetR(EAX, osz, GetR(b & 7, osz));
        SetR(b & 7, osz, t);
        return;
    }
    case 0x98:
        if (osz == 2) SetR(EAX, 2, (uint32_t)(int16_t)(int8_t)r[EAX]);
        else r[EAX] = (uint32_t)(int32_t)(int16_t)r[EAX];
        return;
    case 0x99:
        if (osz == 2) SetR(EDX, 2, (r[EAX] & 0x8000) ? 0xffff : 0);
        else r[EDX] = (r[EAX] & 0x80000000u) ? 0xffffffffu : 0;
        return;
    case 0x9B: return;  // fwait
    case 0xD7: SetR(EAX, 1, mem.Read8(r[EBX] + (r[EAX] & 0xff) + (fs_ ? fs_base : 0))); return;  // xlat
    case 0x9C: Push((eflags & 0x00000ED5u) | 2); return;
    case 0x9D: eflags = (Pop() & 0x00000ED5u) | 0x202; return;
    case 0x9E: eflags = (eflags & ~0xD5u) | ((r[EAX] >> 8) & 0xD5u); return;
    case 0x9F: SetR(4, 1, (eflags & 0xD5u) | 2); return;  // AH
    case 0xA0: case 0xA1: case 0xA2: case 0xA3: {
        int sz = (b & 1) ? osz : 1;
        uint32_t a = Fetch32() + (fs_ ? fs_base : 0);
        Op o{false, 0, a};
        if (b < 0xA2) SetR(EAX, sz, Get(o, sz)); else Set(o, sz, GetR(EAX, sz));
        return;
    }
    case 0xA4: case 0xA5: case 0xA6: case 0xA7: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
        StringOp(b, (b & 1) ? osz : 1, rep);
        return;
    case 0xA8: Alu(4, r[EAX], Fetch8(), 1); return;
    case 0xA9: Alu(4, r[EAX], osz == 2 ? Fetch16() : Fetch32(), osz); return;
    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        SetR(b & 7, 1, Fetch8()); return;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        SetR(b & 7, osz, osz == 2 ? Fetch16() : Fetch32()); return;
    case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
        int sz = (b & 1) ? osz : 1;
        uint8_t m = Fetch8();
        Op o = ModRM(m);
        uint32_t cnt = (b <= 0xC1) ? Fetch8() : (b <= 0xD1) ? 1 : (r[ECX] & 0xff);
        Set(o, sz, Shift((m >> 3) & 7, Get(o, sz), cnt, sz));
        return;
    }
    case 0xC2: {
        uint16_t n = Fetch16();
        eip = Pop();
        r[ESP] += n;
        return;
    }
    case 0xC3: eip = Pop(); return;
    case 0xC6: case 0xC7: {
        int sz = b == 0xC6 ? 1 : osz;
        uint8_t m = Fetch8();
        Op o = ModRM(m);
        uint32_t imm = sz == 1 ? Fetch8() : sz == 2 ? Fetch16() : Fetch32();
        Set(o, sz, imm);
        return;
    }
    case 0xC8: {  // enter n, 0
        uint16_t n = Fetch16();
        uint8_t lvl = Fetch8();
        if (lvl) Throw("enter with nesting level %u at 0x%08x", lvl, start_eip_);
        Push(r[EBP]);
        r[EBP] = r[ESP];
        r[ESP] -= n;
        return;
    }
    case 0xC9: r[ESP] = r[EBP]; r[EBP] = Pop(); return;
    case 0xCC: Throw("int3 at 0x%08x", start_eip_);
    case 0xD9: case 0xDB: case 0xDD: {  // x87: only control / status word handling
        uint8_t m = Fetch8();
        int sub = (m >> 3) & 7;
        if (b == 0xDB && m == 0xE3) { fpu_cw_ = 0x037F; return; }  // fninit
        if (b == 0xDB && m == 0xE2) return;                        // fnclex
        if ((m >> 6) != 3) {
            Op o = ModRM(m);
            if (b == 0xD9 && sub == 7) { mem.Write16(o.ea, fpu_cw_); return; }        // fnstcw
            if (b == 0xD9 && sub == 5) { fpu_cw_ = mem.Read16(o.ea); return; }        // fldcw
            if (b == 0xDD && sub == 7) { mem.Write16(o.ea, 0); return; }              // fnstsw
        }
        Throw("x87 instruction 0x%04x not supported at 0x%08x", (uint32_t)(b << 8 | m), start_eip_);
    }
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: {
        int32_t d = (int8_t)Fetch8();
        bool jump;
        if (b == 0xE3) jump = r[ECX] == 0;
        else {
            r[ECX]--;
            jump = r[ECX] != 0;
            if (b == 0xE0) jump = jump && !(eflags & ZF);
            if (b == 0xE1) jump = jump && (eflags & ZF);
        }
        if (jump) eip += (uint32_t)d;
        return;
    }
    case 0xE8: {
        int32_t d = (int32_t)Fetch32();
        Push(eip);
        eip += (uint32_t)d;
        return;
    }
    case 0xE9: { int32_t d = (int32_t)Fetch32(); eip += (uint32_t)d; return; }
    case 0xEB: { int32_t d = (int8_t)Fetch8(); eip += (uint32_t)d; return; }
    case 0xF5: eflags ^= CF; return;
    case 0xF6: case 0xF7: {
        int sz = b == 0xF6 ? 1 : osz;
        uint8_t m = Fetch8();
        Op o = ModRM(m);
        Group3(o, (m >> 3) & 7, sz);
        return;
    }
    case 0xF8: eflags &= ~CF; return;
    case 0xF9: eflags |= CF; return;
    case 0xFA: case 0xFB: return;
    case 0xFC: eflags &= ~DF; return;
    case 0xFD: eflags |= DF; return;
    case 0xFE: case 0xFF: {
        int sz = b == 0xFE ? 1 : osz;
        uint8_t m = Fetch8();
        int sub = (m >> 3) & 7;
        Op o = ModRM(m);
        switch (sub) {
        case 0: Set(o, sz, IncDec(Get(o, sz), false, sz)); return;
        case 1: Set(o, sz, IncDec(Get(o, sz), true, sz)); return;
        case 2: if (b == 0xFF) { uint32_t t = Get(o, 4); Push(eip); eip = t; return; } break;
        case 4: if (b == 0xFF) { eip = Get(o, 4); return; } break;
        case 6: if (b == 0xFF) { Push(Get(o, 4)); return; } break;
        default: break;
        }
        Throw("unsupported 0x%02x group instruction at 0x%08x", b, start_eip_);
    }
    default:
        Throw("unsupported opcode 0x%02x at 0x%08x", b, start_eip_);
    }
}

void Machine::TwoByte() {
    const int osz = opsize_;
    uint8_t b = Fetch8();
    if (b >= 0x80 && b <= 0x8F) {
        int32_t d = (int32_t)Fetch32();
        if (Cond(b & 15)) eip += (uint32_t)d;
        return;
    }
    if (b >= 0x90 && b <= 0x9F) {
        Op o = ModRM(Fetch8());
        Set(o, 1, Cond(b & 15) ? 1 : 0);
        return;
    }
    if (b >= 0x40 && b <= 0x4F) {
        uint8_t m = Fetch8();
        Op o = ModRM(m);
        uint32_t v = Get(o, osz);
        if (Cond(b & 15)) SetR((m >> 3) & 7, osz, v);
        return;
    }
    if (b >= 0xC8 && b <= 0xCF) {
        uint32_t v = r[b & 7];
        r[b & 7] = (v >> 24) | ((v >> 8) & 0xff00) | ((v << 8) & 0xff0000) | (v << 24);
        return;
    }
    switch (b) {
    case 0x0B: Throw("ud2 at 0x%08x", start_eip_);
    case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E:
    case 0x1F: ModRM(Fetch8()); return;  // prefetch hints / multi-byte nop
    case 0x31: r[EAX] = (uint32_t)steps_; r[EDX] = (uint32_t)(steps_ >> 32); return;  // rdtsc
    case 0xA2: {  // cpuid: an old integer-only Pentium Pro, no MMX / SSE
        uint32_t leaf = r[EAX];
        if (leaf == 0) { r[EAX] = 1; r[EBX] = 0x756e6547; r[EDX] = 0x49656e69; r[ECX] = 0x6c65746e; }
        else if (leaf == 1) { r[EAX] = 0x00000611; r[EBX] = 0; r[ECX] = 0; r[EDX] = 0x00008111; }  // FPU TSC CX8 CMOV
        else { r[EAX] = r[EBX] = r[ECX] = r[EDX] = 0; }
        return;
    }
    case 0xA3: case 0xAB: case 0xB3: case 0xBB: case 0xBA: {  // bt / bts / btr / btc
        uint8_t m = Fetch8();
        Op o = ModRM(m);
        int kind;
        uint32_t bit;
        if (b == 0xBA) {
            kind = (m >> 3) & 7;
            if (kind < 4) Throw("bad bt group at 0x%08x", start_eip_);
            kind -= 4;
            bit = Fetch8() & (osz * 8 - 1);
        } else {
            kind = b == 0xA3 ? 0 : b == 0xAB ? 1 : b == 0xB3 ? 2 : 3;
            int32_t off = (int32_t)GetR((m >> 3) & 7, osz);
            if (!o.reg) {  // the bit offset can reach outside the operand
                o.ea += (uint32_t)((off >> 3) & ~(osz - 1));
                bit = (uint32_t)off & (osz * 8 - 1);
            } else {
                bit = (uint32_t)off & (osz * 8 - 1);
            }
        }
        uint32_t v = Get(o, osz);
        bool set = (v >> bit) & 1;
        eflags = (eflags & ~CF) | (set ? CF : 0);
        if (kind == 1) Set(o, osz, v | (1u << bit));
        else if (kind == 2) Set(o, osz, v & ~(1u << bit));
        else if (kind == 3) Set(o, osz, v ^ (1u << bit));
        return;
    }
    case 0xA4: case 0xA5: case 0xAC: case 0xAD: {  // shld / shrd
        uint8_t m = Fetch8();
        Op o = ModRM(m);
        uint32_t cnt = (b == 0xA4 || b == 0xAC) ? Fetch8() : r[ECX];
        cnt &= 31;
        if (!cnt) return;
        int bits = osz * 8;
        uint32_t dst = Get(o, osz), src = GetR((m >> 3) & 7, osz);
        uint32_t res;
        bool cf;
        if (b <= 0xA5) {
            uint64_t w = ((uint64_t)dst << bits) | src;
            res = (uint32_t)((w << cnt) >> bits) & Mask(osz);
            cf = (dst >> (bits - cnt)) & 1;
        } else {
            uint64_t w = ((uint64_t)src << bits) | dst;
            res = (uint32_t)(w >> cnt) & Mask(osz);
            cf = (dst >> (cnt - 1)) & 1;
        }
        Set(o, osz, res);
        eflags &= ~(CF | PF | AF | ZF | SF | OF);
        if (cf) eflags |= CF;
        if (((res ^ dst) & Sign(osz))) eflags |= OF;
        SetSZP(res, osz);
        return;
    }
    case 0xAF: {
        uint8_t m = Fetch8();
        Op o = ModRM(m);
        int reg = (m >> 3) & 7;
        int64_t p = (int64_t)Sx(GetR(reg, osz), osz) * Sx(Get(o, osz), osz);
        uint32_t lo = (uint32_t)p & Mask(osz);
        SetR(reg, osz, lo);
        eflags &= ~(CF | OF);
        if (p != (int64_t)Sx(lo, osz)) eflags |= CF | OF;
        return;
    }
    case 0xB0: case 0xB1: {  // cmpxchg
        int sz = b == 0xB0 ? 1 : osz;
        uint8_t m = Fetch8();
        Op o = ModRM(m);
        uint32_t v = Get(o, sz);
        Alu(7, GetR(EAX, sz), v, sz);
        if (eflags & ZF) Set(o, sz, GetR((m >> 3) & 7, sz));
        else SetR(EAX, sz, v);
        return;
    }
    case 0xB6: case 0xB7: case 0xBE: case 0xBF: {
        int ssz = (b & 1) ? 2 : 1;
        uint8_t m = Fetch8();
        Op o = ModRM(m);
        uint32_t v = Get(o, ssz);
        if (b >= 0xBE) v = (uint32_t)Sx(v, ssz);
        SetR((m >> 3) & 7, osz, v);
        return;
    }
    case 0xBC: case 0xBD: {  // bsf / bsr
        uint8_t m = Fetch8();
        Op o = ModRM(m);
        uint32_t v = Get(o, osz);
        if (!v) { eflags |= ZF; return; }
        eflags &= ~ZF;
        SetR((m >> 3) & 7, osz, b == 0xBC ? (uint32_t)__builtin_ctz(v) : 31u - (uint32_t)__builtin_clz(v));
        return;
    }
    case 0xC0: case 0xC1: {  // xadd
        int sz = b == 0xC0 ? 1 : osz;
        uint8_t m = Fetch8();
        Op o = ModRM(m);
        int reg = (m >> 3) & 7;
        uint32_t d = Get(o, sz), s = GetR(reg, sz);
        uint32_t sum = Alu(0, d, s, sz);
        SetR(reg, sz, d);
        Set(o, sz, sum);
        return;
    }
    default:
        Throw("unsupported opcode 0x0f 0x%02x at 0x%08x", b, start_eip_);
    }
}

}  // namespace x86
}  // namespace kk4s
