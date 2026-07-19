/*
 * launcher - desktop launcher application
 * Copyright (C) 2026 antinomie1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License only.
 */

#pragma once

#include <string>

struct Config {
    std::string game_dir;
    std::string java_path;   // empty = auto-detect
    std::string username = "Player";
    std::string last_version;
    int max_memory_mb = 2048;
    std::string extra_jvm_args;
    int download_mirror = 1; // 0 official, 1 BMCLAPI (default CN-friendly)

    static std::string default_game_dir();
    static std::string config_path();

    void ensure_defaults();
    bool load();
    bool save() const;
};
