/*
 * launcher - desktop launcher application
 * Copyright (C) 2026 antinomie1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License only.
 */

#pragma once

#include "config.h"

#include <string>

struct LaunchRequest {
    Config config;
    std::string version_id;
    std::string java_path; // resolved
};

struct LaunchResult {
    bool ok = false;
    std::string message;
    std::string command_preview; // for log
};

/** Build classpath / args and start Minecraft (offline). */
LaunchResult launch_minecraft(const LaunchRequest& req);
