/*
 * launcher - desktop launcher application
 * Copyright (C) 2026 antinomie1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License only.
 */

#include "ui.h"
#include "app.h"
#include "font.h"
#include "java_finder.h"
#include "util.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace UI {

namespace {

float S(float v)
{
    return v * GetApp().ui_scale;
}

void BtnStyle(bool active)
{
    ImGui::PushStyleColor(ImGuiCol_Button,
        active ? ImVec4(95.0f / 255.0f, 158.0f / 255.0f, 160.0f / 255.0f, 0.9f)
               : ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        active ? ImVec4(95.0f / 255.0f, 158.0f / 255.0f, 160.0f / 255.0f, 0.85f)
               : ImVec4(0.0f, 0.0f, 0.0f, 0.15f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
        active ? ImVec4(95.0f / 255.0f, 158.0f / 255.0f, 160.0f / 255.0f, 0.9f)
               : ImVec4(0.0f, 0.0f, 0.0f, 0.2f));
}

void TitleBar()
{
    App& app = GetApp();
    const float h = ImGui::GetContentRegionAvail().y;
    const float close_w = S(36.0f);
    const float drag_w = (std::max)(1.0f, ImGui::GetContentRegionAvail().x - close_w - S(8.0f));

    ImGui::PushFont(Font::boldFont);
    const char* title = "Minecraft Launcher";
    const ImVec2 tsize = ImGui::CalcTextSize(title);
    ImGui::SetCursorPosY((h - tsize.y) * 0.5f);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();

    // Drag handle over the left area (drawn after title so input is on top)
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::InvisibleButton("##title_drag", ImVec2(drag_w, h));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && app.window) {
        int x = 0, y = 0;
        glfwGetWindowPos(app.window, &x, &y);
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        glfwSetWindowPos(app.window, x + (int)d.x, y + (int)d.y);
    }

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorPosY(S(4.0f));
    if (ImGui::Button("X", ImVec2(close_w, S(30.0f))))
        app.request_close();
}

void NavButton(const char* label, Page page, float width)
{
    App& app = GetApp();
    bool active = app.page == page;
    BtnStyle(active);
    if (ImGui::Button(label, ImVec2(width, S(40.0f))))
        app.page = page;
    ImGui::PopStyleColor(3);
}

void BottomNav()
{
    NavButton(u8"主页", Page::Home, S(70.0f));
    ImGui::SameLine(0.0f, S(16.0f));
    NavButton(u8"下载", Page::Downloads, S(70.0f));
    ImGui::SameLine(0.0f, S(16.0f));
    NavButton(u8"档案管理", Page::Profiles, S(110.0f));
    ImGui::SameLine(0.0f, S(16.0f));
    NavButton(u8"设置", Page::Settings, S(70.0f));
}

void PageHome()
{
    App& app = GetApp();
    ImGui::TextUnformatted(u8"离线启动已安装的 Minecraft 版本");
    ImGui::Spacing();

    ImGui::TextUnformatted(u8"用户名");
    ImGui::SetNextItemWidth(S(280.0f));
    ImGui::InputText("##username", app.username_buf, sizeof(app.username_buf));

    ImGui::TextUnformatted(u8"版本");
    ImGui::SetNextItemWidth(S(280.0f));
    if (app.versions.empty()) {
        ImGui::TextDisabled(u8"（未找到版本，请到「下载」查看说明）");
    } else {
        if (ImGui::BeginCombo("##version", app.versions[app.selected_version].id.c_str())) {
            for (int i = 0; i < (int)app.versions.size(); ++i) {
                bool sel = (i == app.selected_version);
                if (ImGui::Selectable(app.versions[i].id.c_str(), sel))
                    app.selected_version = i;
                if (sel)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"刷新"))
            app.refresh_versions();
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(app.busy || app.versions.empty());
    BtnStyle(true);
    if (ImGui::Button(u8"启动游戏", ImVec2(S(160.0f), S(44.0f))))
        app.launch_game();
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextWrapped("%s", app.status.c_str());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted(u8"日志");
    ImGui::BeginChild("##log", ImVec2(0, S(140.0f)), true);
    for (const auto& line : app.log)
        ImGui::TextWrapped("%s", line.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

void PageDownloads()
{
    App& app = GetApp();
    ImGui::PushFont(Font::boldFont);
    ImGui::TextUnformatted(u8"下载版本");
    ImGui::PopFont();
    ImGui::Spacing();

    ImGui::TextUnformatted(u8"下载源");
    ImGui::SameLine();
    int mirror = app.config.download_mirror;
    if (ImGui::RadioButton(u8"BMCLAPI（推荐国内）", mirror == 1)) {
        app.config.download_mirror = 1;
        app.manifest_loaded = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(u8"官方 Mojang", mirror == 0)) {
        app.config.download_mirror = 0;
        app.manifest_loaded = false;
    }

    ImGui::Checkbox(u8"显示快照", &app.show_snapshots);
    ImGui::SameLine();
    ImGui::Checkbox(u8"显示远古版本", &app.show_old);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(160.0f));
    ImGui::InputTextWithHint("##vfilter", u8"筛选版本号", app.version_filter, sizeof(app.version_filter));

    ImGui::BeginDisabled(app.downloader.busy());
    if (ImGui::Button(u8"获取版本列表", ImVec2(S(140.0f), S(32.0f))))
        app.fetch_remote_manifest();
    ImGui::SameLine();
    if (ImGui::Button(u8"打开游戏目录", ImVec2(S(140.0f), S(32.0f)))) {
        app.sync_config_from_buffers();
        util::open_folder(app.config.game_dir);
    }
    ImGui::EndDisabled();

    if (app.manifest_loaded) {
        ImGui::Text(u8"最新正式版: %s   快照: %s",
                    app.downloader.latest_release().c_str(),
                    app.downloader.latest_snapshot().c_str());
    } else {
        ImGui::TextDisabled(u8"尚未获取远程版本列表");
    }

    auto indices = app.filtered_remote_indices();
    if (app.selected_remote >= (int)indices.size())
        app.selected_remote = 0;

    ImGui::BeginChild("##remote_list", ImVec2(0, S(160.0f)), ImGuiChildFlags_Borders);
    if (indices.empty()) {
        ImGui::TextDisabled(u8"无匹配版本（请先获取列表，或调整筛选）");
    } else {
        for (int row = 0; row < (int)indices.size(); ++row) {
            const auto& v = app.downloader.remote_versions()[indices[row]];
            bool sel = (row == app.selected_remote);
            char label[128];
            std::snprintf(label, sizeof(label), "%s  (%s)", v.id.c_str(), v.type.c_str());
            if (ImGui::Selectable(label, sel))
                app.selected_remote = row;
        }
    }
    ImGui::EndChild();

    ImGui::BeginDisabled(app.downloader.busy() || indices.empty());
    BtnStyle(true);
    if (ImGui::Button(u8"安装所选版本", ImVec2(S(160.0f), S(40.0f))))
        app.start_download_selected();
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!app.downloader.busy());
    if (ImGui::Button(u8"取消", ImVec2(S(80.0f), S(40.0f))))
        app.downloader.request_cancel();
    ImGui::EndDisabled();

    // Progress
    const DownloadStatus& st = app.last_dl;
    if (st.busy || st.overall_progress > 0.f) {
        ImGui::Spacing();
        ImGui::ProgressBar(st.overall_progress, ImVec2(-1, S(22.0f)));
        ImGui::TextWrapped("%s | %s (%d/%d)",
                           st.phase.c_str(),
                           st.message.c_str(),
                           st.files_done,
                           st.files_total);
        if (!st.current_file.empty())
            ImGui::TextDisabled("%s", st.current_file.c_str());
    }
    if (!st.error.empty() && !st.busy)
        ImGui::TextColored(ImVec4(0.8f, 0.1f, 0.1f, 1.f), "%s", st.error.c_str());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text(u8"本机已安装：%d", (int)app.versions.size());
    ImGui::BeginChild("##local_ver", ImVec2(0, S(70.0f)), ImGuiChildFlags_Borders);
    for (const auto& v : app.versions)
        ImGui::BulletText("%s", v.id.c_str());
    if (app.versions.empty())
        ImGui::TextDisabled(u8"空 — 安装完成后会出现在这里，并可在主页启动");
    ImGui::EndChild();
}

void PageProfiles()
{
    App& app = GetApp();
    ImGui::PushFont(Font::boldFont);
    ImGui::TextUnformatted(u8"档案管理");
    ImGui::PopFont();
    ImGui::Spacing();

    ImGui::TextUnformatted(u8"游戏目录 (.minecraft)");
    ImGui::SetNextItemWidth(-S(100.0f));
    ImGui::InputText("##gamedir", app.game_dir_buf, sizeof(app.game_dir_buf));
    ImGui::SameLine();
    if (ImGui::Button(u8"打开##gd"))
        util::open_folder(app.game_dir_buf);

    ImGui::TextUnformatted(u8"离线用户名");
    ImGui::SetNextItemWidth(S(280.0f));
    ImGui::InputText("##user2", app.username_buf, sizeof(app.username_buf));

    ImGui::Spacing();
    if (ImGui::Button(u8"应用并刷新版本", ImVec2(S(180.0f), S(36.0f)))) {
        app.save_config();
        app.refresh_versions();
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"恢复默认目录", ImVec2(S(160.0f), S(36.0f)))) {
        std::snprintf(app.game_dir_buf, sizeof(app.game_dir_buf), "%s", Config::default_game_dir().c_str());
    }

    ImGui::Spacing();
    ImGui::TextWrapped(u8"提示：离线 UUID 由用户名生成，与正版账号无关。服务器若开启正版验证将无法进入。");
}

void PageSettings()
{
    App& app = GetApp();
    ImGui::PushFont(Font::boldFont);
    ImGui::TextUnformatted(u8"设置");
    ImGui::PopFont();
    ImGui::Spacing();

    ImGui::TextUnformatted(u8"Java 路径 (java.exe / javaw.exe)");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##java", app.java_path_buf, sizeof(app.java_path_buf));
    if (ImGui::Button(u8"自动检测 Java")) {
        std::string j = find_java(app.java_path_buf);
        if (j.empty())
            j = find_java({});
        if (j.empty()) {
            app.status = u8"未找到 Java";
            app.push_log(app.status);
        } else {
            std::snprintf(app.java_path_buf, sizeof(app.java_path_buf), "%s", j.c_str());
            app.push_log(std::string(u8"检测到 Java: ") + j);
            app.status = u8"已填入 Java 路径";
        }
    }

    ImGui::Spacing();
    ImGui::TextUnformatted(u8"最大内存 (MB)");
    ImGui::SetNextItemWidth(S(200.0f));
    ImGui::SliderInt("##mem", &app.max_memory_mb, 512, 16384);

    ImGui::TextUnformatted(u8"额外 JVM 参数");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##jvm", app.extra_jvm_buf, sizeof(app.extra_jvm_buf));

    ImGui::Spacing();
    if (ImGui::Button(u8"保存设置", ImVec2(S(140.0f), S(36.0f))))
        app.save_config();

    ImGui::Spacing();
    ImGui::TextUnformatted(u8"默认下载源（下载页可改）");
    int mirror = app.config.download_mirror;
    if (ImGui::RadioButton(u8"BMCLAPI##set", mirror == 1))
        app.config.download_mirror = 1;
    ImGui::SameLine();
    if (ImGui::RadioButton(u8"官方 Mojang##set", mirror == 0))
        app.config.download_mirror = 0;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextWrapped(
        u8"launcher 0.2 — Minecraft 离线启动 + 版本下载\n"
        u8"技术栈: Dear ImGui + GLFW + OpenGL | 许可: GPL-2.0-only");
}

} // namespace

void SetStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Button] = ImVec4(1.0f, 1.0f, 1.0f, 0.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.0f, 0.0f, 0.0f, 0.15f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.0f, 0.0f, 0.0f, 0.2f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.45f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.55f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.65f);
    style.Colors[ImGuiCol_Header] = ImVec4(95.0f / 255.0f, 158.0f / 255.0f, 160.0f / 255.0f, 0.45f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(95.0f / 255.0f, 158.0f / 255.0f, 160.0f / 255.0f, 0.65f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.20f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.95f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.1f, 0.1f, 0.1f, 0.92f);
    style.FrameRounding = 4.0f;
    style.WindowRounding = 6.0f;
    style.GrabRounding = 3.0f;
}

void RenderUI()
{
    App& app = GetApp();
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

    ImGui::Begin("Main", nullptr, window_flags);

    const float bottom_height = S(50.0f);
    const float top_height = S(40.0f);
    const float middle_height = ImGui::GetWindowHeight() - bottom_height - top_height - S(20.0f);

    ImGui::BeginChild("TopRegion", ImVec2(0, top_height), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    TitleBar();
    ImGui::EndChild();

    ImGui::BeginChild("MiddleRegion", ImVec2(0, middle_height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoBackground);
    ImGui::Dummy(ImVec2(0, S(4.0f)));
    switch (app.page) {
    case Page::Home:      PageHome(); break;
    case Page::Downloads: PageDownloads(); break;
    case Page::Profiles:  PageProfiles(); break;
    case Page::Settings:  PageSettings(); break;
    }
    ImGui::EndChild();

    ImGui::BeginChild("BottomRegion", ImVec2(0, bottom_height), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    BottomNav();
    ImGui::EndChild();

    ImGui::End();
}

} // namespace UI
