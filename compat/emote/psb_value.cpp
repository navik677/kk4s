// kk4s E-mote runtime: normalized PSB value model. See psb_value.h.
#include "emote/psb_value.h"

#include <cstdlib>

namespace kk4s {
namespace emote {

const char* ValueTypeName(Value::Type t) {
    switch (t) {
    case Value::Type::Undefined: return "undefined";
    case Value::Type::Null: return "null";
    case Value::Type::Bool: return "bool";
    case Value::Type::Int: return "int";
    case Value::Type::Double: return "double";
    case Value::Type::String: return "string";
    case Value::Type::Resource: return "resource";
    case Value::Type::Extra: return "extra";
    case Value::Type::Array: return "array";
    case Value::Type::Object: return "object";
    }
    return "?";
}

Value Value::MakeNull() { Value v; v.type_ = Type::Null; return v; }
Value Value::MakeBool(bool b) { Value v; v.type_ = Type::Bool; v.b_ = b; return v; }
Value Value::MakeInt(int64_t i) { Value v; v.type_ = Type::Int; v.i_ = i; return v; }
Value Value::MakeDouble(double d) { Value v; v.type_ = Type::Double; v.d_ = d; return v; }
Value Value::MakeString(std::string s) { Value v; v.type_ = Type::String; v.s_ = std::move(s); return v; }
Value Value::MakeResource(uint32_t index) { Value v; v.type_ = Type::Resource; v.i_ = index; return v; }
Value Value::MakeExtra(uint32_t index) { Value v; v.type_ = Type::Extra; v.i_ = index; return v; }
Value Value::MakeArray() { Value v; v.type_ = Type::Array; return v; }
Value Value::MakeObject() { Value v; v.type_ = Type::Object; return v; }

const Value& Value::UndefinedValue() {
    static const Value u;
    return u;
}

double Value::number() const {
    if (type_ == Type::Int) return (double)i_;
    if (type_ == Type::Double) return d_;
    return 0.0;
}

size_t Value::size() const {
    if (type_ == Type::Array) return items_.size();
    if (type_ == Type::Object) return members_.size();
    return 0;
}

const Value& Value::operator[](size_t i) const {
    if (type_ == Type::Array && i < items_.size()) return items_[i];
    return UndefinedValue();
}

const Value* Value::Find(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    for (const Member& m : members_)
        if (m.first == key) return &m.second;
    return nullptr;
}

const Value* Value::FindPath(const std::string& path) const {
    const Value* cur = this;
    size_t p = 0;
    while (cur && p <= path.size()) {
        size_t e = path.find('/', p);
        if (e == std::string::npos) e = path.size();
        std::string part = path.substr(p, e - p);
        if (!part.empty()) {
            if (cur->is_array()) {
                char* end = nullptr;
                unsigned long idx = std::strtoul(part.c_str(), &end, 10);
                if (!end || *end || idx >= cur->items_.size()) return nullptr;
                cur = &cur->items_[idx];
            } else {
                cur = cur->Find(part);
            }
        }
        if (e == path.size()) break;
        p = e + 1;
    }
    return cur;
}

double Value::GetNumber(const std::string& key, double def) const {
    const Value* v = Find(key);
    return v && v->is_number() ? v->number() : def;
}

int64_t Value::GetInt(const std::string& key, int64_t def) const {
    const Value* v = Find(key);
    if (!v) return def;
    if (v->is_int()) return v->int_value();
    if (v->is_double()) return (int64_t)v->double_value();
    return def;
}

std::string Value::GetString(const std::string& key, const std::string& def) const {
    const Value* v = Find(key);
    return v && v->is_string() ? v->string_value() : def;
}

}  // namespace emote
}  // namespace kk4s
