// kk4s: prints the canonical text of a PSB file (same format as `tools/psb_ref.py --canon`).
// Build: g++ -std=c++17 -O2 -I compat tests/psb/psb_dump.cpp compat/psb/psb.cpp -lz -o psb_dump
#include "psb/psb.h"

#include <zlib.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
class Dump : public kk4s::psb::Visitor {
public:
    std::string out;
    void Undefined() override { Sep(); out += "undefined"; }
    void Null() override { Sep(); out += "null"; }
    void Bool(bool v) override { Sep(); out += v ? "true" : "false"; }
    void Int(int64_t v) override { Sep(); out += std::to_string((long long)v); }
    void Double(double v) override { Sep(); char b[64]; std::snprintf(b, sizeof b, "%.17g", v); out += b; }
    void String(const std::string& s) override { Sep(); Quote(s); }
    void Resource(uint32_t i, const uint8_t* d, size_t n) override {
        Sep();
        char b[96];
        std::snprintf(b, sizeof b, "<res %u %zu %08x>", i, n, (unsigned)adler32(adler32(0, nullptr, 0), d, (uInt)n));
        out += b;
    }
    void Extra(uint32_t i) override { Sep(); out += "<extra " + std::to_string(i) + ">"; }
    void BeginObject(size_t) override { Sep(); out += "{"; first_.push_back(true); key_ = false; }
    void Key(const std::string& k) override { Sep(); Quote(k); out += ":"; key_ = true; }
    void EndObject() override { out += "}"; first_.pop_back(); }
    void BeginArray(size_t) override { Sep(); out += "["; first_.push_back(true); }
    void EndArray() override { out += "]"; first_.pop_back(); }

private:
    // a comma goes before every element except the first of a container and values after a key
    void Sep() {
        if (key_) { key_ = false; return; }
        if (first_.empty()) return;
        if (first_.back()) first_.back() = false; else out += ",";
    }
    void Quote(const std::string& s) {
        out += '"';
        for (unsigned char c : s) {
            if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
            else if (c >= 0x20 && c < 0x7f) out += (char)c;
            else { char b[8]; std::snprintf(b, sizeof b, "\\x%02x", c); out += b; }
        }
        out += '"';
    }
    std::vector<bool> first_;
    bool key_ = false;
};
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: psb_dump FILE\n"); return 2; }
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), {});
    kk4s::psb::Reader r;
    if (!r.Open(data.data(), data.size())) { std::printf("ERROR %s\n", r.error().c_str()); return 1; }
    Dump d;
    if (!r.Walk(d)) { std::printf("ERROR %s\n", r.error().c_str()); return 1; }
    std::printf("%s\n", d.out.c_str());
    return 0;
}
