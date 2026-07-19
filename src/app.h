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
#include "mc_download.h"
#include "mc_version.h"

#include <GLFW/glfw3.h>

#include <string>
#include <vector>
#include <deque>

enum class Page {
    Home = 0,
    Downloads = 1,
    Profiles = 2,
    Settings = 3,
};

struct App {
    GLFWwindow* window = nullptr;
    float ui_scale = 1.0f;

    Config config;
    Page page = Page::Home;

    std::vector<VersionInfo> versions;
    int selected_version = 0;

    // ImGui edit buffers
    char username_buf[64]{};
    char game_dir_buf[512]{};
    char java_path_buf[512]{};
    char extra_jvm_buf[256]{};
    int max_memory_mb = 2048;

    // Download UI
    McDownloadService downloader;
    bool manifest_loaded = false;
    bool show_snapshots = false;
    bool show_old = false;
    int selected_remote = 0;
    char version_filter[64]{};
    DownloadStatus last_dl{};
    bool was_downloading = false;

    std::string status;
    std::deque<std::string> log;
    bool busy = false;

    void init(GLFWwindow* win, float scale);
    void tick(); // poll download status etc.
    void sync_buffers_from_config();
    void sync_config_from_buffers();
    void refresh_versions();
    void request_close();
    void launch_game();
    void push_log(const std::string& line);
    void save_config();

    void fetch_remote_manifest();
    void start_download_selected();
    DownloadMirror current_mirror() const;
    std::vector<int> filtered_remote_indices() const;
};

App& GetApp();
