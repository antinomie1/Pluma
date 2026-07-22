#pragma once

#include <cstdint>
#include <map>
#include <string>

// Simple flat key/value JSON config store, backed by simdjson for reading
// (simdjson is read-only by design) and a hand-written serializer for
// writing. Keys are flat strings -- "theme.dark" is a literal key, not a
// nested path; simdjson's dom API is only used to walk the parsed document's
// top-level object once at Load() time.
//
// Threading: mirrors the ui::theme / ui::i18n convention -- render-thread-
// exclusive, no locking. Load() once before the first frame
// (render/renderer.cpp); Set()/Save() from frame.cpp while building a frame.
namespace config {

class Config {
public:
    static Config& Instance();

    // Parses `path` into the in-memory map and remembers `path` for Save().
    // A missing file or a parse error leaves the map empty (getters then
    // return their `def`) -- never throws.
    void Load(const std::string& path);

    std::string GetString(const char* key, const char* def) const;
    bool GetBool(const char* key, bool def) const;
    int64_t GetInt(const char* key, int64_t def) const;
    double GetDouble(const char* key, double def) const;

    void Set(const char* key, const std::string& value);
    // Without this overload, a `const char*` argument (e.g.
    // ui::i18n::CurrentLanguageCode()) prefers the bool overload below --
    // pointer-to-bool is a standard conversion, while const char* to
    // std::string is a user-defined one, and overload resolution ranks
    // standard conversions higher. An exact-match const char* overload
    // forwarding to the string one avoids that trap.
    void Set(const char* key, const char* value);
    void Set(const char* key, bool value);
    void Set(const char* key, int64_t value);
    void Set(const char* key, double value);

    // Hand-writes the in-memory map back to the path remembered by Load(), as
    // a flat JSON object with deterministic (sorted) key order. simdjson is
    // read-only, so this goes through std::ofstream directly.
    void Save() const;

private:
    Config() = default;

    struct Value {
        enum class Type { String, Bool, Int, Double } type = Type::String;
        std::string string_value;
        bool bool_value = false;
        int64_t int_value = 0;
        double double_value = 0.0;
    };

    std::string path_;
    std::map<std::string, Value> values_;
};

} // namespace config
