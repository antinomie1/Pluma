#pragma once

// Assembles the Java command line to launch an already-installed instance:
// reads <game_dir>/versions/<name>/<name>.json (net::ParseLaunchProfile),
// extracts that version's native libraries into a natives/ folder (miniz),
// builds the classpath and substitutes the ${...} placeholders in the game
// arguments. Header stays third-party-free (plain net::LaunchParams/
// LaunchCommand in net/types.h); miniz/simdjson are only touched in
// launcher.cpp. Does not spawn the process itself -- that's
// platform::LaunchProcess, kept in the platform layer so net has no OS
// process-API dependency (see CLAUDE.md's module graph).
#include "net/types.h"

namespace net {

// Resolves `params` into a runnable LaunchCommand. On any failure (missing
// json/jar, unparseable version, no mainClass) the returned command's `error`
// is non-empty and it must not be launched. Extracting natives writes into
// <game_dir>/versions/<instance>/natives/ as a side effect.
LaunchCommand PrepareLaunch(const LaunchParams& params);

} // namespace net
