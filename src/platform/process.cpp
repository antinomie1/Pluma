#include "platform/process.h"

#ifdef _WIN32

#include <windows.h>

#include <string>

namespace platform {
namespace {

// Quotes one argv element per the Windows CommandLineToArgvW rules (the same
// algorithm MSVC's CRT uses), so paths/classpaths with spaces survive the flat
// command-line string CreateProcess takes.
std::string QuoteArg(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) {
        return arg;
    }
    std::string out = "\"";
    for (auto it = arg.begin();; ++it) {
        unsigned backslashes = 0;
        while (it != arg.end() && *it == '\\') {
            ++it;
            ++backslashes;
        }
        if (it == arg.end()) {
            out.append(backslashes * 2, '\\');
            break;
        } else if (*it == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out.push_back('"');
        } else {
            out.append(backslashes, '\\');
            out.push_back(*it);
        }
    }
    out.push_back('"');
    return out;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

} // namespace

ProcessHandle LaunchProcess(const std::string& exe, const std::vector<std::string>& args,
                            const std::string& cwd) {
    std::string command_line = QuoteArg(exe);
    for (const std::string& arg : args) {
        command_line.push_back(' ');
        command_line += QuoteArg(arg);
    }

    const std::wstring exe_w = Utf8ToWide(exe);
    std::wstring command_line_w = Utf8ToWide(command_line); // mutable: CreateProcessW may modify it
    const std::wstring cwd_w = Utf8ToWide(cwd);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    const BOOL ok = CreateProcessW(exe_w.c_str(), command_line_w.data(), nullptr, nullptr, FALSE,
                                   CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS, nullptr,
                                   cwd_w.empty() ? nullptr : cwd_w.c_str(), &si, &pi);
    if (!ok) return 0;

    // The thread handle is never needed; keep the process handle open so the
    // caller can poll the child's liveness, and hand it back as the token.
    CloseHandle(pi.hThread);
    return reinterpret_cast<ProcessHandle>(pi.hProcess);
}

bool IsProcessRunning(ProcessHandle handle) {
    if (handle == 0) return false;
    return WaitForSingleObject(reinterpret_cast<HANDLE>(handle), 0) == WAIT_TIMEOUT;
}

void CloseProcessHandle(ProcessHandle handle) {
    if (handle != 0) CloseHandle(reinterpret_cast<HANDLE>(handle));
}

} // namespace platform

#else // POSIX

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <vector>

namespace platform {

ProcessHandle LaunchProcess(const std::string& exe, const std::vector<std::string>& args,
                            const std::string& cwd) {
    const pid_t pid = fork();
    if (pid < 0) return 0;

    if (pid == 0) {
        // Child: move into the game directory, then exec. Not setsid() here --
        // the parent needs to stay the child's parent so waitpid() (in
        // IsProcessRunning) can observe its exit. Any failure _exit()s the
        // forked child without unwinding the parent's state.
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) _exit(127);

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(exe.c_str()));
        for (const std::string& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);

        execvp(exe.c_str(), argv.data());
        _exit(127); // exec failed
    }

    return static_cast<ProcessHandle>(pid); // parent: child launched, not waited on
}

bool IsProcessRunning(ProcessHandle handle) {
    if (handle == 0) return false;
    int status = 0;
    // WNOHANG: 0 => still running; ==pid => exited (and now reaped); <0 => no
    // such child (already reaped / never ours) -> treat as not running.
    const pid_t r = waitpid(static_cast<pid_t>(handle), &status, WNOHANG);
    return r == 0;
}

void CloseProcessHandle(ProcessHandle /*handle*/) {
    // Nothing to release on POSIX: IsProcessRunning already reaped the child
    // via waitpid the moment it reported the process gone.
}

} // namespace platform

#endif
