// kk4s: a Kirikiri 2 XP3 filter plugin (.tpm) loaded into the x86 interpreter.
// No engine dependencies (used by compat/kk4s_tpm.cpp and tests/tpm).
//
// Load() maps the DLL, binds its imports to a small fake Win32 API, runs
// DllMain(PROCESS_ATTACH) and V2Link(exporter). The fake iTVPFunctionExporter
// answers both QueryFunctions (UTF-16 names) and QueryFunctionsByNarrowString,
// for plugins built with the __stdcall (Kirikiri <= 2.24) and the __cdecl
// (2.25+) TJS_INTF_METHOD. Every tp_stub function a plugin asks for gets an
// address; TVPSetXP3ArchiveExtractionFilter records the callback, a few others
// are harmless, the rest fail with their name when they are actually called.
#pragma once

#include "pe32.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kk4s {
namespace x86 {

class TpmPlugin {
public:
    TpmPlugin();
    ~TpmPlugin();

    bool Load(const std::vector<uint8_t>& file, std::string& error);
    bool HasFilter() const { return filter_ != 0; }
    // Run the filter over buf as Kirikiri does (in chunks), in place.
    bool Filter(uint32_t hash, uint64_t offset, uint8_t* buf, size_t n, std::string& error);
    // Things worth logging: exporter calling convention, tp_stub names asked for.
    const std::vector<std::string>& Notes() const { return notes_; }
    uint64_t Steps() const { return m_->Steps(); }

private:
    bool LoadWith(const std::vector<uint8_t>& file, Machine::Conv conv, std::string& error);
    uint32_t Resolve(const std::string& dll, const std::string& name, uint16_t ordinal);
    uint32_t TpStub(const std::string& signature);
    uint32_t Win32(const std::string& name);

    std::unique_ptr<Machine> m_;
    PeImage img_;
    uint32_t filter_ = 0;
    uint32_t info_ = 0;    // guest tTVPXP3ExtractionFilterInfo
    uint32_t buf_ = 0;     // guest buffer
    uint32_t exporter_ = 0;
    std::vector<std::string> notes_;
    std::vector<uint32_t> tls_;
    uint32_t last_error_ = 0;
    std::string fail_;     // set by a fake function that ended the call
};

// A filter that XORs with a keystream depending on (hash, position): position
// p < prefix is K[p], beyond that K[prefix + (p - prefix) % period].
struct XorModel {
    uint32_t prefix = 0;
    uint32_t period = 0;
};

// Smallest period of k over its second half, 0 when none up to size / 4.
uint32_t PeriodOf(const std::vector<uint8_t>& k);

// Keystream of `n` bytes from offset 0 for a hash (the filter run over zeros).
bool Keystream(TpmPlugin& p, uint32_t hash, size_t n, std::vector<uint8_t>& out, std::string& error);

// Decide whether the filter is such a XOR, from the keystreams of a few hashes
// and checks at far offsets. `samples` are (hash, raw, decrypted) of verified
// entries; a XOR filter must reproduce each decrypted entry as raw ^ keystream.
bool DetectXorModel(TpmPlugin& p, const std::vector<std::pair<uint32_t, std::pair<std::vector<uint8_t>,
                    std::vector<uint8_t>>>>& samples, XorModel& model, std::string& why);

}  // namespace x86
}  // namespace kk4s
