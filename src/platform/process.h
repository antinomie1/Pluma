#pragma once

// Cross-platform process spawn + liveness polling, used to launch the game's
// Java process from the render thread and then monitor it from the game-monitor
// thread. Follows the platform layer's convention (src/platform): one file,
// both #ifdef branches fully implemented, plain primitive types (an opaque
// handle rather than an RAII wrapper, since the caller -- platform::GameMonitor
// -- decides when to poll and release it).
#include <cstdint>
#include <string>
#include <vector>

namespace platform {

// Opaque handle to a spawned process: the Win32 process HANDLE on Windows, the
// child pid on POSIX, both stored as an integer. 0 means "no process / launch
// failed". Poll it with IsProcessRunning() and release it with
// CloseProcessHandle() once it has exited (or is no longer tracked).
using ProcessHandle = std::uintptr_t;

// Spawns `exe` with `args` (argv excluding the program name) as a detached
// child, with its working directory set to `cwd`. Returns a handle to the new
// process, or 0 if it could not be started. Does not wait for it -- the game
// keeps running independently of the launcher.
ProcessHandle LaunchProcess(const std::string& exe, const std::vector<std::string>& args,
                            const std::string& cwd);

// Whether the process behind `handle` is still alive. Returns false for a 0
// handle, or once the process has exited (on POSIX this also reaps the child,
// so it must not be called again for the same handle after it returns false).
bool IsProcessRunning(ProcessHandle handle);

// Releases the OS resources for `handle` (CloseHandle on Windows; a no-op on
// POSIX, where IsProcessRunning already reaped the child). Call once, after
// IsProcessRunning has reported the process gone.
void CloseProcessHandle(ProcessHandle handle);

} // namespace platform
