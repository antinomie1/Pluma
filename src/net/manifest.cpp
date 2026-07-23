#include "net/manifest.h"

#include <simdjson.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace net {
namespace {

// Whether a library's rules[] permit it on the current OS/arch. Absent
// rules[] means "always applies" (the common case for most libraries).
// Mirrors the official launcher's algorithm: with no matching rule at all the
// library is disallowed, and later matching rules override earlier ones.
bool LibraryAllowedOnThisOS(simdjson::dom::element library) {
#if defined(_WIN32)
    constexpr std::string_view kOsName = "windows";
#elif defined(__APPLE__)
    constexpr std::string_view kOsName = "osx";
#else
    constexpr std::string_view kOsName = "linux";
#endif
    // Arch rules in practice only ever gate 32-bit-specific natives; this
    // project only builds/ships 64-bit, so that's the only value compared.
    constexpr std::string_view kArch = "x86_64";

    simdjson::dom::array rules;
    if (library["rules"].get(rules)) {
        return true; // no rules[] -- unconditionally applies
    }

    bool allowed = false;
    for (simdjson::dom::element rule : rules) {
        std::string_view action;
        if (rule["action"].get(action)) continue;

        bool matches = true;
        simdjson::dom::element os;
        if (!rule["os"].get(os)) {
            std::string_view name;
            if (!os["name"].get(name) && name != kOsName) matches = false;
            std::string_view arch;
            if (!os["arch"].get(arch) && arch != kArch) matches = false;
        }
        if (matches) {
            allowed = (action == "allow");
        }
    }
    return allowed;
}

// Resolves the natives[] map entry for the current OS into a classifiers[]
// key, substituting the ${arch} placeholder LWJGL2-era entries use (e.g.
// "natives-windows-${arch}") -- this project only ever builds/ships 64-bit.
// Returns "" if the library has no natives for this OS.
std::string NativesKeyFor(simdjson::dom::element library) {
#if defined(_WIN32)
    constexpr const char* kKey = "windows";
#elif defined(__APPLE__)
    constexpr const char* kKey = "osx";
#else
    constexpr const char* kKey = "linux";
#endif
    simdjson::dom::element natives;
    if (library["natives"].get(natives)) return std::string();
    std::string_view classifier_key;
    if (natives[kKey].get(classifier_key)) return std::string();

    std::string key(classifier_key);
    const std::size_t pos = key.find("${arch}");
    if (pos != std::string::npos) key.replace(pos, 7, "64");
    return key;
}

} // namespace

ManifestSnapshot ParseManifest(std::string_view json) {
    ManifestSnapshot snapshot;
    simdjson::dom::parser parser;
    simdjson::padded_string padded(json);
    simdjson::dom::element doc;
    if (parser.parse(padded).get(doc)) {
        snapshot.status = ManifestSnapshot::Status::Error;
        snapshot.error = "malformed manifest JSON";
        return snapshot;
    }

    simdjson::dom::array versions;
    if (doc["versions"].get(versions)) {
        snapshot.status = ManifestSnapshot::Status::Error;
        snapshot.error = "manifest missing versions[]";
        return snapshot;
    }

    for (simdjson::dom::element entry : versions) {
        VersionEntry v;
        std::string_view sv;
        if (!entry["id"].get(sv)) v.id.assign(sv);
        if (!entry["type"].get(sv)) v.type.assign(sv);
        if (!entry["url"].get(sv)) v.url.assign(sv);
        if (!entry["sha1"].get(sv)) v.sha1.assign(sv);
        if (!entry["releaseTime"].get(sv)) v.release_time.assign(sv);
        if (!v.id.empty()) snapshot.versions.push_back(std::move(v));
    }

    snapshot.status = ManifestSnapshot::Status::Ready;
    return snapshot;
}

VersionDetail ParseVersionJson(std::string_view json) {
    VersionDetail detail;
    simdjson::dom::parser parser;
    simdjson::padded_string padded(json);
    simdjson::dom::element doc;
    if (parser.parse(padded).get(doc)) return detail;

    std::string_view sv;

    simdjson::dom::element client;
    if (!doc["downloads"]["client"].get(client)) {
        if (!client["url"].get(sv)) detail.client.url.assign(sv);
        if (!client["sha1"].get(sv)) detail.client.sha1.assign(sv);
        int64_t size = 0;
        if (!client["size"].get(size)) detail.client.size = static_cast<std::uint64_t>(size);
    }

    simdjson::dom::element asset_index;
    if (!doc["assetIndex"].get(asset_index)) {
        if (!asset_index["id"].get(sv)) detail.asset_index.id.assign(sv);
        if (!asset_index["url"].get(sv)) detail.asset_index.url.assign(sv);
        if (!asset_index["sha1"].get(sv)) detail.asset_index.sha1.assign(sv);
        int64_t size = 0;
        if (!asset_index["size"].get(size)) detail.asset_index.size = static_cast<std::uint64_t>(size);
    }

    simdjson::dom::array libraries;
    if (!doc["libraries"].get(libraries)) {
        for (simdjson::dom::element library : libraries) {
            if (!LibraryAllowedOnThisOS(library)) continue;

            // Only the modern (1.13+, and Mojang's re-served old-version
            // JSONs) downloads{artifact,classifiers} schema is supported --
            // pre-1.13 libraries that only carry a Maven "url"+"name" pair
            // are skipped (old_beta/old_alpha coverage is best-effort).
            simdjson::dom::element downloads;
            if (library["downloads"].get(downloads)) continue;

            simdjson::dom::element artifact;
            if (!downloads["artifact"].get(artifact)) {
                LibraryArtifact entry;
                if (!artifact["path"].get(sv)) entry.path.assign(sv);
                if (!artifact["url"].get(sv)) entry.url.assign(sv);
                if (!artifact["sha1"].get(sv)) entry.sha1.assign(sv);
                int64_t size = 0;
                if (!artifact["size"].get(size)) entry.size = static_cast<std::uint64_t>(size);
                if (!entry.path.empty()) detail.libraries.push_back(std::move(entry));
            }

            const std::string natives_key = NativesKeyFor(library);
            if (!natives_key.empty()) {
                simdjson::dom::element classifiers;
                simdjson::dom::element native_artifact;
                if (!downloads["classifiers"].get(classifiers) &&
                    !classifiers[natives_key].get(native_artifact)) {
                    LibraryArtifact entry;
                    if (!native_artifact["path"].get(sv)) entry.path.assign(sv);
                    if (!native_artifact["url"].get(sv)) entry.url.assign(sv);
                    if (!native_artifact["sha1"].get(sv)) entry.sha1.assign(sv);
                    int64_t size = 0;
                    if (!native_artifact["size"].get(size)) entry.size = static_cast<std::uint64_t>(size);
                    if (!entry.path.empty()) detail.libraries.push_back(std::move(entry));
                }
            }
        }
    }

    return detail;
}

std::vector<AssetObject> ParseAssetIndex(std::string_view json) {
    std::vector<AssetObject> objects;
    simdjson::dom::parser parser;
    simdjson::padded_string padded(json);
    simdjson::dom::element doc;
    if (parser.parse(padded).get(doc)) return objects;

    simdjson::dom::object object_map;
    if (doc["objects"].get(object_map)) return objects;

    for (auto [key, value] : object_map) {
        (void)key;
        AssetObject obj;
        std::string_view sv;
        if (!value["hash"].get(sv)) obj.hash.assign(sv);
        int64_t size = 0;
        if (!value["size"].get(size)) obj.size = static_cast<std::uint64_t>(size);
        if (!obj.hash.empty()) objects.push_back(std::move(obj));
    }
    return objects;
}

namespace {

// Reads a whole (small) JSON file into a string; "" on failure. Kept local to
// this file -- ScanInstances/the launcher only need it for the version JSONs,
// which are a few dozen KB at most.
std::string ReadWholeFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::string();
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

} // namespace

std::vector<InstalledInstance> ScanInstances(const std::string& game_dir) {
    namespace fs = std::filesystem;
    std::vector<InstalledInstance> out;
    if (game_dir.empty()) return out;

    std::error_code ec;
    const fs::path versions = fs::path(game_dir) / "versions";
    if (!fs::is_directory(versions, ec)) return out;

    for (const fs::directory_entry& entry : fs::directory_iterator(versions, ec)) {
        if (ec) break;
        if (!entry.is_directory(ec)) continue;
        const std::string name = entry.path().filename().string();
        const fs::path json_path = entry.path() / (name + ".json");
        if (!fs::exists(json_path, ec)) continue;

        InstalledInstance inst;
        inst.name = name;

        const std::string json = ReadWholeFile(json_path);
        if (!json.empty()) {
            simdjson::dom::parser parser;
            simdjson::dom::element doc;
            if (!parser.parse(simdjson::padded_string(json)).get(doc)) {
                std::string_view sv;
                if (!doc["id"].get(sv)) inst.version_id.assign(sv);
                if (!doc["type"].get(sv)) inst.type.assign(sv);
            }
        }
        if (inst.version_id.empty()) inst.version_id = name;
        out.push_back(std::move(inst));
    }
    return out;
}

LaunchProfile ParseLaunchProfile(std::string_view json) {
    LaunchProfile profile;
    simdjson::dom::parser parser;
    simdjson::padded_string padded(json);
    simdjson::dom::element doc;
    if (parser.parse(padded).get(doc)) return profile;

    std::string_view sv;
    if (!doc["mainClass"].get(sv)) profile.main_class.assign(sv);
    if (!doc["type"].get(sv)) profile.type.assign(sv);
    if (!doc["assetIndex"]["id"].get(sv)) profile.asset_index_id.assign(sv);

    // Libraries: the same OS-rule / natives-classifier logic ParseVersionJson
    // uses (reusing LibraryAllowedOnThisOS / NativesKeyFor above), but here the
    // main artifact goes on the classpath and the natives artifact into the
    // separate native list (to be extracted, not classpathed).
    simdjson::dom::array libraries;
    if (!doc["libraries"].get(libraries)) {
        for (simdjson::dom::element library : libraries) {
            if (!LibraryAllowedOnThisOS(library)) continue;

            simdjson::dom::element downloads;
            if (library["downloads"].get(downloads)) continue;

            simdjson::dom::element artifact;
            if (!downloads["artifact"].get(artifact)) {
                std::string_view path;
                if (!artifact["path"].get(path)) profile.classpath_libs.emplace_back(path);
            }

            const std::string natives_key = NativesKeyFor(library);
            if (!natives_key.empty()) {
                simdjson::dom::element classifiers;
                simdjson::dom::element native_artifact;
                if (!downloads["classifiers"].get(classifiers) &&
                    !classifiers[natives_key].get(native_artifact)) {
                    std::string_view path;
                    if (!native_artifact["path"].get(path)) profile.native_libs.emplace_back(path);
                }
            }
        }
    }

    // Game arguments. Modern format: arguments.game[] -- an array mixing plain
    // strings and rule-gated objects (feature flags: demo mode, custom
    // resolution, quickPlay). Keep only the plain strings; the gated ones are
    // opt-in features an offline launch never enables. Legacy format:
    // minecraftArguments, a single space-joined string.
    simdjson::dom::array game_args;
    if (!doc["arguments"]["game"].get(game_args)) {
        for (simdjson::dom::element arg : game_args) {
            std::string_view s;
            if (!arg.get(s)) profile.game_args.emplace_back(s); // non-strings error out -> skipped
        }
    } else {
        std::string_view legacy;
        if (!doc["minecraftArguments"].get(legacy)) {
            std::istringstream iss{std::string(legacy)};
            std::string token;
            while (iss >> token) profile.game_args.push_back(token);
        }
    }

    return profile;
}

} // namespace net
