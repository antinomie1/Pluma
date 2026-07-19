/*
 * launcher - desktop launcher application
 * Copyright (C) 2026 antinomie1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License only.
 */

#include "java_finder.h"
#include "util.h"

#include <windows.h>

#include <vector>

namespace {

bool is_java_exe(const util::fs::path& p)
{
    return util::file_exists(p) && (p.filename() == "java.exe" || p.filename() == "javaw.exe");
}

std::string which_on_path(const char* exe)
{
    wchar_t wexe[64];
    MultiByteToWideChar(CP_UTF8, 0, exe, -1, wexe, 64);
    wchar_t buf[MAX_PATH];
    if (SearchPathW(nullptr, wexe, nullptr, MAX_PATH, buf, nullptr))
        return util::narrow(buf);
    return {};
}

void try_add(std::vector<std::string>& candidates, const util::fs::path& p)
{
    if (is_java_exe(p))
        candidates.push_back(p.string());
    else if (util::dir_exists(p)) {
        try_add(candidates, p / "bin" / "javaw.exe");
        try_add(candidates, p / "bin" / "java.exe");
    }
}

} // namespace

std::string find_java(const std::string& preferred)
{
    std::vector<std::string> candidates;

    if (!preferred.empty()) {
        util::fs::path p(preferred);
        if (util::dir_exists(p))
            try_add(candidates, p);
        else
            try_add(candidates, p);
    }

    std::string java_home = util::get_env("JAVA_HOME");
    if (!java_home.empty())
        try_add(candidates, java_home);

    // Prefer javaw for GUI (no console flash)
    if (auto p = which_on_path("javaw.exe"); !p.empty())
        candidates.push_back(p);
    if (auto p = which_on_path("java.exe"); !p.empty())
        candidates.push_back(p);

    const char* prog_files[] = {
        "C:\\Program Files\\Java",
        "C:\\Program Files\\Eclipse Adoptium",
        "C:\\Program Files\\Microsoft",
        "C:\\Program Files\\Zulu",
        "C:\\Program Files\\Amazon Corretto",
        "C:\\Program Files\\BellSoft",
        "C:\\Program Files (x86)\\Java",
    };

    for (const char* root : prog_files) {
        util::fs::path base(root);
        if (!util::dir_exists(base))
            continue;
        std::error_code ec;
        for (auto& ent : util::fs::directory_iterator(base, ec)) {
            if (!ent.is_directory())
                continue;
            try_add(candidates, ent.path());
        }
    }

    // Minecraft bundled runtime (common on Windows)
    std::string local = util::get_env("LOCALAPPDATA");
    if (!local.empty()) {
        util::fs::path rt = util::fs::path(local) / "Packages";
        // Also check official launcher java runtimes under Program Files
        util::fs::path mc_java = util::fs::path(local) / "Programs" / "mcpelauncher"; // unlikely
        (void)mc_java;
    }

    // Official Minecraft Launcher runtimes
    std::string pf = util::get_env("ProgramFiles(x86)");
    if (pf.empty())
        pf = "C:\\Program Files (x86)";
    util::fs::path official = util::fs::path(pf) / "Minecraft Launcher" / "runtime";
    if (util::dir_exists(official)) {
        std::error_code ec;
        for (auto& ent : util::fs::recursive_directory_iterator(official, ec)) {
            if (ent.is_regular_file() && ent.path().filename() == "javaw.exe")
                candidates.push_back(ent.path().string());
        }
    }

    util::fs::path pf64 = "C:\\Program Files\\Minecraft Launcher\\runtime";
    if (util::dir_exists(pf64)) {
        std::error_code ec;
        for (auto& ent : util::fs::recursive_directory_iterator(pf64, ec)) {
            if (ent.is_regular_file() && ent.path().filename() == "javaw.exe")
                candidates.push_back(ent.path().string());
        }
    }

    // New Microsoft Store / Xbox launcher style: %LOCALAPPDATA%\Packages\Microsoft.4297127D64EC6_...
    // Also: %LOCALAPPDATA%\Packages\Microsoft.MinecraftUWP - skip
    // Game core runtimes often under:
    // %LOCALAPPDATA%\Packages\Microsoft.4297127D64EC6_*\LocalCache\Local\runtime
    if (!local.empty()) {
        util::fs::path packages = util::fs::path(local) / "Packages";
        if (util::dir_exists(packages)) {
            std::error_code ec;
            for (auto& ent : util::fs::directory_iterator(packages, ec)) {
                auto name = ent.path().filename().string();
                if (name.find("Microsoft.4297127D64EC6") == std::string::npos)
                    continue;
                util::fs::path rt = ent.path() / "LocalCache" / "Local" / "runtime";
                if (!util::dir_exists(rt))
                    continue;
                std::error_code ec2;
                for (auto& j : util::fs::recursive_directory_iterator(rt, ec2)) {
                    if (j.is_regular_file() && j.path().filename() == "javaw.exe")
                        candidates.push_back(j.path().string());
                }
            }
        }
    }

    for (const auto& c : candidates) {
        if (util::file_exists(c))
            return c;
    }
    return {};
}
