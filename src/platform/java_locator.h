#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Runtime discovery/selection of Java executables and system RAM, backing
// Settings > Java (src/ui/java_settings.cpp). Follows system_fonts.h's
// convention: plain std::string/std::vector<std::string>/std::uint64_t
// returns, empty/0 meaning "not found" -- no RAII wrapper needed since
// nothing here owns a live OS handle.
namespace platform {

// Scans PATH entries and JAVA_HOME/bin for a java executable (javaw.exe
// preferred over java.exe on Windows so launching doesn't flash a console;
// "java" on POSIX). Deduplicated, existing files only. Fast/synchronous --
// safe to call from the render thread.
std::vector<std::string> DiscoverJavaOnPath();

// Opens a native "pick an executable" dialog filtered to javaw.exe/java.exe
// (Windows) or java (POSIX). Returns the chosen path, or "" if cancelled.
// Blocking/modal by nature -- same threading expectation as any other
// render-thread-initiated OS dialog (e.g. the window min/close FABs).
std::string PickJavaExecutable();

// Total physical RAM in bytes, or 0 if it couldn't be determined.
std::uint64_t SystemMemoryBytes();

// Normalizes a path for equality comparison only (not for display/storage):
// collapses redundant separators/"." segments, and on Windows also folds
// case (Windows paths are case-insensitive; POSIX paths are not). Used to
// dedupe Java paths that look different as strings but refer to the same
// file -- e.g. a JAVA_HOME with vs. without a trailing separator used to
// produce "...\JRE-25\bin\javaw.exe" from one source and
// "...\JRE-25\\bin\javaw.exe" (doubled separator) from another.
std::string NormalizeJavaPath(const std::string& path);

} // namespace platform
