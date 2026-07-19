/*
 * launcher - desktop launcher application
 * Copyright (C) 2026 antinomie1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License only.
 */

#include "mc_version.h"
#include "util.h"

#include <algorithm>
#include <set>

using json = nlohmann::json;
namespace fs = util::fs;

std::vector<VersionInfo> list_installed_versions(const std::string& game_dir)
{
    std::vector<VersionInfo> out;
    fs::path versions = fs::path(game_dir) / "versions";
    if (!util::dir_exists(versions))
        return out;

    std::error_code ec;
    for (auto& ent : fs::directory_iterator(versions, ec)) {
        if (!ent.is_directory())
            continue;
        std::string id = ent.path().filename().string();
        fs::path json_path = ent.path() / (id + ".json");
        fs::path jar_path = ent.path() / (id + ".jar");
        if (util::file_exists(json_path)) {
            VersionInfo v;
            v.id = id;
            v.path = json_path.string();
            // Prefer versions that have a client jar (or will inherit one)
            (void)jar_path;
            out.push_back(std::move(v));
        }
    }
    std::sort(out.begin(), out.end(), [](const VersionInfo& a, const VersionInfo& b) {
        return a.id > b.id;
    });
    return out;
}

namespace {

bool os_rule_matches(const json& os)
{
    if (!os.is_object())
        return true;
    if (os.contains("name")) {
        std::string name = os["name"].get<std::string>();
        if (name != "windows")
            return false;
    }
    if (os.contains("arch")) {
        std::string arch = os["arch"].get<std::string>();
#if defined(_WIN64) || defined(_M_X64) || defined(__x86_64__)
        const char* host = "x86_64";
        const char* host_alt = "amd64";
#else
        const char* host = "x86";
        const char* host_alt = "x86";
#endif
        if (arch != host && arch != host_alt && arch != "x86_64" && arch != "amd64") {
            // On 64-bit allow x86 natives sometimes needed; keep simple: match 64
            if (arch == "x86")
                return false;
        }
    }
    return true;
}

bool rules_allow(const json& rules)
{
    if (!rules.is_array() || rules.empty())
        return true;
    bool allowed = false;
    for (const auto& rule : rules) {
        if (!rule.is_object() || !rule.contains("action"))
            continue;
        bool applies = true;
        if (rule.contains("os"))
            applies = os_rule_matches(rule["os"]);
        // features: skip optional demo/custom resolution features -> treat as not present
        if (rule.contains("features"))
            applies = false;
        if (!applies)
            continue;
        allowed = rule["action"].get<std::string>() == "allow";
    }
    return allowed;
}

json merge_version(json parent, json child)
{
    // libraries: parent then child (child appended, may duplicate - OK for classpath uniqueness later)
    json libs = json::array();
    if (parent.contains("libraries") && parent["libraries"].is_array())
        for (auto& l : parent["libraries"])
            libs.push_back(l);
    if (child.contains("libraries") && child["libraries"].is_array())
        for (auto& l : child["libraries"])
            libs.push_back(l);
    child["libraries"] = libs;

    // arguments: concatenate
    if (parent.contains("arguments") || child.contains("arguments")) {
        json args;
        args["jvm"] = json::array();
        args["game"] = json::array();
        auto append_args = [&](const json& src, const char* key) {
            if (src.contains("arguments") && src["arguments"].contains(key) && src["arguments"][key].is_array())
                for (auto& a : src["arguments"][key])
                    args[key].push_back(a);
        };
        append_args(parent, "jvm");
        append_args(child, "jvm");
        append_args(parent, "game");
        append_args(child, "game");
        child["arguments"] = args;
    }

    // scalar overrides from child already present; fill missing from parent
    for (auto it = parent.begin(); it != parent.end(); ++it) {
        const std::string key = it.key();
        if (key == "libraries" || key == "arguments" || key == "inheritsFrom")
            continue;
        if (!child.contains(key))
            child[key] = it.value();
    }
    child.erase("inheritsFrom");
    return child;
}

json load_one(const std::string& game_dir, const std::string& version_id, std::set<std::string>& stack, std::string* err)
{
    if (stack.count(version_id)) {
        if (err)
            *err = "版本继承循环: " + version_id;
        return nullptr;
    }
    stack.insert(version_id);

    fs::path path = fs::path(game_dir) / "versions" / version_id / (version_id + ".json");
    if (!util::file_exists(path)) {
        if (err)
            *err = "找不到版本 JSON: " + path.string();
        return nullptr;
    }

    json j;
    try {
        j = json::parse(util::read_text_file(path));
    } catch (const std::exception& e) {
        if (err)
            *err = std::string("解析版本 JSON 失败: ") + e.what();
        return nullptr;
    }

    if (j.contains("inheritsFrom")) {
        std::string parent_id = j["inheritsFrom"].get<std::string>();
        json parent = load_one(game_dir, parent_id, stack, err);
        if (parent.is_null())
            return nullptr;
        j = merge_version(std::move(parent), std::move(j));
    }

    // Keep id
    if (!j.contains("id"))
        j["id"] = version_id;

    return j;
}

} // namespace

nlohmann::json load_version_json(const std::string& game_dir, const std::string& version_id, std::string* err)
{
    std::set<std::string> stack;
    return load_one(game_dir, version_id, stack, err);
}

bool mc_rules_allow(const nlohmann::json& rules)
{
    return rules_allow(rules);
}
