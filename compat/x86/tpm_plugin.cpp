// kk4s: XP3 filter plugin (.tpm) in the x86 interpreter. See tpm_plugin.h.
#include "tpm_plugin.h"

#include <cstring>

namespace kk4s {
namespace x86 {

namespace {

constexpr uint32_t kChunk = 64u << 10;       // guest buffer: filters are called in pieces like this
constexpr uint32_t kFakeExe = 0x00400000;     // GetModuleHandle(NULL)
constexpr uint32_t kFakeSystemDll = 0x7D000000;
constexpr uint64_t kLinkSteps = 50000000;     // DllMain / V2Link

std::string Narrow(const std::u16string& s) {
    std::string out;
    for (char16_t c : s) out += c < 0x80 ? (char)c : '?';
    return out;
}

// "void ::TVPSetXP3ArchiveExtractionFilter(tTVPXP3ArchiveExtractionFilter)" -> the bare name
std::string BareName(const std::string& sig) {
    size_t paren = sig.find('(');
    std::string head = sig.substr(0, paren);
    size_t colons = head.rfind("::");
    if (colons != std::string::npos) head = head.substr(colons + 2);
    size_t sp = head.rfind(' ');
    if (sp != std::string::npos) head = head.substr(sp + 1);
    return head;
}

}  // namespace

TpmPlugin::TpmPlugin() : m_(new Machine) {}
TpmPlugin::~TpmPlugin() = default;

// A guest tTJSVariantString with Kirikiri's layout (tjsVariantString.h, 32-bit):
// RefCount @0, LongString @4, ShortString[22] @8, Length @52, HeapFlag @56,
// Hint @60. The text always goes to LongString, which tp_stub's inline
// operator const tjs_char*() returns when it is set.
static uint32_t MakeVariantString(Machine& m, const std::u16string& s) {
    uint32_t vs = m.Alloc(64 + 2 * ((uint32_t)s.size() + 1));
    uint32_t text = vs + 64;
    for (size_t i = 0; i < s.size(); ++i) m.mem.Write16(text + 2 * (uint32_t)i, (uint16_t)s[i]);
    m.mem.Write32(vs + 0, 0);  // RefCount (0 = one reference in TJS)
    m.mem.Write32(vs + 4, text);
    m.mem.Write32(vs + 52, (uint32_t)s.size());
    return vs;
}

// The text of a guest ttstr (a pointer to a tTJSVariantString, or 0 = empty).
static std::u16string ReadTtstr(Machine& m, uint32_t self) {
    uint32_t vs = self ? m.mem.Read32(self) : 0;
    if (!vs) return std::u16string();
    uint32_t text = m.mem.Read32(vs + 4);
    return m.mem.ReadWString(text ? text : vs + 8, 1 << 16);
}

uint32_t TpmPlugin::TpStub(const std::string& sig) {
    Machine& m = *m_;
    std::string name = BareName(sig);
    if (notes_.size() < 64) notes_.push_back("asks for " + sig);
    using C = Machine::Conv;
    // tTJSString (ttstr): one pointer to a tTJSVariantString; tp_stub calls its
    // members as __stdcall functions with `this` first
    if (sig.compare(0, 23, "tTJSString::tTJSString(") == 0) {
        if (sig.find("const tjs_char *") != std::string::npos)
            return m.AddHost(sig, 2, C::Stdcall, [](Machine& mm) {
                uint32_t self = mm.Arg(0), str = mm.Arg(1);
                mm.mem.Write32(self, str ? MakeVariantString(mm, mm.mem.ReadWString(str, 1 << 16)) : 0);
                return self;
            });
        if (sig.find("const tjs_nchar *") != std::string::npos || sig.find("const char *") != std::string::npos)
            return m.AddHost(sig, 2, C::Stdcall, [](Machine& mm) {
                uint32_t self = mm.Arg(0), str = mm.Arg(1);
                std::u16string w;
                if (str) for (char c : mm.mem.ReadCString(str, 1 << 16)) w += (char16_t)(uint8_t)c;
                mm.mem.Write32(self, str ? MakeVariantString(mm, w) : 0);
                return self;
            });
        if (sig.find("const tTJSString &") != std::string::npos || sig.find("const ttstr &") != std::string::npos)
            return m.AddHost(sig, 2, C::Stdcall, [](Machine& mm) {
                uint32_t self = mm.Arg(0), other = mm.Arg(1);
                mm.mem.Write32(self, other ? mm.mem.Read32(other) : 0);  // shared, never freed
                return self;
            });
        if (sig == "tTJSString::tTJSString()")
            return m.AddHost(sig, 1, C::Stdcall, [](Machine& mm) { mm.mem.Write32(mm.Arg(0), 0); return mm.Arg(0); });
    }
    if (sig.find("tTJSString::operator +=(const tjs_char *)") != std::string::npos)
        return m.AddHost(sig, 2, C::Stdcall, [](Machine& mm) {
            uint32_t self = mm.Arg(0);
            mm.mem.Write32(self, MakeVariantString(mm, ReadTtstr(mm, self) + mm.mem.ReadWString(mm.Arg(1), 1 << 16)));
            return 0u;
        });
    if (sig.find("tTJSString::operator +=(const tTJSString &)") != std::string::npos ||
        sig.find("tTJSString::operator +=(const ttstr &)") != std::string::npos)
        return m.AddHost(sig, 2, C::Stdcall, [](Machine& mm) {
            uint32_t self = mm.Arg(0);
            mm.mem.Write32(self, MakeVariantString(mm, ReadTtstr(mm, self) + ReadTtstr(mm, mm.Arg(1))));
            return 0u;
        });
    if (sig.compare(0, 13, "tTJSString::~") == 0)
        return m.AddHost(sig, 1, C::Stdcall, [](Machine&) { return 0u; });
    if (name == "TVPSetXP3ArchiveExtractionFilter")
        return m.AddHost(name, 1, C::Stdcall, [this](Machine& mm) { filter_ = mm.Arg(0); return 0u; });
    if (name == "TVPThrowPluginUnboundFunctionError")
        return m.AddHost(name, 1, C::Stdcall, [](Machine& mm) -> uint32_t {
            throw Fault{"the plugin needs a function kk4s does not have: " + mm.mem.ReadCString(mm.Arg(0), 512)};
        });
    if (name == "TVPThrowExceptionMessage")
        return m.AddHost(name, 1, C::Stdcall, [](Machine& mm) -> uint32_t {
            throw Fault{"the plugin raised an error: " + Narrow(mm.mem.ReadWString(mm.Arg(0), 512))};
        });
    if (name == "TVPGetAppPath")  // ttstr returned by value: hidden result pointer first
        return m.AddHost(name, 1, C::Stdcall, [](Machine& mm) {
            mm.mem.Write32(mm.Arg(0), MakeVariantString(mm, u"file://./c/game/"));
            return mm.Arg(0);
        });
    if (name == "TVPRegisterStorageMedia" || name == "TVPUnregisterStorageMedia")  // not used, recorded
        return m.AddHost(name, 1, C::Stdcall, [this, name](Machine& mm) {
            if (notes_.size() < 64) notes_.push_back(name + " called (ignored)");
            (void)mm;
            return 0u;
        });
    if (name == "TVPSetCommandLine")  // (const tjs_char *name, const ttstr &value): recorded only
        return m.AddHost(name, 2, C::Stdcall, [this](Machine& mm) {
            std::string opt = Narrow(mm.mem.ReadWString(mm.Arg(0), 256));
            uint32_t vs = mm.Arg(1) ? mm.mem.Read32(mm.Arg(1)) : 0;
            uint32_t text = vs ? mm.mem.Read32(vs + 4) : 0;
            std::string val = text ? Narrow(mm.mem.ReadWString(text, 256)) : "";
            if (notes_.size() < 64) notes_.push_back("sets command line " + opt + "=" + val);
            return 0u;
        });
    if (name == "TVPAddLog" || name == "TVPAddImportantLog")
        return m.AddHost(name, 1, C::Stdcall, [](Machine&) { return 0u; });
    if (name == "TJSAllocVariantString")  // only so that ttstr constants for logs can be built
        return m.AddHost(name, 1, C::Stdcall, [](Machine& mm) { return mm.Alloc(64); });
    return m.AddHost(name, 0, C::Stdcall, [sig](Machine&) -> uint32_t {
        throw Fault{"the plugin called a tp_stub function kk4s does not provide: " + sig};
    });
}

uint32_t TpmPlugin::Win32(const std::string& n) {
    Machine& m = *m_;
    using C = Machine::Conv;
    auto H = [&](int nargs, HostFn fn) { return m.AddHost(n, nargs, C::Stdcall, std::move(fn)); };
    auto Cd = [&](HostFn fn) { return m.AddHost(n, 0, C::Cdecl, std::move(fn)); };
    auto zero = [](Machine&) { return 0u; };
    auto one = [](Machine&) { return 1u; };
    auto fail = [n](Machine&) -> uint32_t { throw Fault{"the plugin called " + n + " (not provided by kk4s)"}; };

    // modules
    if (n == "GetModuleHandleA" || n == "GetModuleHandleW")
        return H(1, [](Machine& mm) { return mm.Arg(0) ? kFakeSystemDll : kFakeExe; });
    if (n == "GetModuleHandleExA" || n == "GetModuleHandleExW")
        return H(3, [](Machine& mm) { if (mm.Arg(2)) mm.mem.Write32(mm.Arg(2), kFakeExe); return 1u; });
    if (n == "LoadLibraryA" || n == "LoadLibraryW") return H(1, [](Machine&) { return kFakeSystemDll; });
    if (n == "LoadLibraryExA" || n == "LoadLibraryExW") return H(3, [](Machine&) { return kFakeSystemDll; });
    if (n == "FreeLibrary" || n == "DisableThreadLibraryCalls") return H(1, one);
    if (n == "GetProcAddress")
        return H(2, [this](Machine& mm) -> uint32_t {
            uint32_t p = mm.Arg(1);
            if (p < 0x10000) return 0;  // by ordinal
            return Win32(mm.mem.ReadCString(p, 256));
        });
    if (n == "GetModuleFileNameA" || n == "GetModuleFileNameW")
        return H(3, [n](Machine& mm) {
            static const char path[] = "C:\\Game\\game.exe";
            uint32_t dst = mm.Arg(1), size = mm.Arg(2), len = sizeof(path) - 1;
            if (!size) return 0u;
            if (len >= size) len = size - 1;
            bool wide = n.back() == 'W';
            for (uint32_t i = 0; i <= len; ++i) {
                char c = i < len ? path[i] : 0;
                if (wide) mm.mem.Write16(dst + 2 * i, (uint16_t)c); else mm.mem.Write8(dst + i, (uint8_t)c);
            }
            return len;
        });
    // errors
    if (n == "GetLastError") return H(0, [this](Machine&) { return last_error_; });
    if (n == "SetLastError") return H(1, [this](Machine& mm) { last_error_ = mm.Arg(0); return 0u; });
    // memory: a bump heap, nothing is freed
    if (n == "GetProcessHeap") return H(0, one);
    if (n == "HeapCreate") return H(3, one);
    if (n == "HeapDestroy") return H(1, one);
    if (n == "HeapAlloc") return H(3, [](Machine& mm) { return mm.Alloc(mm.Arg(2)); });
    if (n == "HeapFree") return H(3, one);
    if (n == "HeapSize") return H(3, [](Machine&) { return 0xFFFFFFFFu; });
    if (n == "HeapReAlloc")
        return H(4, [](Machine& mm) {
            uint32_t old = mm.Arg(2), size = mm.Arg(3);
            uint32_t p = mm.Alloc(size);
            if (old) {  // the old size is unknown: copy what is mapped
                std::vector<uint8_t> tmp(size);
                for (uint32_t i = 0; i < size && mm.mem.IsMapped(old + i); ++i) tmp[i] = mm.mem.Read8(old + i);
                mm.mem.Write(p, tmp.data(), size);
            }
            return p;
        });
    if (n == "VirtualAlloc") return H(4, [](Machine& mm) { return mm.Alloc(mm.Arg(1)); });
    if (n == "VirtualFree") return H(3, one);
    if (n == "VirtualProtect")
        return H(4, [](Machine& mm) { if (mm.Arg(3)) mm.mem.Write32(mm.Arg(3), 0x40); return 1u; });
    if (n == "LocalAlloc" || n == "GlobalAlloc") return H(2, [](Machine& mm) { return mm.Alloc(mm.Arg(1)); });
    if (n == "LocalFree" || n == "GlobalFree") return H(1, zero);
    if (n == "GlobalLock") return H(1, [](Machine& mm) { return mm.Arg(0); });
    if (n == "GlobalUnlock") return H(1, one);
    // synchronisation: one thread
    if (n == "InitializeCriticalSection" || n == "EnterCriticalSection" || n == "LeaveCriticalSection" ||
        n == "DeleteCriticalSection")
        return H(1, zero);
    if (n == "TryEnterCriticalSection") return H(1, one);
    if (n == "InitializeCriticalSectionAndSpinCount") return H(2, one);
    if (n == "InitializeCriticalSectionEx") return H(3, one);
    if (n == "InterlockedIncrement")
        return H(1, [](Machine& mm) { uint32_t v = mm.mem.Read32(mm.Arg(0)) + 1; mm.mem.Write32(mm.Arg(0), v); return v; });
    if (n == "InterlockedDecrement")
        return H(1, [](Machine& mm) { uint32_t v = mm.mem.Read32(mm.Arg(0)) - 1; mm.mem.Write32(mm.Arg(0), v); return v; });
    if (n == "InterlockedExchange")
        return H(2, [](Machine& mm) { uint32_t v = mm.mem.Read32(mm.Arg(0)); mm.mem.Write32(mm.Arg(0), mm.Arg(1)); return v; });
    if (n == "InterlockedExchangeAdd")
        return H(2, [](Machine& mm) { uint32_t v = mm.mem.Read32(mm.Arg(0)); mm.mem.Write32(mm.Arg(0), v + mm.Arg(1)); return v; });
    if (n == "InterlockedCompareExchange")
        return H(3, [](Machine& mm) {
            uint32_t v = mm.mem.Read32(mm.Arg(0));
            if (v == mm.Arg(2)) mm.mem.Write32(mm.Arg(0), mm.Arg(1));
            return v;
        });
    // thread / fiber local storage
    if (n == "TlsAlloc" || n == "FlsAlloc")
        return H(n == "FlsAlloc" ? 1 : 0, [this](Machine&) { tls_.push_back(0); return (uint32_t)tls_.size() - 1; });
    if (n == "TlsFree" || n == "FlsFree") return H(1, one);
    if (n == "TlsGetValue" || n == "FlsGetValue")
        return H(1, [this](Machine& mm) { uint32_t i = mm.Arg(0); return i < tls_.size() ? tls_[i] : 0u; });
    if (n == "TlsSetValue" || n == "FlsSetValue")
        return H(2, [this](Machine& mm) {
            uint32_t i = mm.Arg(0);
            if (i >= tls_.size()) return 0u;
            tls_[i] = mm.Arg(1);
            return 1u;
        });
    // process / time / system information
    if (n == "GetCurrentThreadId" || n == "GetCurrentProcessId") return H(0, one);
    if (n == "GetCurrentProcess" || n == "GetCurrentThread") return H(0, [](Machine&) { return 0xFFFFFFFFu; });
    if (n == "GetTickCount") return H(0, [](Machine&) { return 100000u; });
    if (n == "QueryPerformanceCounter" || n == "QueryPerformanceFrequency" || n == "GetSystemTimeAsFileTime")
        return H(1, [](Machine& mm) {
            if (mm.Arg(0)) { mm.mem.Write32(mm.Arg(0), 1000000); mm.mem.Write32(mm.Arg(0) + 4, 0); }
            return 1u;
        });
    if (n == "GetLocalTime" || n == "GetSystemTime")
        return H(1, [](Machine& mm) {
            static const uint16_t t[8] = {2010, 1, 5, 1, 12, 0, 0, 0};
            for (int i = 0; i < 8; ++i) mm.mem.Write16(mm.Arg(0) + 2 * i, t[i]);
            return 0u;
        });
    if (n == "GetVersion") return H(0, [](Machine&) { return 0x0A280105u; });  // Windows XP 5.1
    if (n == "GetVersionExA" || n == "GetVersionExW")
        return H(1, [](Machine& mm) {
            uint32_t p = mm.Arg(0);
            mm.mem.Write32(p + 4, 5); mm.mem.Write32(p + 8, 1); mm.mem.Write32(p + 12, 2600); mm.mem.Write32(p + 16, 2);
            return 1u;
        });
    if (n == "GetSystemInfo")
        return H(1, [](Machine& mm) {
            uint32_t p = mm.Arg(0);
            mm.mem.Write32(p + 4, 4096);         // page size
            mm.mem.Write32(p + 8, 0x10000);
            mm.mem.Write32(p + 12, 0x7FFEFFFF);
            mm.mem.Write32(p + 16, 1);
            mm.mem.Write32(p + 20, 1);           // processors
            mm.mem.Write32(p + 24, 586);
            mm.mem.Write32(p + 28, 0x10000);     // allocation granularity
            return 0u;
        });
    if (n == "IsProcessorFeaturePresent" || n == "IsDebuggerPresent") return H(1 - (n == "IsDebuggerPresent"), zero);
    if (n == "Sleep" || n == "OutputDebugStringA" || n == "OutputDebugStringW") return H(1, zero);
    if (n == "CloseHandle") return H(1, one);
    if (n == "EncodePointer" || n == "DecodePointer") return H(1, [](Machine& mm) { return mm.Arg(0); });
    if (n == "SetUnhandledExceptionFilter") return H(1, zero);
    if (n == "UnhandledExceptionFilter") return H(1, zero);
    // process startup data the CRT reads
    if (n == "GetCommandLineA" || n == "GetCommandLineW")
        return H(0, [n](Machine& mm) {
            uint32_t p = mm.Alloc(64);
            const char* s = "game.exe";
            for (int i = 0; s[i]; ++i) {
                if (n.back() == 'W') mm.mem.Write16(p + 2 * i, (uint16_t)s[i]); else mm.mem.Write8(p + i, (uint8_t)s[i]);
            }
            return p;
        });
    if (n == "GetEnvironmentStrings" || n == "GetEnvironmentStringsA" || n == "GetEnvironmentStringsW")
        return H(0, [](Machine& mm) { return mm.Alloc(16); });  // empty block
    if (n == "FreeEnvironmentStringsA" || n == "FreeEnvironmentStringsW") return H(1, one);
    if (n == "GetEnvironmentVariableA" || n == "GetEnvironmentVariableW") return H(3, zero);
    if (n == "GetStartupInfoA" || n == "GetStartupInfoW")
        return H(1, [](Machine& mm) { mm.mem.Write32(mm.Arg(0), 68); return 0u; });
    if (n == "GetStdHandle") return H(1, [](Machine&) { return 0xFFFFFFFFu; });
    if (n == "GetFileType") return H(1, zero);
    if (n == "SetHandleCount") return H(1, [](Machine& mm) { return mm.Arg(0); });
    if (n == "GetACP") return H(0, [](Machine&) { return 932u; });
    if (n == "GetOEMCP") return H(0, [](Machine&) { return 932u; });
    if (n == "IsValidCodePage") return H(1, one);
    if (n == "GetCPInfo")
        return H(2, [](Machine& mm) {
            uint32_t p = mm.Arg(1);
            mm.mem.Write32(p, 2);   // MaxCharSize
            mm.mem.Write8(p + 4, '?');
            mm.mem.Write8(p + 6, 0x81); mm.mem.Write8(p + 7, 0x9F);  // Shift_JIS lead byte ranges
            mm.mem.Write8(p + 8, 0xE0); mm.mem.Write8(p + 9, 0xFC);
            return 1u;
        });
    if (n == "MultiByteToWideChar")
        return H(6, [](Machine& mm) {
            uint32_t src = mm.Arg(2), dst = mm.Arg(4), cap = mm.Arg(5);
            int32_t len = (int32_t)mm.Arg(3);
            std::string s = len < 0 ? mm.mem.ReadCString(src, 65536) + '\0' : std::string(len, '\0');
            if (len >= 0) mm.mem.Read(src, &s[0], (size_t)len);
            if (!cap) return (uint32_t)s.size();
            uint32_t n2 = (uint32_t)s.size() < cap ? (uint32_t)s.size() : cap;
            for (uint32_t i = 0; i < n2; ++i) mm.mem.Write16(dst + 2 * i, (uint8_t)s[i]);
            return n2;
        });
    if (n == "WideCharToMultiByte")
        return H(8, [](Machine& mm) {
            uint32_t src = mm.Arg(2), dst = mm.Arg(4), cap = mm.Arg(5);
            int32_t len = (int32_t)mm.Arg(3);
            std::u16string s = len < 0 ? mm.mem.ReadWString(src, 65536) + u'\0' : std::u16string(len, u'\0');
            if (len >= 0) for (int32_t i = 0; i < len; ++i) s[i] = mm.mem.Read16(src + 2 * (uint32_t)i);
            if (!cap) return (uint32_t)s.size();
            uint32_t n2 = (uint32_t)s.size() < cap ? (uint32_t)s.size() : cap;
            for (uint32_t i = 0; i < n2; ++i) mm.mem.Write8(dst + i, s[i] < 0x80 ? (uint8_t)s[i] : '?');
            return n2;
        });
    if (n == "LCMapStringW" || n == "LCMapStringA" || n == "LCMapStringEx")
        return H(n == "LCMapStringEx" ? 9 : 6, [n](Machine& mm) {
            // case mapping only (LCMAP_LOWERCASE 0x100, LCMAP_UPPERCASE 0x200)
            bool wide = n != "LCMapStringA";
            uint32_t flags = mm.Arg(1), src = mm.Arg(2), dst = mm.Arg(4), cap = mm.Arg(5);
            int32_t len = (int32_t)mm.Arg(3);
            if (len < 0) len = (int32_t)(wide ? mm.mem.ReadWString(src, 1 << 16).size() : mm.mem.ReadCString(src, 1 << 16).size()) + 1;
            if (!cap) return (uint32_t)len;
            if ((uint32_t)len > cap) return 0u;
            for (int32_t i = 0; i < len; ++i) {
                uint32_t c = wide ? mm.mem.Read16(src + 2 * (uint32_t)i) : mm.mem.Read8(src + (uint32_t)i);
                if ((flags & 0x100) && c >= 'A' && c <= 'Z') c += 32;
                if ((flags & 0x200) && c >= 'a' && c <= 'z') c -= 32;
                if (wide) mm.mem.Write16(dst + 2 * (uint32_t)i, (uint16_t)c); else mm.mem.Write8(dst + (uint32_t)i, (uint8_t)c);
            }
            return (uint32_t)len;
        });
    if (n == "GetStringTypeW" || n == "GetStringTypeA" || n == "GetStringTypeExW")
        return H(n == "GetStringTypeA" || n == "GetStringTypeExW" ? 5 : 4, [n](Machine& mm) {
            // CT_CTYPE1 classes for ASCII; everything else is "alpha"
            bool ex = n != "GetStringTypeW";
            uint32_t src = mm.Arg(ex ? 2 : 1), dst = mm.Arg(ex ? 4 : 3);
            int32_t len = (int32_t)mm.Arg(ex ? 3 : 2);
            bool wide = n.find('W') != std::string::npos;
            if (len < 0) len = (int32_t)(wide ? mm.mem.ReadWString(src, 1 << 16).size() : mm.mem.ReadCString(src, 1 << 16).size()) + 1;
            for (int32_t i = 0; i < len; ++i) {
                uint32_t c = wide ? mm.mem.Read16(src + 2 * (uint32_t)i) : mm.mem.Read8(src + (uint32_t)i);
                uint16_t t = 0;
                if (c >= 'A' && c <= 'Z') t |= 0x101;
                else if (c >= 'a' && c <= 'z') t |= 0x102;
                else if (c >= '0' && c <= '9') t |= 0x4;
                else if (c == ' ' || c == '\t') t |= 0x48;
                else if (c == '\n' || c == '\r' || c == '\v' || c == '\f') t |= 0x28;
                else if (c < 0x20 || c == 0x7f) t |= 0x20;
                else if (c < 0x80) t |= 0x10;
                else t |= 0x100;
                if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) t |= 0x80;
                mm.mem.Write16(dst + 2 * (uint32_t)i, t);
            }
            return 1u;
        });
    if (n == "lstrlenA") return H(1, [](Machine& mm) { return mm.Arg(0) ? (uint32_t)mm.mem.ReadCString(mm.Arg(0), 1 << 20).size() : 0u; });
    if (n == "lstrlenW") return H(1, [](Machine& mm) { return mm.Arg(0) ? (uint32_t)mm.mem.ReadWString(mm.Arg(0), 1 << 20).size() : 0u; });
    // files: none can be opened (a plugin that reads its key from a file fails cleanly)
    if (n == "CreateFileA" || n == "CreateFileW") return H(7, [](Machine&) { return 0xFFFFFFFFu; });
    if (n == "GetFileAttributesA" || n == "GetFileAttributesW") return H(1, [](Machine&) { return 0xFFFFFFFFu; });
    // the end of the process / dialogs: a plugin that gets here gave up
    if (n == "ExitProcess" || n == "FatalAppExitA" || n == "FatalAppExitW") return H(1, fail);
    if (n == "TerminateProcess") return H(2, fail);
    if (n == "RaiseException" || n == "RtlUnwind") return H(4, fail);
    if (n == "MessageBoxA" || n == "MessageBoxW")
        return H(4, [n](Machine& mm) -> uint32_t {
            std::string text = n.back() == 'W' ? Narrow(mm.mem.ReadWString(mm.Arg(1), 256)) : mm.mem.ReadCString(mm.Arg(1), 256);
            throw Fault{"the plugin shows a message box: " + text};
        });

    // msvcrt (cdecl)
    if (n == "malloc") return Cd([](Machine& mm) { return mm.Alloc(mm.Arg(0)); });
    if (n == "calloc") return Cd([](Machine& mm) { return mm.Alloc(mm.Arg(0) * mm.Arg(1)); });
    if (n == "free" || n == "_lock" || n == "_unlock") return Cd(zero);
    if (n == "realloc")
        return Cd([](Machine& mm) {
            uint32_t old = mm.Arg(0), size = mm.Arg(1);
            uint32_t p = mm.Alloc(size);
            if (old) {
                std::vector<uint8_t> tmp(size);
                for (uint32_t i = 0; i < size && mm.mem.IsMapped(old + i); ++i) tmp[i] = mm.mem.Read8(old + i);
                mm.mem.Write(p, tmp.data(), size);
            }
            return p;
        });
    if (n == "memcpy" || n == "memmove")
        return Cd([](Machine& mm) {
            std::vector<uint8_t> tmp(mm.Arg(2));
            if (!tmp.empty()) { mm.mem.Read(mm.Arg(1), tmp.data(), tmp.size()); mm.mem.Write(mm.Arg(0), tmp.data(), tmp.size()); }
            return mm.Arg(0);
        });
    if (n == "memset")
        return Cd([](Machine& mm) {
            std::vector<uint8_t> tmp(mm.Arg(2), (uint8_t)mm.Arg(1));
            if (!tmp.empty()) mm.mem.Write(mm.Arg(0), tmp.data(), tmp.size());
            return mm.Arg(0);
        });
    if (n == "strlen") return Cd([](Machine& mm) { return (uint32_t)mm.mem.ReadCString(mm.Arg(0), 1 << 20).size(); });
    if (n == "_initterm" || n == "_initterm_e")
        return Cd([n](Machine& mm) {
            for (uint32_t p = mm.Arg(0); p < mm.Arg(1); p += 4) {
                uint32_t f = mm.mem.Read32(p);
                if (!f) continue;
                uint32_t r = mm.Call(f, {}, kLinkSteps);
                if (n == "_initterm_e" && r) return r;
            }
            return 0u;
        });
    if (n == "atexit" || n == "_onexit" || n == "__dllonexit") return Cd([](Machine& mm) { return mm.Arg(0); });
    if (n == "_amsg_exit" || n == "abort" || n == "exit" || n == "_exit" || n == "_purecall") return Cd(fail);

    return m.AddHost(n, 0, C::Stdcall, fail);  // unknown: fails only if called (data imports read zeros)
}

uint32_t TpmPlugin::Resolve(const std::string& dll, const std::string& name, uint16_t ordinal) {
    if (name.empty()) {
        std::string what = dll + "!#" + std::to_string(ordinal);
        return m_->AddHost(what, 0, Machine::Conv::Stdcall, [what](Machine&) -> uint32_t {
            throw Fault{"the plugin called " + what + " (an import by ordinal)"};
        });
    }
    return Win32(name);
}

bool TpmPlugin::Load(const std::vector<uint8_t>& file, std::string& error) {
    // The exporter's calling convention is not visible from outside: cdecl
    // callers usually clean up with "add esp, n" after the call, but code that
    // stores arguments with mov [esp+n] never does. Try the guess first, then
    // both conventions explicitly, each on a fresh machine; the one where
    // V2Link sets a filter wins.
    const Machine::Conv convs[] = {Machine::Conv::Auto, Machine::Conv::Cdecl, Machine::Conv::Stdcall};
    const char* names[] = {"guessed", "cdecl", "stdcall"};
    std::string first_error;
    for (int i = 0; i < 3; ++i) {
        m_.reset(new Machine);
        filter_ = info_ = buf_ = exporter_ = 0;
        tls_.clear();
        std::vector<std::string> keep = notes_;
        notes_.clear();
        std::string err;
        if (LoadWith(file, convs[i], err)) {
            notes_.insert(notes_.begin(), std::string("exporter convention: ") + names[i]);
            return true;
        }
        if (first_error.empty()) first_error = err;
        // a file that is not a plugin at all fails the same way every time
        if (err.find("not a loadable") != std::string::npos || err.find("no V2Link") != std::string::npos) break;
    }
    error = first_error;
    return false;
}

bool TpmPlugin::LoadWith(const std::vector<uint8_t>& file, Machine::Conv conv, std::string& error) {
    Machine& m = *m_;
    try {
        img_ = LoadPe32(m, file, [this](const std::string& d, const std::string& n, uint16_t o) { return Resolve(d, n, o); });
        m.mem.Map(kFakeExe, 0x1000);
        m.mem.Write16(kFakeExe, 0x5A4D);  // "MZ"
        uint32_t v2link = img_.Export("V2Link");
        if (!v2link) { error = "no V2Link export (not a Kirikiri 2 plugin)"; return false; }

        // the exporter object: one pointer to a two-entry vtable
        uint32_t qf = m.AddHost("QueryFunctions", 4, conv, [this](Machine& mm) {
            uint32_t names = mm.Arg(1), funcs = mm.Arg(2), count = mm.Arg(3);
            for (uint32_t i = 0; i < count; ++i)
                mm.mem.Write32(funcs + 4 * i, TpStub(Narrow(mm.mem.ReadWString(mm.mem.Read32(names + 4 * i), 1024))));
            return 1u;
        });
        uint32_t qn = m.AddHost("QueryFunctionsByNarrowString", 4, conv, [this](Machine& mm) {
            uint32_t names = mm.Arg(1), funcs = mm.Arg(2), count = mm.Arg(3);
            for (uint32_t i = 0; i < count; ++i)
                mm.mem.Write32(funcs + 4 * i, TpStub(mm.mem.ReadCString(mm.mem.Read32(names + 4 * i), 1024)));
            return 1u;
        });
        uint32_t vtbl = m.Alloc(8);
        m.mem.Write32(vtbl, qf);
        m.mem.Write32(vtbl + 4, qn);
        exporter_ = m.Alloc(16);
        m.mem.Write32(exporter_, vtbl);

        if (img_.entry) {
            try {
                if (!m.Call(img_.entry, {img_.base, 1, 0}, kLinkSteps)) notes_.push_back("DllMain returned FALSE");
            } catch (const Fault& f) {
                notes_.push_back("DllMain failed: " + f.what);
            }
        }
        m.Call(v2link, {exporter_}, kLinkSteps);
        if (!filter_) { error = "V2Link did not set an XP3 extraction filter"; return false; }
        info_ = m.Alloc(32);
        buf_ = m.Alloc(kChunk);
        return true;
    } catch (const Fault& f) {
        error = f.what;
        return false;
    }
}

bool TpmPlugin::Filter(uint32_t hash, uint64_t offset, uint8_t* buf, size_t n, std::string& error) {
    if (!filter_) { error = "no filter"; return false; }
    Machine& m = *m_;
    try {
        while (n) {
            uint32_t chunk = n > kChunk ? kChunk : (uint32_t)n;
            m.mem.Write(buf_, buf, chunk);
            // tTVPXP3ExtractionFilterInfo, #pragma pack(4): 24 bytes
            m.mem.Write32(info_ + 0, 24);
            m.mem.Write32(info_ + 4, (uint32_t)offset);
            m.mem.Write32(info_ + 8, (uint32_t)(offset >> 32));
            m.mem.Write32(info_ + 12, buf_);
            m.mem.Write32(info_ + 16, chunk);
            m.mem.Write32(info_ + 20, hash);
            m.Call(filter_, {info_}, 4000ull * chunk + 2000000);
            m.mem.Read(buf_, buf, chunk);
            buf += chunk;
            offset += chunk;
            n -= chunk;
        }
        return true;
    } catch (const Fault& f) {
        error = f.what;
        return false;
    }
}

bool Keystream(TpmPlugin& p, uint32_t hash, size_t n, std::vector<uint8_t>& out, std::string& error) {
    out.assign(n, 0);
    return p.Filter(hash, 0, out.data(), n, error);
}

uint32_t PeriodOf(const std::vector<uint8_t>& k) {
    const uint32_t size = (uint32_t)k.size(), half = size / 2;
    for (uint32_t p = 1; p <= size / 4; ++p) {
        bool ok = true;
        for (uint32_t i = half; i < size && ok; ++i) ok = k[i] == k[i - p];
        if (ok) return p;
    }
    return 0;
}

bool DetectXorModel(TpmPlugin& p, const std::vector<std::pair<uint32_t, std::pair<std::vector<uint8_t>,
                    std::vector<uint8_t>>>>& samples, XorModel& model, std::string& why) {
    std::string err;
    // 1. each verified entry must be raw ^ keystream
    for (const auto& s : samples) {
        std::vector<uint8_t> k;
        if (!Keystream(p, s.first, s.second.first.size(), k, err)) { why = err; return false; }
        for (size_t i = 0; i < k.size(); ++i)
            if ((uint8_t)(s.second.first[i] ^ k[i]) != s.second.second[i]) { why = "not a XOR keystream"; return false; }
    }
    // 2. the keystream settles into a short period, also far away
    const uint32_t len = 4096;
    std::vector<uint32_t> hashes;
    for (const auto& s : samples) if (hashes.size() < 2) hashes.push_back(s.first);
    hashes.push_back(0x12345678u);
    hashes.push_back(0xDEADBEEFu);
    for (uint32_t h : hashes) {
        std::vector<uint8_t> k;
        if (!Keystream(p, h, len, k, err)) { why = err; return false; }
        uint32_t period = PeriodOf(k);
        if (!period) { why = "the keystream does not repeat within 4 KiB"; return false; }
        for (uint64_t off : {(uint64_t)len + 1, (uint64_t)65536 + 7, (uint64_t)(1u << 20) + 3, (uint64_t)123456789,
                             (uint64_t)0x7FFFFF00u}) {
            std::vector<uint8_t> z(256, 0);
            if (!p.Filter(h, off, z.data(), z.size(), err)) { why = err; return false; }
            for (size_t i = 0; i < z.size(); ++i) {
                uint64_t pos = off + i;
                uint8_t want = k[len / 2 + (uint32_t)((pos - len / 2) % period)];
                if (z[i] != want) { why = "the keystream changes beyond 4 KiB"; return false; }
            }
        }
    }
    model.prefix = len / 2;
    model.period = 0;  // per hash, see the decoder
    return true;
}

}  // namespace x86
}  // namespace kk4s
