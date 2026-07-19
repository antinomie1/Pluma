/*
 * launcher - desktop launcher application
 * Copyright (C) 2026 antinomie1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License only.
 */

#include "config.h"
#include "util.h"

#include <nlohmann/json.hpp>

#include <windows.h>

using json = nlohmann::json;

std::string Config::default_game_dir()
{
    std::string appdata = util::get_env("APPDATA");
    if (appdata.empty())
        return ".minecraft";
    return (util::fs::path(appdata) / ".minecraft").string();
}

std::string Config::config_path()
{
    std::string appdata = util::get_env("APPDATA");
    util::fs::path base = appdata.empty() ? util::fs::path(".") : util::fs::path(appdata);
    return (base / "mc-minimal-launcher" / "config.json").string();
}

void Config::ensure_defaults()
{
    if (game_dir.empty())
        game_dir = default_game_dir();
    if (username.empty())
        username = "Player";
    if (max_memory_mb < 512)
        max_memory_mb = 512;
    if (max_memory_mb > 32768)
        max_memory_mb = 32768;
}

bool Config::load()
{
    ensure_defaults();
    const auto path = config_path();
    if (!util::file_exists(path))
        return false;
    try {
        auto j = json::parse(util::read_text_file(path));
        if (j.contains("game_dir"))
            game_dir = j["game_dir"].get<std::string>();
        if (j.contains("java_path"))
            java_path = j["java_path"].get<std::string>();
        if (j.contains("username"))
            username = j["username"].get<std::string>();
        if (j.contains("last_version"))
            last_version = j["last_version"].get<std::string>();
        if (j.contains("max_memory_mb"))
            max_memory_mb = j["max_memory_mb"].get<int>();
        if (j.contains("extra_jvm_args"))
            extra_jvm_args = j["extra_jvm_args"].get<std::string>();
        if (j.contains("download_mirror"))
            download_mirror = j["download_mirror"].get<int>();
        ensure_defaults();
        return true;
    } catch (...) {
        ensure_defaults();
        return false;
    }
}

bool Config::save() const
{
    json j;
    j["game_dir"] = game_dir;
    j["java_path"] = java_path;
    j["username"] = username;
    j["last_version"] = last_version;
    j["max_memory_mb"] = max_memory_mb;
    j["extra_jvm_args"] = extra_jvm_args;
    j["download_mirror"] = download_mirror;
    return util::write_text_file(config_path(), j.dump(2));
}
