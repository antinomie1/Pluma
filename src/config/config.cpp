#include "config/config.h"

#include <simdjson.h>

#include <cstdio>
#include <fstream>

namespace config {

Config& Config::Instance() {
    static Config instance;
    return instance;
}

void Config::Load(const std::string& path) {
    path_ = path;
    values_.clear();

    simdjson::dom::parser parser;
    auto loaded = simdjson::padded_string::load(path);
    if (loaded.error()) {
        // Missing file, unreadable, etc. -- start from an empty map; getters
        // fall back to their `def`.
        return;
    }

    simdjson::dom::element doc;
    if (parser.parse(loaded.value()).get(doc)) {
        return; // malformed JSON
    }

    simdjson::dom::object root;
    if (doc.get(root)) {
        return; // top level is not an object
    }

    for (auto [key, value] : root) {
        Value v;
        switch (value.type()) {
            case simdjson::dom::element_type::STRING: {
                std::string_view sv;
                if (value.get(sv)) continue;
                v.type = Value::Type::String;
                v.string_value.assign(sv.data(), sv.size());
                break;
            }
            case simdjson::dom::element_type::BOOL: {
                bool b = false;
                if (value.get(b)) continue;
                v.type = Value::Type::Bool;
                v.bool_value = b;
                break;
            }
            case simdjson::dom::element_type::INT64: {
                int64_t i = 0;
                if (value.get(i)) continue;
                v.type = Value::Type::Int;
                v.int_value = i;
                break;
            }
            case simdjson::dom::element_type::UINT64: {
                uint64_t u = 0;
                if (value.get(u)) continue;
                v.type = Value::Type::Int;
                v.int_value = static_cast<int64_t>(u);
                break;
            }
            default:
                // DOUBLE / ARRAY / OBJECT / NULL_VALUE: not part of the flat
                // scalar config vocabulary -- skip.
                continue;
        }
        values_[std::string(key)] = v;
    }
}

std::string Config::GetString(const char* key, const char* def) const {
    auto it = values_.find(key);
    if (it == values_.end() || it->second.type != Value::Type::String) return def;
    return it->second.string_value;
}

bool Config::GetBool(const char* key, bool def) const {
    auto it = values_.find(key);
    if (it == values_.end() || it->second.type != Value::Type::Bool) return def;
    return it->second.bool_value;
}

int64_t Config::GetInt(const char* key, int64_t def) const {
    auto it = values_.find(key);
    if (it == values_.end() || it->second.type != Value::Type::Int) return def;
    return it->second.int_value;
}

void Config::Set(const char* key, const std::string& value) {
    Value v;
    v.type = Value::Type::String;
    v.string_value = value;
    values_[key] = v;
}

void Config::Set(const char* key, const char* value) {
    Set(key, std::string(value));
}

void Config::Set(const char* key, bool value) {
    Value v;
    v.type = Value::Type::Bool;
    v.bool_value = value;
    values_[key] = v;
}

void Config::Set(const char* key, int64_t value) {
    Value v;
    v.type = Value::Type::Int;
    v.int_value = value;
    values_[key] = v;
}

namespace {

void WriteEscapedString(std::ofstream& out, const std::string& s) {
    out << '"';
    for (char c : s) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out << buf;
                } else {
                    out << c;
                }
        }
    }
    out << '"';
}

} // namespace

void Config::Save() const {
    if (path_.empty()) return;

    std::ofstream out(path_, std::ios::out | std::ios::trunc);
    if (!out) return;

    out << "{\n";
    std::size_t index = 0;
    const std::size_t count = values_.size();
    for (const auto& [key, value] : values_) {
        out << "  ";
        WriteEscapedString(out, key);
        out << ": ";
        switch (value.type) {
            case Value::Type::String:
                WriteEscapedString(out, value.string_value);
                break;
            case Value::Type::Bool:
                out << (value.bool_value ? "true" : "false");
                break;
            case Value::Type::Int:
                out << value.int_value;
                break;
        }
        ++index;
        if (index < count) out << ",";
        out << "\n";
    }
    out << "}\n";
}

} // namespace config
