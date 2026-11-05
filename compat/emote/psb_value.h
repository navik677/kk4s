// kk4s E-mote runtime: normalized PSB value model.
//
// A plain tree of values, independent of the container bytes, the cipher, the
// game and the renderer. Everything above the PSB core (adapters, tools, tests)
// reads this model instead of PSB bytes. Objects keep their key order.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace kk4s {
namespace emote {

class Value {
public:
    enum class Type { Undefined, Null, Bool, Int, Double, String, Resource, Extra, Array, Object };
    using Member = std::pair<std::string, Value>;

    Value() = default;
    static Value MakeNull();
    static Value MakeBool(bool b);
    static Value MakeInt(int64_t i);
    static Value MakeDouble(double d);
    static Value MakeString(std::string s);
    static Value MakeResource(uint32_t index);
    static Value MakeExtra(uint32_t index);
    static Value MakeArray();
    static Value MakeObject();

    Type type() const { return type_; }
    bool is_undefined() const { return type_ == Type::Undefined; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_int() const { return type_ == Type::Int; }
    bool is_double() const { return type_ == Type::Double; }
    bool is_number() const { return type_ == Type::Int || type_ == Type::Double; }
    bool is_string() const { return type_ == Type::String; }
    bool is_resource() const { return type_ == Type::Resource; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    bool bool_value() const { return b_; }
    int64_t int_value() const { return i_; }
    double double_value() const { return d_; }
    // Int or Double as double; 0 otherwise.
    double number() const;
    const std::string& string_value() const { return s_; }
    uint32_t resource_index() const { return (uint32_t)i_; }

    // Array / Object
    size_t size() const;
    const std::vector<Value>& items() const { return items_; }
    const std::vector<Member>& members() const { return members_; }
    std::vector<Value>& items() { return items_; }
    std::vector<Member>& members() { return members_; }
    const Value& operator[](size_t i) const;  // Undefined when out of range
    // Object member lookup (first match); nullptr when absent or not an object.
    const Value* Find(const std::string& key) const;
    // "a/b/0/c" path lookup through objects and arrays; nullptr when absent.
    const Value* FindPath(const std::string& path) const;

    // Typed lookups with a default, for adapters.
    double GetNumber(const std::string& key, double def) const;
    int64_t GetInt(const std::string& key, int64_t def) const;
    std::string GetString(const std::string& key, const std::string& def) const;

    static const Value& UndefinedValue();

private:
    Type type_ = Type::Undefined;
    bool b_ = false;
    int64_t i_ = 0;
    double d_ = 0.0;
    std::string s_;
    std::vector<Value> items_;
    std::vector<Member> members_;
};

const char* ValueTypeName(Value::Type t);

}  // namespace emote
}  // namespace kk4s
