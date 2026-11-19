#include "spillway/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace spillway::json {

const Value& Value::operator[](const std::string& key) const {
    static const Value null;
    const Object* o = object();
    if (!o) return null;
    auto it = o->find(key);
    return it == o->end() ? null : it->second;
}

namespace {

// Recursive-descent parser over a string_view.
struct Parser {
    std::string_view s;
    size_t i = 0;
    int depth = 0;

    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i; }
    bool eat(char c) { ws(); if (i < s.size() && s[i] == c) { ++i; return true; } return false; }
    bool lit(std::string_view w) { if (s.substr(i, w.size()) == w) { i += w.size(); return true; } return false; }

    std::optional<std::string> str() {
        if (!eat('"')) return std::nullopt;
        std::string out;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c == '\\') {
                if (i >= s.size()) return std::nullopt;
                const char e = s[i++];
                switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case '"': case '\\': case '/': out += e; break;
                default: return std::nullopt; // \u escapes are never written by us
                }
            } else {
                out += c;
            }
        }
        if (i >= s.size()) return std::nullopt;
        ++i;
        return out;
    }

    std::optional<Value> value() {
        if (++depth > 64) return std::nullopt;
        ws();
        if (i >= s.size()) return std::nullopt;
        std::optional<Value> result;
        const char c = s[i];
        if (c == '{') {
            ++i;
            Object o;
            if (!eat('}')) {
                do {
                    auto k = str();
                    if (!k || !eat(':')) return std::nullopt;
                    auto v = value();
                    if (!v) return std::nullopt;
                    o[*k] = std::move(*v);
                } while (eat(','));
                if (!eat('}')) return std::nullopt;
            }
            result = Value(std::move(o));
        } else if (c == '[') {
            ++i;
            Array a;
            if (!eat(']')) {
                do {
                    auto v = value();
                    if (!v) return std::nullopt;
                    a.push_back(std::move(*v));
                } while (eat(','));
                if (!eat(']')) return std::nullopt;
            }
            result = Value(std::move(a));
        } else if (c == '"') {
            auto st = str();
            if (!st) return std::nullopt;
            result = Value(std::move(*st));
        } else if (lit("true")) {
            result = Value(true);
        } else if (lit("false")) {
            result = Value(false);
        } else if (lit("null")) {
            result = Value(nullptr);
        } else {
            const std::string num(s.substr(i, s.find_first_of(",}] \t\r\n", i) - i));
            char* end = nullptr;
            const double d = std::strtod(num.c_str(), &end);
            if (num.empty() || end != num.c_str() + num.size() || !std::isfinite(d)) return std::nullopt;
            i += num.size();
            result = Value(d);
        }
        --depth;
        return result;
    }
};

void write(const Value& v, std::string& out, int indent, int level) {
    const std::string pad(static_cast<size_t>(indent * (level + 1)), ' ');
    const std::string padEnd(static_cast<size_t>(indent * level), ' ');
    const char* nl = indent > 0 ? "\n" : "";
    if (std::holds_alternative<std::nullptr_t>(v.v)) out += "null";
    else if (auto b = std::get_if<bool>(&v.v)) out += *b ? "true" : "false";
    else if (auto d = std::get_if<double>(&v.v)) {
        char buf[32];
        if (*d == std::floor(*d) && std::abs(*d) < 1e15) std::snprintf(buf, sizeof buf, "%.0f", *d);
        else std::snprintf(buf, sizeof buf, "%.6g", *d);
        out += buf;
    } else if (auto st = std::get_if<std::string>(&v.v)) {
        out += '"';
        for (char c : *st) {
            if (c == '"' || c == '\\') { out += '\\'; out += c; }
            else if (c == '\n') out += "\\n";
            else out += c;
        }
        out += '"';
    } else if (auto a = std::get_if<Array>(&v.v)) {
        out += '[';
        for (size_t k = 0; k < a->size(); ++k) {
            out += nl + pad;
            write((*a)[k], out, indent, level + 1);
            if (k + 1 < a->size()) out += ',';
        }
        if (!a->empty()) out += nl + padEnd;
        out += ']';
    } else if (auto o = std::get_if<Object>(&v.v)) {
        out += '{';
        size_t k = 0;
        for (const auto& [key, val] : *o) {
            out += nl + pad + '"' + key + "\": ";
            write(val, out, indent, level + 1);
            if (++k < o->size()) out += ',';
        }
        if (!o->empty()) out += nl + padEnd;
        out += '}';
    }
}

} // namespace

std::optional<Value> parse(std::string_view text) {
    Parser p{text};
    auto v = p.value();
    p.ws();
    if (!v || p.i != text.size()) return std::nullopt;
    return v;
}

std::string dump(const Value& value, int indent) {
    std::string out;
    write(value, out, indent, 0);
    out += '\n';
    return out;
}

} // namespace spillway::json
