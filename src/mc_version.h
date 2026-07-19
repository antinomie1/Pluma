/*
 * launcher - desktop launcher application
 * Copyright (C) 2026 antinomie1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License only.
 */

#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct VersionInfo {
    std::string id;
    std::string path; // versions/<id>/<id>.json
};

std::vector<VersionInfo> list_installed_versions(const std::string& game_dir);

/** Load version JSON with inheritsFrom merge. */
nlohmann::json load_version_json(const std::string& game_dir, const std::string& version_id, std::string* err);

/** Evaluate library/argument OS rules (Windows host). */
bool mc_rules_allow(const nlohmann::json& rules);
