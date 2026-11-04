// kk4s: prints a TJS data pack (`TJS/ns0` / `TJS/4s0`) as canonical JSON.
// Build:
//   g++ -std=c++17 -O2 -I compat tests/pbd/pbd_dump.cpp compat/pbd/pbd.cpp \
//       -llz4 -lxxhash -o pbd_dump
#include "pbd/pbd.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

void Quote(const std::string& s, std::string& out) {
    static const char* hex = "0123456789abcdef";
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out.push_back(hex[c >> 4]);
                    out.push_back(hex[c & 15]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

void Dump(const kk4s::pbd::Value& v, std::string& out) {
    using Type = kk4s::pbd::Value::Type;
    switch (v.type) {
        case Type::Void: out += "null"; break;
        case Type::String: Quote(v.string, out); break;
        case Type::Integer: out += std::to_string(static_cast<long long>(v.integer)); break;
        case Type::Double: {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.17g", v.real);
            out += buf;
            break;
        }
        case Type::Array:
            out.push_back('[');
            for (std::size_t i = 0; i < v.array.size(); i++) {
                if (i) out.push_back(',');
                Dump(v.array[i], out);
            }
            out.push_back(']');
            break;
        case Type::Dictionary:
            out.push_back('{');
            for (std::size_t i = 0; i < v.dictionary.size(); i++) {
                if (i) out.push_back(',');
                Quote(v.dictionary[i].first, out);
                out.push_back(':');
                Dump(v.dictionary[i].second, out);
            }
            out.push_back('}');
            break;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: pbd_dump FILE\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    kk4s::pbd::Value root;
    std::string error;
    if (!kk4s::pbd::Decode(data.data(), data.size(), root, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::string out;
    Dump(root, out);
    std::printf("%s\n", out.c_str());
    return 0;
}
