// kk4s: a small IA-32 interpreter, only for running a game's own XP3 filter
// plugin (.tpm) in a sandbox (compat/kk4s_tpm.cpp). Not a Windows runtime:
// integer instructions, x87 control words, string ops, no SSE/MMX (cpuid says
// so, so CRT code takes its integer paths), flat 32-bit memory with a private
// page table, and "host functions": addresses that run C++ code when the guest
// calls them (fake tp_stub and Win32 functions).
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace kk4s {
namespace x86 {

// Guest memory fault, bad instruction, host function failure or step limit.
struct Fault {
    std::string what;
};

class Memory {
public:
    static constexpr uint32_t kPage = 4096;
    // Map [addr, addr + size) zero-filled (already mapped pages stay as they are).
    void Map(uint32_t addr, uint32_t size);
    bool IsMapped(uint32_t addr) const;
    bool IsRangeFree(uint32_t addr, uint32_t size) const;
    uint8_t* Page(uint32_t addr) const;  // null when unmapped
    size_t MappedBytes() const { return pages_ * kPage; }

    uint8_t Read8(uint32_t a) const;
    uint16_t Read16(uint32_t a) const;
    uint32_t Read32(uint32_t a) const;
    void Write8(uint32_t a, uint8_t v);
    void Write16(uint32_t a, uint16_t v);
    void Write32(uint32_t a, uint32_t v);
    void Read(uint32_t a, void* dst, size_t n) const;
    void Write(uint32_t a, const void* src, size_t n);
    std::string ReadCString(uint32_t a, size_t max = 4096) const;
    std::u16string ReadWString(uint32_t a, size_t max = 4096) const;

private:
    uint8_t* Need(uint32_t a) const;
    std::unique_ptr<std::unique_ptr<uint8_t[]>[]> dir_[1024];
    size_t pages_ = 0;
};

class Machine;
using HostFn = std::function<uint32_t(Machine&)>;  // returns EAX

class Machine {
public:
    enum Reg { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI };
    static constexpr uint32_t CF = 1u << 0, PF = 1u << 2, AF = 1u << 4, ZF = 1u << 6, SF = 1u << 7,
                              IF = 1u << 9, DF = 1u << 10, OF = 1u << 11;
    // Calling convention of a host function: how many argument bytes it pops.
    enum class Conv { Cdecl, Stdcall, Auto };  // Auto: cdecl when the caller cleans up after the call

    Machine();

    Memory mem;
    uint32_t r[8] = {};
    uint32_t eip = 0;
    uint32_t eflags = 0x202;
    uint32_t fs_base = 0;

    // Register a host function; returns the guest address to call.
    uint32_t AddHost(const std::string& name, int nargs, Conv conv, HostFn fn);
    const std::string& HostName(uint32_t addr) const;
    bool IsHost(uint32_t addr) const;
    // Inside a host function: the n-th 32-bit argument (0 = first).
    uint32_t Arg(int n) const { return mem.Read32(r[ESP] + 4 + 4 * (uint32_t)n); }

    // Call a guest function with stdcall/cdecl stack arguments (pushed right to
    // left) and run until it returns. Throws Fault. Re-entrant (host functions
    // may call back into the guest).
    uint32_t Call(uint32_t fn, const std::vector<uint32_t>& args, uint64_t max_steps);

    // Bump allocator for guest heap memory (zeroed, 16-byte aligned).
    uint32_t Alloc(uint32_t size);
    uint64_t Steps() const { return steps_; }

    // Stack and well-known regions.
    static constexpr uint32_t kHostBase = 0x7E000000;   // host function thunks
    static constexpr uint32_t kReturnStop = 0x7E7FFFF0; // a Call returns here
    static constexpr uint32_t kStackTop = 0x7F000000;
    static constexpr uint32_t kStackSize = 1u << 20;
    static constexpr uint32_t kHeapBase = 0x40000000;
    static constexpr uint32_t kHeapLimit = 0x48000000; // 128 MiB of guest heap at most
    static constexpr uint32_t kTib = 0x7FFD0000;

private:
    struct Host {
        std::string name;
        int nargs;
        Conv conv;
        HostFn fn;
    };
    void Step();
    void RunHost(uint32_t addr);

    // decoding state of the current instruction
    struct Op {
        bool reg;
        int idx;      // register number when reg
        uint32_t ea;  // effective address otherwise
    };
    uint8_t Fetch8();
    uint16_t Fetch16();
    uint32_t Fetch32();
    Op ModRM(uint8_t modrm);
    uint32_t GetR(int idx, int sz) const;
    void SetR(int idx, int sz, uint32_t v);
    uint32_t Get(const Op& o, int sz);
    void Set(const Op& o, int sz, uint32_t v);
    void Push(uint32_t v);
    uint32_t Pop();
    bool Cond(int cc) const;
    uint32_t Alu(int op, uint32_t a, uint32_t b, int sz);
    uint32_t IncDec(uint32_t a, bool dec, int sz);
    uint32_t Shift(int op, uint32_t a, uint32_t cnt, int sz);
    void SetSZP(uint32_t res, int sz);
    void Group3(const Op& o, int sub, int sz);
    void StringOp(uint8_t op, int sz, int rep);
    void TwoByte();

    std::deque<Host> hosts_;  // a deque: host functions add hosts while they run (no reallocation)
    uint32_t heap_next_ = kHeapBase;
    uint64_t steps_ = 0;
    uint64_t limit_ = 0;
    int opsize_ = 4;       // operand size of the current instruction
    bool fs_ = false;      // FS segment override
    uint32_t start_eip_ = 0;
    uint16_t fpu_cw_ = 0x037F;
};

}  // namespace x86
}  // namespace kk4s
