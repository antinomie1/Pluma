#include "platform/file_dialog.h"

#ifdef _WIN32

// Require Windows 10 APIs, matching java_locator.cpp / win32_chrome.cpp /
// system_fonts.cpp's floor for this codebase.
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <shlobj.h>

#else // POSIX

#include <array>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#endif

namespace platform {

#ifdef _WIN32

namespace {

// Duplicated from java_locator.cpp rather than shared -- that file's copy is
// anonymous-namespace-scoped to its own translation unit (see this project's
// "small per-file platform helpers" convention, also followed by
// system_fonts.cpp).
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

} // namespace

std::string PickDirectory() {
    const HRESULT init_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    // SUCCEEDED(S_OK) means we own this thread's COM apartment and must
    // uninitialize it below; RPC_E_CHANGED_MODE means something already
    // initialized COM here (with a possibly different concurrency model) --
    // usable either way, but not ours to tear down.
    if (FAILED(init_hr) && init_hr != RPC_E_CHANGED_MODE) {
        return std::string();
    }
    const bool owns_com = SUCCEEDED(init_hr);

    std::string result;
    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&dialog)))) {
        DWORD options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options))) {
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        }
        dialog->SetTitle(L"Select Game Directory");

        if (SUCCEEDED(dialog->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = WideToUtf8(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }

    if (owns_com) {
        CoUninitialize();
    }
    return result;
}

#else // POSIX

namespace {

// Duplicated from java_locator.cpp -- see that file's CommandExists/
// RunPickerCommand for the original; not shared across translation units,
// matching this project's per-file platform-helper convention.
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

std::string PickDirectory() {
    if (CommandExists("zenity")) {
        return RunPickerCommand(
            "zenity --file-selection --directory --title=\"Select Game Directory\" 2>/dev/null");
    }
    if (CommandExists("kdialog")) {
        return RunPickerCommand("kdialog --getexistingdirectory . 2>/dev/null");
    }
    return std::string(); // no picker available -- acts like "cancelled"
}

#endif

} // namespace platform
