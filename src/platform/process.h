#pragma once

// Cross-platform detached process spawn, used to launch the game's Java
// process from the render thread without blocking on it. Follows the platform
// layer's convention (src/platform): one file, both #ifdef branches fully
// implemented, plain std::string/std::vector arguments, a primitive return
// (bool = the child was successfully started; the launcher does not wait for
// or manage the child's lifetime afterwards).
#include <string>
#include <vector>

namespace platform {

// Spawns `exe` with `args` (argv excluding the program name) as a detached
// child, with its working directory set to `cwd`. Returns true if the child
// process was successfully created. Does not wait for it -- the game keeps
// running independently of the launcher.
bool LaunchProcess(const std::string& exe, const std::vector<std::string>& args,
                   const std::string& cwd);

} // namespace platform
