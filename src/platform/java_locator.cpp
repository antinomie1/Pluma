#include "platform/java_locator.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>

#ifdef _WIN32

// Require Windows 10 APIs, matching win32_chrome.cpp / system_fonts.cpp's
// floor for this codebase.
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <commdlg.h>

#else // POSIX

#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstdlib>

#endif

namespace platform {

#ifdef _WIN32

namespace {

// Converts a UTF-16 Windows string to UTF-8, matching the std::string/
// std::vector<std::string> return convention this file exposes -- callers
// never see wide strings.
std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return std::string();
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (size <= 0) return std::string();
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), size,
                        nullptr, nullptr);
    return out;
}

// The inverse of WideToUtf8 -- needed only for NormalizeJavaPath's
// case-folding (Windows paths are case-insensitive).
std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int size =
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (size <= 0) return std::wstring();
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), size);
    return out;
}

// Reads an environment variable's full value, or an empty wstring if unset.
std::wstring ReadEnvVar(const wchar_t* name) {
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) return std::wstring(); // not set (or genuinely empty)
    std::wstring value(needed, L'\0');
    const DWORD actual = GetEnvironmentVariableW(name, value.data(), needed);
    if (actual == 0 || actual >= needed) return std::wstring();
    value.resize(actual);
    return value;
}

} // namespace

std::vector<std::string> DiscoverJavaOnPath() {
    std::vector<std::string> found;
    namespace fs = std::filesystem;

    // javaw.exe is preferred over java.exe in a given directory so launching
    // doesn't flash a console window; only the first match per directory is
    // kept. Dedup compares NormalizeJavaPath()'d forms, not raw strings --
    // PATH and JAVA_HOME\bin can resolve to the same real file while looking
    // like different strings (case, or JAVA_HOME's trailing separator, see
    // the fs::path join below).
    auto try_dir = [&](const std::wstring& dir) {
        if (dir.empty()) return;
        for (const wchar_t* exe : {L"javaw.exe", L"java.exe"}) {
            std::error_code ec;
            const fs::path candidate = fs::path(dir) / exe;
            if (fs::exists(candidate, ec) && !ec) {
                const std::string utf8 = WideToUtf8(candidate.wstring());
                const std::string normalized = NormalizeJavaPath(utf8);
                const bool duplicate = std::any_of(
                    found.begin(), found.end(), [&](const std::string& existing) {
                        return NormalizeJavaPath(existing) == normalized;
                    });
                if (!duplicate) {
                    found.push_back(utf8);
                }
                return;
            }
        }
    };

    const std::wstring path_var = ReadEnvVar(L"PATH");
    std::size_t start = 0;
    while (start <= path_var.size()) {
        const std::size_t sep = path_var.find(L';', start);
        const std::wstring dir =
            path_var.substr(start, sep == std::wstring::npos ? std::wstring::npos : sep - start);
        try_dir(dir);
        if (sep == std::wstring::npos) break;
        start = sep + 1;
    }

    const std::wstring java_home = ReadEnvVar(L"JAVA_HOME");
    if (!java_home.empty()) {
        // fs::path's / operator correctly handles a trailing separator on
        // java_home (won't double it up) -- unlike the raw wstring
        // concatenation this used to do, which is exactly how
        // "...\JRE-25\\bin\javaw.exe" (doubled separator) got discovered as
        // a seemingly-different path from the same directory's PATH entry.
        try_dir((fs::path(java_home) / L"bin").wstring());
    }

    return found;
}

std::string PickJavaExecutable() {
    wchar_t file[MAX_PATH] = L"";

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = L"Java Executable\0javaw.exe;java.exe\0All Files\0*.*\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = L"Select Java Executable";

    if (!GetOpenFileNameW(&ofn)) {
        return std::string(); // cancelled or failed
    }
    return WideToUtf8(std::wstring(file));
}

std::uint64_t SystemMemoryBytes() {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) {
        return 0;
    }
    return static_cast<std::uint64_t>(status.ullTotalPhys);
}

std::string NormalizeJavaPath(const std::string& path) {
    if (path.empty()) return path;
    std::wstring normalized = std::filesystem::path(Utf8ToWide(path)).lexically_normal().wstring();
    for (wchar_t& c : normalized) {
        c = static_cast<wchar_t>(::towlower(c));
    }
    return WideToUtf8(normalized);
}

#else // POSIX

namespace {

// Scans PATH for an executable named `name` (access(..., X_OK)), without
// actually running it -- used to pick zenity vs kdialog vs "neither".
bool CommandExists(const char* name) {
    const char* path_env = std::getenv("PATH");
    if (path_env == nullptr) return false;
    const std::string path(path_env);

    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t sep = path.find(':', start);
        const std::string dir =
            path.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
        if (!dir.empty()) {
            const std::string candidate = dir + "/" + name;
            if (access(candidate.c_str(), X_OK) == 0) return true;
        }
        if (sep == std::string::npos) break;
        start = sep + 1;
    }
    return false;
}

// Runs `command` via the shell and returns its trimmed stdout. Used for the
// zenity/kdialog file-picker invocations, which print the chosen path (or
// nothing, on Cancel) to stdout.
std::string RunPickerCommand(const std::string& command) {
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) return std::string();

    std::string result;
    std::array<char, 256> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result += buffer.data();
    }
    pclose(pipe);

    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    return result;
}

} // namespace

std::vector<std::string> DiscoverJavaOnPath() {
    std::vector<std::string> found;
    namespace fs = std::filesystem;

    // Dedup compares NormalizeJavaPath()'d forms, not raw strings -- PATH and
    // JAVA_HOME/bin can resolve to the same real file while looking like
    // different strings (e.g. a redundant separator).
    auto try_dir = [&](const std::string& dir) {
        if (dir.empty()) return;
        std::error_code ec;
        const fs::path candidate = fs::path(dir) / "java";
        if (fs::exists(candidate, ec) && !ec) {
            const std::string s = candidate.string();
            const std::string normalized = NormalizeJavaPath(s);
            const bool duplicate = std::any_of(
                found.begin(), found.end(), [&](const std::string& existing) {
                    return NormalizeJavaPath(existing) == normalized;
                });
            if (!duplicate) {
                found.push_back(s);
            }
        }
    };

    const char* path_env = std::getenv("PATH");
    if (path_env != nullptr) {
        const std::string path(path_env);
        std::size_t start = 0;
        while (start <= path.size()) {
            const std::size_t sep = path.find(':', start);
            const std::string dir =
                path.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
            try_dir(dir);
            if (sep == std::string::npos) break;
            start = sep + 1;
        }
    }

    const char* java_home = std::getenv("JAVA_HOME");
    if (java_home != nullptr && java_home[0] != '\0') {
        // fs::path's / operator correctly handles a trailing separator on
        // java_home (won't double it up) -- unlike the raw string
        // concatenation this used to do.
        try_dir((fs::path(java_home) / "bin").string());
    }

    return found;
}

std::string PickJavaExecutable() {
    if (CommandExists("zenity")) {
        return RunPickerCommand(
            "zenity --file-selection --title=\"Select Java Executable\" 2>/dev/null");
    }
    if (CommandExists("kdialog")) {
        return RunPickerCommand("kdialog --getopenfilename . 2>/dev/null");
    }
    return std::string(); // no picker available -- acts like "cancelled"
}

std::uint64_t SystemMemoryBytes() {
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages <= 0 || page_size <= 0) return 0;
    return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(page_size);
}

std::string NormalizeJavaPath(const std::string& path) {
    if (path.empty()) return path;
    // POSIX paths are case-sensitive, unlike Windows -- collapse redundant
    // separators/"." segments only, no case-folding.
    return std::filesystem::path(path).lexically_normal().string();
}

#endif

} // namespace platform
