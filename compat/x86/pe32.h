// kk4s: loader for 32-bit Windows DLLs (PE32, i386) into an x86::Machine.
// Maps the headers and sections, applies base relocations when the preferred
// base is taken, binds imports through a resolver (host functions) and finds
// exports by name. TLS callbacks are not run.
#pragma once

#include "x86emu.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace kk4s {
namespace x86 {

struct PeImage {
    uint32_t base = 0;
    uint32_t size = 0;
    uint32_t entry = 0;  // 0 when the DLL has no entry point
    std::map<std::string, uint32_t> exports;  // name -> guest address
    std::vector<std::string> imports;         // "dll!name" or "dll!#ordinal", for logs
    bool has_tls = false;

    // Exported function by name; also tries the stdcall-decorated "_name@N".
    uint32_t Export(const std::string& name) const;
};

// dll (lower case, e.g. "kernel32.dll"), name (empty for an ordinal import),
// ordinal -> guest address to bind (a host function), never 0.
using ImportResolver = std::function<uint32_t(const std::string& dll, const std::string& name, uint16_t ordinal)>;

// Throws Fault with a reason when the file is not a loadable i386 DLL.
PeImage LoadPe32(Machine& m, const std::vector<uint8_t>& file, const ImportResolver& resolve);

}  // namespace x86
}  // namespace kk4s
