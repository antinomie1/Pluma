#pragma once

// Plain data structures shared across the net module and consumed by ui/
// render. Deliberately curl/mbedtls/simdjson-free -- std::string/std::vector/
// POD only, so downstream modules (ui, render) can link net and see these
// types without pulling in any third-party network/crypto headers themselves
// (see CLAUDE.md's module dependency graph / src/net's own file-layout doc
// comment in the implementation plan).
#include <cstdint>
#include <string>
#include <vector>

namespace net {

// How DownloadManager resolves a download URL to a set of candidate URLs to
// try in order (net::CandidateUrls, src/net/mirror.h): Auto tries the BMCLAPI
// mirror first and falls back to the official Mojang/libraries/resources
// host; OfficialOnly/BmclapiOnly pin to a single source.
enum class MirrorMode { Auto, OfficialOnly, BmclapiOnly };

// One entry from Mojang's version_manifest_v2.json `versions[]` array.
struct VersionEntry {
    std::string id;
    std::string type; // "release" / "snapshot" / "old_beta" / "old_alpha"
    std::string url;   // official version JSON URL for this entry
    std::string sha1;
    std::string release_time;
};

// Published by DownloadManager (core::SharedValue<ManifestSnapshot>) so the
// render thread can read the version list without blocking on the network.
struct ManifestSnapshot {
    enum class Status { Idle, Loading, Ready, Error };
    Status status = Status::Idle;
    std::vector<VersionEntry> versions;
    std::string error;
};

// Everything the UI collects (from config + the new-instance dialog) to hand
// off a single EnqueueInstall() call. Assembled on the render thread from
// config::Config -- DownloadManager's worker threads never touch config
// themselves, keeping config's render-thread-exclusive/no-lock contract
// intact (see CLAUDE.md).
struct InstallParams {
    std::string game_dir;
    std::string instance_name;
    MirrorMode mirror = MirrorMode::Auto;
    int concurrency = 8;       // simultaneous files in flight
    int threads_per_file = 4;  // HTTP Range segments per large file
};

// One installed instance discovered by scanning <game_dir>/versions/*/ (see
// net::ScanInstances). `name` is the instance directory name (also the
// jar/json basename, i.e. what the UI chose in the new-instance dialog);
// `version_id`/`type` are the top-level "id"/"type" read back from the
// stored <name>.json (version_id falls back to name if the JSON has no id).
struct InstalledInstance {
    std::string name;
    std::string version_id;
    std::string type;
};

// Everything the UI collects (config + selected instance/account) to build a
// launch command line. Assembled on the render thread; the net launcher never
// touches config itself, same contract as InstallParams above.
struct LaunchParams {
    std::string game_dir;
    std::string instance_name;
    std::string java_exe;
    int memory_mb = 2048;
    std::string jvm_args;   // extra user JVM args, space-separated
    std::string player_name;
    std::string player_uuid;
    // Version isolation: when true (the default), the game directory is the
    // instance's own versions/<name>/ folder (mods/saves/resourcepacks per
    // instance); when false, the shared game_dir root is used and that user
    // data is shared across every instance.
    bool isolate_instance = true;
};

// The fully-resolved command net::PrepareLaunch produced: the Java executable,
// its argument vector (JVM opts + classpath + main class + substituted game
// args), and the working directory to spawn it in. `error` non-empty means
// preparation failed (missing jar/json/mainClass) and the command must not be
// launched.
struct LaunchCommand {
    std::string exe;
    std::vector<std::string> args;
    std::string cwd;
    std::string error;
};

// One row in the Tasks page, published via
// core::SharedValue<std::vector<TaskInfo>>.
struct TaskInfo {
    std::uint64_t id = 0;
    std::string name; // instance name

    enum class Phase { Json, Client, Libraries, Assets, Done };
    Phase phase = Phase::Json;

    enum class Status { Queued, Running, Paused, Done, Error };
    Status status = Status::Queued;

    std::uint64_t bytes_done = 0;
    std::uint64_t bytes_total = 0;
    int files_done = 0;
    int files_total = 0;
    double speed_bps = 0.0;
    std::string error;
};

} // namespace net
