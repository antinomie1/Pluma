#pragma once

// simdjson-backed parsing of the three Mojang JSON documents the install
// pipeline touches: the version manifest, a single version's JSON, and an
// asset index. Header stays simdjson-free (plain std::string/std::vector/POD
// only) -- simdjson::dom types are only ever touched inside manifest.cpp.
#include "net/types.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace net {

// Parses version_manifest_v2.json's `versions[]` array (id/type/url/sha1/
// releaseTime) into a Ready snapshot, or an Error snapshot (with `error` set)
// if `json` doesn't parse as the expected shape.
ManifestSnapshot ParseManifest(std::string_view json);

// One resolved library artifact to download to
// `<game_dir>/libraries/<path>` -- already filtered against the current OS's
// `rules[]` (see manifest.cpp's internal LibraryAllowedOnThisOS), so every
// entry ParseVersionJson() returns is meant to be downloaded on this
// platform. Covers both a library's main artifact and (when present) its
// natives/classifiers artifact for the current OS -- each becomes its own
// entry here.
struct LibraryArtifact {
    std::string path; // relative to <game_dir>/libraries/
    std::string url;
    std::string sha1;
    std::uint64_t size = 0;
};

struct VersionDetail {
    struct ClientJar {
        std::string url;
        std::string sha1;
        std::uint64_t size = 0;
    } client;

    std::vector<LibraryArtifact> libraries;

    struct AssetIndexRef {
        std::string id;
        std::string url;
        std::string sha1;
        std::uint64_t size = 0;
    } asset_index;
};

// Parses one version's JSON (downloads.client, libraries[], assetIndex).
// Returns a default-constructed (empty) VersionDetail if `json` doesn't parse
// as the expected shape -- callers distinguish that from "really has no
// libraries" by checking client.url.empty().
VersionDetail ParseVersionJson(std::string_view json);

// One object entry from an asset index's `objects` map. Its on-disk path is
// `<game_dir>/assets/objects/<hash[0:2]>/<hash>` and its official URL is
// `https://resources.download.minecraft.net/<hash[0:2]>/<hash>` -- both
// cheap to compute from `hash` alone, so neither is stored here.
struct AssetObject {
    std::string hash; // also the object's SHA1
    std::uint64_t size = 0;
};

// Parses an asset index JSON's `objects` map. Returns an empty vector if
// `json` doesn't parse as the expected shape.
std::vector<AssetObject> ParseAssetIndex(std::string_view json);

// Scans <game_dir>/versions/*/ for installed instances, reading each
// <name>/<name>.json's top-level id/type back. Read-only; returns an empty
// vector if the versions/ directory doesn't exist. Used by the Home page's
// instance list.
std::vector<InstalledInstance> ScanInstances(const std::string& game_dir);

// The subset of a version JSON needed to *launch* (as opposed to download) an
// instance: the main class, asset-index id, version type, the game-argument
// template, and the library paths split into classpath jars vs. native jars
// to extract. Paths are relative to <game_dir>/libraries/ (same as
// LibraryArtifact::path). game_args is the raw, unsubstituted template (the
// launcher fills ${...} placeholders); rule-gated argument objects in the
// modern format are dropped, only plain string args are kept.
struct LaunchProfile {
    std::string main_class;
    std::string asset_index_id;
    std::string type;
    std::vector<std::string> game_args;
    std::vector<std::string> classpath_libs;
    std::vector<std::string> native_libs;
};

// Parses one version's JSON for the launch-relevant fields above. Returns a
// default-constructed (empty main_class) profile if `json` doesn't parse --
// callers check main_class.empty().
LaunchProfile ParseLaunchProfile(std::string_view json);

} // namespace net
