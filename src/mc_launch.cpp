/*
 * launcher - desktop launcher application
 * Copyright (C) 2026 antinomie1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License only.
 */

#include "mc_launch.h"
#include "mc_version.h"
#include "util.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <map>
#include <set>
#include <sstream>

using json = nlohmann::json;
namespace fs = util::fs;

namespace {

std::string maven_rel_path(const std::string& name, const std::string& classifier = {})
{
    auto parts = util::split(name, ':');
    if (parts.size() < 3)
        return {};
    std::string group = util::replace_all(parts[0], ".", "/");
    const std::string& artifact = parts[1];
    const std::string& version = parts[2];
    std::string file = artifact + "-" + version;
    if (parts.size() >= 4 && classifier.empty())
        file += "-" + parts[3];
    else if (!classifier.empty())
        file += "-" + classifier;
    file += ".jar";
    return group + "/" + artifact + "/" + version + "/" + file;
}

bool rule_list_allows(const json& lib)
{
    if (!lib.contains("rules"))
        return true;
    return mc_rules_allow(lib["rules"]);
}

std::string natives_classifier(const json& lib)
{
    if (!lib.contains("natives") || !lib["natives"].is_object())
        return {};
    const auto& n = lib["natives"];
    std::string key;
    if (n.contains("windows"))
        key = n["windows"].get<std::string>();
    else
        return {};
#if defined(_WIN64) || defined(_M_X64) || defined(__x86_64__)
    key = util::replace_all(key, "${arch}", "64");
#else
    key = util::replace_all(key, "${arch}", "32");
#endif
    return key;
}

void collect_libraries(const json& version, const fs::path& game_dir,
                       std::vector<fs::path>& classpath, std::vector<fs::path>& native_jars)
{
    std::set<std::string> seen;
    auto add_cp = [&](const fs::path& p) {
        std::string s = p.string();
        if (seen.insert(s).second)
            classpath.push_back(p);
    };

    if (!version.contains("libraries") || !version["libraries"].is_array())
        return;

    for (const auto& lib : version["libraries"]) {
        if (!rule_list_allows(lib))
            continue;

        // Artifact jar
        fs::path artifact_path;
        if (lib.contains("downloads") && lib["downloads"].contains("artifact") &&
            lib["downloads"]["artifact"].contains("path")) {
            artifact_path = game_dir / "libraries" / lib["downloads"]["artifact"]["path"].get<std::string>();
        } else if (lib.contains("name")) {
            std::string rel = maven_rel_path(lib["name"].get<std::string>());
            if (!rel.empty())
                artifact_path = game_dir / "libraries" / rel;
        }
        if (!artifact_path.empty() && util::file_exists(artifact_path))
            add_cp(artifact_path);

        // Natives
        std::string classifier = natives_classifier(lib);
        if (!classifier.empty()) {
            fs::path nat;
            if (lib.contains("downloads") && lib["downloads"].contains("classifiers") &&
                lib["downloads"]["classifiers"].contains(classifier) &&
                lib["downloads"]["classifiers"][classifier].contains("path")) {
                nat = game_dir / "libraries" / lib["downloads"]["classifiers"][classifier]["path"].get<std::string>();
            } else if (lib.contains("name")) {
                std::string rel = maven_rel_path(lib["name"].get<std::string>(), classifier);
                if (!rel.empty())
                    nat = game_dir / "libraries" / rel;
            }
            if (!nat.empty() && util::file_exists(nat))
                native_jars.push_back(nat);
        }
    }
}

bool arg_allowed(const json& entry)
{
    if (entry.is_string())
        return true;
    if (!entry.is_object())
        return false;
    if (entry.contains("rules") && !mc_rules_allow(entry["rules"]))
        return false;
    return true;
}

void append_arg_value(std::vector<std::string>& out, const json& value)
{
    if (value.is_string()) {
        out.push_back(value.get<std::string>());
    } else if (value.is_array()) {
        for (const auto& v : value)
            if (v.is_string())
                out.push_back(v.get<std::string>());
    }
}

std::vector<std::string> expand_argument_list(const json& list)
{
    std::vector<std::string> out;
    if (!list.is_array())
        return out;
    for (const auto& entry : list) {
        if (!arg_allowed(entry))
            continue;
        if (entry.is_string()) {
            out.push_back(entry.get<std::string>());
        } else if (entry.is_object() && entry.contains("value")) {
            append_arg_value(out, entry["value"]);
        }
    }
    return out;
}

std::string substitute_vars(std::string s, const std::map<std::string, std::string>& vars)
{
    for (const auto& [k, v] : vars)
        s = util::replace_all(s, "${" + k + "}", v);
    return s;
}

bool start_process(const std::string& exe, const std::vector<std::string>& args,
                   const std::string& work_dir, std::string* err)
{
    std::ostringstream oss;
    oss << util::quote_arg(exe);
    for (const auto& a : args)
        oss << ' ' << util::quote_arg(a);
    std::string cmdline = oss.str();
    std::wstring wcmd = util::widen(cmdline);
    std::wstring wdir = util::widen(work_dir);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(wcmd.begin(), wcmd.end());
    buf.push_back(0);

    BOOL ok = CreateProcessW(
        nullptr,
        buf.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS,
        nullptr,
        wdir.empty() ? nullptr : wdir.c_str(),
        &si,
        &pi);

    if (!ok) {
        if (err)
            *err = "CreateProcess 失败，错误码 " + std::to_string(GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

} // namespace

LaunchResult launch_minecraft(const LaunchRequest& req)
{
    LaunchResult result;

    if (req.version_id.empty()) {
        result.message = "请选择游戏版本";
        return result;
    }
    if (req.config.username.empty()) {
        result.message = "用户名不能为空";
        return result;
    }
    if (!util::dir_exists(req.config.game_dir)) {
        result.message = "游戏目录不存在: " + req.config.game_dir;
        return result;
    }
    if (req.java_path.empty() || !util::file_exists(req.java_path)) {
        result.message = "未找到 Java，请在「设置」中指定 java/javaw 路径";
        return result;
    }

    std::string err;
    json version = load_version_json(req.config.game_dir, req.version_id, &err);
    if (version.is_null()) {
        result.message = err.empty() ? "加载版本失败" : err;
        return result;
    }

    fs::path game_dir = req.config.game_dir;
    fs::path version_dir = game_dir / "versions" / req.version_id;
    fs::path client_jar = version_dir / (req.version_id + ".jar");

    // Inherited client jar may live under inheritsFrom id
    if (!util::file_exists(client_jar) && version.contains("jar")) {
        std::string jar_id = version["jar"].get<std::string>();
        client_jar = game_dir / "versions" / jar_id / (jar_id + ".jar");
    }
    if (!util::file_exists(client_jar)) {
        // Try parent folder from id field
        if (version.contains("id")) {
            std::string id = version["id"].get<std::string>();
            fs::path alt = game_dir / "versions" / id / (id + ".jar");
            if (util::file_exists(alt))
                client_jar = alt;
        }
    }
    if (!util::file_exists(client_jar)) {
        result.message = "找不到客户端 jar。请先用官方启动器完整下载该版本。\n期望路径: " + client_jar.string();
        return result;
    }

    std::vector<fs::path> classpath;
    std::vector<fs::path> native_jars;
    collect_libraries(version, game_dir, classpath, native_jars);
    classpath.push_back(client_jar);

    fs::path natives_dir = version_dir / "natives";
    if (!native_jars.empty()) {
        std::error_code ec;
        fs::remove_all(natives_dir, ec);
        fs::create_directories(natives_dir, ec);
        for (const auto& nj : native_jars) {
            std::string e2;
            if (!util::extract_zip(nj, natives_dir, &e2)) {
                result.message = "解压 natives 失败 (" + nj.filename().string() + "): " + e2;
                return result;
            }
        }
        // Remove META-INF signatures that can break natives loading
        fs::path meta = natives_dir / "META-INF";
        if (util::dir_exists(meta))
            fs::remove_all(meta, ec);
    } else {
        std::error_code ec;
        fs::create_directories(natives_dir, ec);
    }

    std::string cp = util::join(
        [&] {
            std::vector<std::string> s;
            s.reserve(classpath.size());
            for (const auto& p : classpath)
                s.push_back(p.string());
            return s;
        }(),
        ";");

    std::string assets_root = (game_dir / "assets").string();
    std::string asset_index;
    if (version.contains("assetIndex") && version["assetIndex"].contains("id"))
        asset_index = version["assetIndex"]["id"].get<std::string>();
    else if (version.contains("assets"))
        asset_index = version["assets"].get<std::string>();
    else
        asset_index = "legacy";

    std::string main_class = version.value("mainClass", "net.minecraft.client.main.Main");
    std::string uuid = util::offline_player_uuid(req.config.username);
    std::string version_name = version.value("id", req.version_id);
    std::string version_type = version.value("type", "release");

    std::map<std::string, std::string> vars = {
        {"auth_player_name", req.config.username},
        {"version_name", version_name},
        {"game_directory", game_dir.string()},
        {"assets_root", assets_root},
        {"assets_index_name", asset_index},
        {"auth_uuid", uuid},
        {"auth_access_token", "0"},
        {"clientid", "0"},
        {"auth_xuid", "0"},
        {"user_type", "legacy"},
        {"version_type", version_type},
        {"natives_directory", natives_dir.string()},
        {"launcher_name", "mc-minimal-launcher"},
        {"launcher_version", "0.1"},
        {"classpath", cp},
        {"library_directory", (game_dir / "libraries").string()},
        {"classpath_separator", ";"},
        {"primary_jar", client_jar.string()},
    };

    std::vector<std::string> jvm_args;
    jvm_args.push_back("-Xmx" + std::to_string(req.config.max_memory_mb) + "M");
    jvm_args.push_back("-Xms512M");
    if (!req.config.extra_jvm_args.empty()) {
        // naive split by space
        for (auto& part : util::split(req.config.extra_jvm_args, ' ')) {
            part = util::trim(part);
            if (!part.empty())
                jvm_args.push_back(part);
        }
    }

    std::vector<std::string> game_args;

    if (version.contains("arguments")) {
        auto jvm = expand_argument_list(version["arguments"].value("jvm", json::array()));
        auto game = expand_argument_list(version["arguments"].value("game", json::array()));
        for (auto& a : jvm)
            jvm_args.push_back(substitute_vars(a, vars));
        for (auto& a : game)
            game_args.push_back(substitute_vars(a, vars));
    } else if (version.contains("minecraftArguments")) {
        // Legacy
        jvm_args.push_back("-Djava.library.path=" + natives_dir.string());
        jvm_args.push_back("-cp");
        jvm_args.push_back(cp);
        std::string ma = version["minecraftArguments"].get<std::string>();
        ma = substitute_vars(ma, vars);
        // Also old placeholders
        ma = util::replace_all(ma, "${auth_session}", "0");
        ma = util::replace_all(ma, "${game_assets}", assets_root);
        ma = util::replace_all(ma, "${user_properties}", "{}");
        // split
        std::istringstream iss(ma);
        std::string tok;
        while (iss >> tok)
            game_args.push_back(tok);
    } else {
        jvm_args.push_back("-Djava.library.path=" + natives_dir.string());
        jvm_args.push_back("-cp");
        jvm_args.push_back(cp);
        game_args = {
            "--username", req.config.username,
            "--version", version_name,
            "--gameDir", game_dir.string(),
            "--assetsDir", assets_root,
            "--assetIndex", asset_index,
            "--uuid", uuid,
            "--accessToken", "0",
            "--userType", "legacy",
            "--versionType", version_type,
        };
    }

    // Ensure classpath present for modern args format
    bool has_classpath_flag = false;
    for (size_t i = 0; i < jvm_args.size(); ++i) {
        if (jvm_args[i] == "-cp" || jvm_args[i] == "-classpath") {
            has_classpath_flag = true;
            break;
        }
    }
    if (!has_classpath_flag) {
        // Modern JSON usually includes ${classpath} via -cp
        bool any_cp_token = false;
        for (const auto& a : jvm_args) {
            if (a.find(cp) != std::string::npos || a == cp) {
                any_cp_token = true;
                break;
            }
        }
        if (!any_cp_token) {
            jvm_args.push_back("-cp");
            jvm_args.push_back(cp);
        }
    }

    bool has_natives = false;
    for (const auto& a : jvm_args) {
        if (a.find("java.library.path") != std::string::npos) {
            has_natives = true;
            break;
        }
    }
    if (!has_natives)
        jvm_args.insert(jvm_args.begin(), "-Djava.library.path=" + natives_dir.string());

    std::vector<std::string> all_args = jvm_args;
    all_args.push_back(main_class);
    all_args.insert(all_args.end(), game_args.begin(), game_args.end());

    // Preview (truncate classpath in log)
    {
        std::ostringstream prev;
        prev << req.java_path << " ... -cp <" << classpath.size() << " jars> " << main_class
             << " --username " << req.config.username << " --version " << version_name;
        result.command_preview = prev.str();
    }

    // Write argfile if command line would be huge
    std::string launch_err;
    std::ostringstream full;
    full << util::quote_arg(req.java_path);
    for (const auto& a : all_args)
        full << ' ' << util::quote_arg(a);

    bool ok = false;
    if (full.str().size() > 30000) {
        // Java argument file
        fs::path argfile = version_dir / "minimal-launcher.args";
        std::ostringstream af;
        for (const auto& a : all_args) {
            if (a.find(' ') != std::string::npos || a.find('\t') != std::string::npos)
                af << '"' << util::replace_all(a, "\"", "\\\"") << "\"\n";
            else
                af << a << "\n";
        }
        util::write_text_file(argfile, af.str());
        ok = start_process(req.java_path, {"@" + argfile.string()}, game_dir.string(), &launch_err);
    } else {
        ok = start_process(req.java_path, all_args, game_dir.string(), &launch_err);
    }

    if (!ok) {
        result.message = launch_err.empty() ? "启动进程失败" : launch_err;
        return result;
    }

    result.ok = true;
    result.message = "已启动 " + version_name + "（离线：" + req.config.username + "）";
    return result;
}
