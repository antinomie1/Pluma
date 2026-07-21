#include "ui/frame.h"

#include "logic/engine.h"
#include "platform/window.h"

#include <imgui.h>
#include <imgui_md2/imgui_md2.h>

#include <cstdio>

namespace ui {
namespace {

constexpr float kTitlePadding = 24.0f;
constexpr float kContentPadding = 24.0f;
constexpr float kFabGap = 6.0f; // gap between / around the caption FABs

// Material Icons "remove" glyph (U+E15B) as the minimise icon. There is no
// Icons:: constant for it; valid because the embedded icon font covers PUA.
constexpr const char* kMinimizeIcon = "\xee\x85\x9b";

} // namespace

void BuildFrame(platform::Window& window, const logic::State& logic_state) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##pluma_root", nullptr, flags);
    ImGui::PopStyleVar(3);

    const ImGuiMD2::Theme& theme = ImGuiMD2::GetTheme();
    const ImGuiMD2::Color bar_color =
        theme.app_bar.a > 0.0f ? theme.app_bar : theme.colors.primary;
    const ImGuiMD2::Color on_bar =
        theme.on_app_bar.a > 0.0f ? theme.on_app_bar : theme.colors.on_primary;

    const ImVec2 win_pos = ImGui::GetWindowPos();
    const ImVec2 win_size = ImGui::GetWindowSize();
    const float bar_right = win_pos.x + win_size.x;

    const float fab = ImGuiMD2::Metrics::FabSize();
    const float bar_height = fab + 8.0f;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(win_pos, ImVec2(bar_right, win_pos.y + bar_height), bar_color.U32());

    // Title: Headline6 (Medium weight), white, vertically centred. Drawn twice
    // with a sub-pixel offset (faux-bold) for extra weight beyond Medium.
    ImFont* title_font = ImGuiMD2::GetTheme().fonts.Get(ImGuiMD2::TextStyle::Headline6);
    ImGui::PushFont(title_font);
    const float title_h = ImGui::GetTextLineHeight();
    ImGui::PopFont();
    const float title_y = win_pos.y + (bar_height - title_h) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(win_pos.x + kTitlePadding, title_y));
    ImGuiMD2::Text(ImGuiMD2::TextStyle::Headline6, "Pluma", on_bar);
    ImGui::SetCursorScreenPos(ImVec2(win_pos.x + kTitlePadding + 0.8f, title_y));
    ImGuiMD2::Text(ImGuiMD2::TextStyle::Headline6, "Pluma", on_bar);

    // MD2 FloatingActionButtons as caption controls.  56 × 56 dp, secondary
    // (blue) container with white icons.  Vertically centred; right-aligned.
    // ElevationShadow bug fixed in imgui_md2: the concentric case now draws a
    // centred circular shadow instead of a downward-offset drop shadow.
    const float fab_y = win_pos.y + (bar_height - fab) * 0.5f;
    const float close_x = bar_right - kFabGap - fab;
    const float min_x = close_x - kFabGap - fab;

    ImGui::SetCursorScreenPos(ImVec2(min_x, fab_y));
    if (ImGuiMD2::FloatingActionButton("##win_min", kMinimizeIcon)) {
        window.minimize();
    }
    ImGui::SetCursorScreenPos(ImVec2(close_x, fab_y));
    if (ImGuiMD2::FloatingActionButton("##win_close", ImGuiMD2::Icons::Close)) {
        window.close();
    }

    // Publish the draggable caption region for the Windows native hit-test: the
    // full bar height, minus the two FABs (plus surrounding gaps) on the right.
    window.setCaptionRegion(bar_height, (fab + kFabGap) * 2.0f + kFabGap);

    // Content area.
    ImGui::SetCursorScreenPos(
        ImVec2(win_pos.x + kContentPadding, win_pos.y + bar_height + kContentPadding));
    ImGui::BeginGroup();
    ImGuiMD2::Text(ImGuiMD2::TextStyle::Headline6, "Framework skeleton");
    ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2,
                   "Event, render and logic threads are running independently.");
    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    static bool demo_toggle = true;
    ImGuiMD2::Switch("Demo switch", &demo_toggle);
    if (ImGuiMD2::ContainedButton("PRIMARY ACTION")) {
        // Placeholder for future features.
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    char ticks[96];
    std::snprintf(ticks, sizeof(ticks), "logic ticks: %llu  (uptime %.1fs)",
                  static_cast<unsigned long long>(logic_state.ticks), logic_state.uptime_seconds);
    ImGuiMD2::Text(ImGuiMD2::TextStyle::Caption, ticks);
    ImGui::EndGroup();

    ImGui::End();
}

} // namespace ui
