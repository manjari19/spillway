#pragma once

// A deliberately small JSON reader/writer, enough for save files.
// Objects use std::map so output is always in the same (sorted) key order.

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace spillway::json {

struct Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;

struct Value {
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> v = nullptr;

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : v(b) {}
    Value(double d) : v(d) {}
    Value(int i) : v(static_cast<double>(i)) {}
    Value(const char* s) : v(std::string(s)) {}
    Value(std::string s) : v(std::move(s)) {}
    Value(Array a) : v(std::move(a)) {}
    Value(Object o) : v(std::move(o)) {}

    bool isObject() const { return std::holds_alternative<Object>(v); }
    bool isArray() const { return std::holds_alternative<Array>(v); }
    const Object* object() const { return std::get_if<Object>(&v); }
    const Array* array() const { return std::get_if<Array>(&v); }

    // Typed lookups with a fallback, so readers tolerate missing/wrong fields.
    double number(double fallback) const { auto p = std::get_if<double>(&v); return p ? *p : fallback; }
    bool boolean(bool fallback) const { auto p = std::get_if<bool>(&v); return p ? *p : fallback; }
    const Value& operator[](const std::string& key) const;
};

// Returns nullopt on any syntax error (callers then start fresh).
std::optional<Value> parse(std::string_view text);
std::string dump(const Value& value, int indent = 2);

} // namespace spillway::json
