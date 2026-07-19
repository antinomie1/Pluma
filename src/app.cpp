/*
 * launcher - desktop launcher application
 * Copyright (C) 2026 antinomie1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License only.
 */

#include "app.h"
#include "java_finder.h"
#include "mc_launch.h"
#include "util.h"

#include <cctype>
#include <cstdio>
#include <cstring>

static App g_app;

App& GetApp()
{
    return g_app;
}

void App::init(GLFWwindow* win, float scale)
{
    window = win;
    ui_scale = scale;
    config.load();
    sync_buffers_from_config();
    refresh_versions();

    std::string java = find_java(config.java_path);
    if (java.empty())
        push_log("未检测到 Java。请在「设置」中填写 java.exe / javaw.exe 路径。");
    else {
        if (config.java_path.empty()) {
            config.java_path = java;
            std::snprintf(java_path_buf, sizeof(java_path_buf), "%s", java.c_str());
        }
        push_log(std::string("Java: ") + java);
    }

    if (!util::dir_exists(config.game_dir))
        push_log("游戏目录不存在: " + config.game_dir + "（可在「档案管理」修改）");
    else if (versions.empty())
        push_log("未找到已安装版本。可到「下载」页安装官方版本。");
    else
        push_log("找到 " + std::to_string(versions.size()) + " 个已安装版本。");

    status = "就绪（离线模式）";
}

void App::tick()
{
    DownloadStatus st = downloader.snapshot();
    last_dl = st;

    if (was_downloading && !st.busy) {
        if (!st.error.empty()) {
            push_log("[下载] 失败: " + st.error);
            status = st.error;
        } else {
            push_log("[下载] " + st.message);
            status = st.message;
            refresh_versions();
            // select newly installed if possible
            for (int i = 0; i < (int)versions.size(); ++i) {
                // match message "安装完成: id"
                if (st.message.find(versions[i].id) != std::string::npos) {
                    selected_version = i;
                    break;
                }
            }
        }
        busy = false;
    }
    was_downloading = st.busy;
    if (st.busy)
        busy = true;
}

void App::sync_buffers_from_config()
{
    std::snprintf(username_buf, sizeof(username_buf), "%s", config.username.c_str());
    std::snprintf(game_dir_buf, sizeof(game_dir_buf), "%s", config.game_dir.c_str());
    std::snprintf(java_path_buf, sizeof(java_path_buf), "%s", config.java_path.c_str());
    std::snprintf(extra_jvm_buf, sizeof(extra_jvm_buf), "%s", config.extra_jvm_args.c_str());
    max_memory_mb = config.max_memory_mb;
}

void App::sync_config_from_buffers()
{
    config.username = username_buf;
    config.game_dir = game_dir_buf;
    config.java_path = java_path_buf;
    config.extra_jvm_args = extra_jvm_buf;
    config.max_memory_mb = max_memory_mb;
    if (selected_version >= 0 && selected_version < (int)versions.size())
        config.last_version = versions[selected_version].id;
}

void App::refresh_versions()
{
    sync_config_from_buffers();
    versions = list_installed_versions(config.game_dir);
    selected_version = 0;
    if (!config.last_version.empty()) {
        for (int i = 0; i < (int)versions.size(); ++i) {
            if (versions[i].id == config.last_version) {
                selected_version = i;
                break;
            }
        }
    }
}

void App::request_close()
{
    if (window)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void App::push_log(const std::string& line)
{
    log.push_back(line);
    while (log.size() > 80)
        log.pop_front();
}

void App::save_config()
{
    sync_config_from_buffers();
    if (config.save()) {
        status = "配置已保存";
        push_log("配置已写入: " + Config::config_path());
    } else {
        status = "保存配置失败";
        push_log("无法写入配置文件");
    }
}

void App::launch_game()
{
    if (busy)
        return;
    busy = true;
    sync_config_from_buffers();

    if (versions.empty() || selected_version < 0 || selected_version >= (int)versions.size()) {
        status = "没有可用版本";
        push_log(status);
        busy = false;
        return;
    }

    const std::string& ver = versions[selected_version].id;
    config.last_version = ver;
    config.save();

    LaunchRequest req;
    req.config = config;
    req.version_id = ver;
    req.java_path = find_java(config.java_path);
    if (req.java_path.empty())
        req.java_path = config.java_path;

    push_log("正在启动 " + ver + " ...");
    status = "启动中…";

    LaunchResult r = launch_minecraft(req);
    if (!r.command_preview.empty())
        push_log(r.command_preview);
    push_log(r.message);
    status = r.message;
    busy = false;
}

DownloadMirror App::current_mirror() const
{
    return config.download_mirror == 0 ? DownloadMirror::Official : DownloadMirror::BMCLAPI;
}

void App::fetch_remote_manifest()
{
    if (downloader.busy()) {
        push_log("下载任务进行中，请稍候");
        return;
    }
    push_log("正在获取版本列表…");
    std::string err;
    if (!downloader.fetch_manifest(current_mirror(), &err)) {
        push_log("获取版本列表失败: " + (err.empty() ? "未知错误" : err));
        status = "获取版本列表失败";
        manifest_loaded = false;
        return;
    }
    manifest_loaded = true;
    selected_remote = 0;
    push_log("远程版本: " + std::to_string(downloader.remote_versions().size()) +
             " 个（最新正式版 " + downloader.latest_release() + "）");
    status = "版本列表已更新";
}

std::vector<int> App::filtered_remote_indices() const
{
    std::vector<int> idx;
    const auto& list = downloader.remote_versions();
    std::string filter = version_filter;
    for (char& c : filter)
        c = (char)std::tolower((unsigned char)c);

    for (int i = 0; i < (int)list.size(); ++i) {
        const auto& v = list[i];
        if (v.type_enum == RemoteVersionType::Snapshot && !show_snapshots)
            continue;
        if ((v.type_enum == RemoteVersionType::OldBeta || v.type_enum == RemoteVersionType::OldAlpha) && !show_old)
            continue;
        if (v.type_enum == RemoteVersionType::Other && !show_old && !show_snapshots)
            continue;
        if (!filter.empty()) {
            std::string id = v.id;
            for (char& c : id)
                c = (char)std::tolower((unsigned char)c);
            if (id.find(filter) == std::string::npos)
                continue;
        }
        idx.push_back(i);
    }
    return idx;
}

void App::start_download_selected()
{
    if (downloader.busy())
        return;
    auto indices = filtered_remote_indices();
    if (indices.empty() || selected_remote < 0 || selected_remote >= (int)indices.size()) {
        push_log("请先选择要安装的版本");
        return;
    }
    sync_config_from_buffers();
    config.save();

    const auto& rv = downloader.remote_versions()[indices[selected_remote]];
    push_log("开始安装 " + rv.id + " …");
    status = "下载中: " + rv.id;
    was_downloading = true;
    busy = true;
    if (!downloader.start_install(config.game_dir, rv.id, current_mirror())) {
        push_log("无法启动下载任务");
        busy = false;
        was_downloading = false;
    }
}
