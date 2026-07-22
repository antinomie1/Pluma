#include "ui/frame.h"

#include "config/config.h"
#include "logic/engine.h"
#include "platform/window.h"
#include "ui/app_state.h"
#include "ui/i18n.h"
#include "ui/theme.h"

#include <imgui.h>
#include <imgui_md2/imgui_md2.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ui {
namespace {

constexpr float kTitlePadding = 24.0f;
constexpr float kContentPadding = 24.0f;
constexpr float kFabGap = 6.0f; // gap between / around the caption FABs

// Bottom navigation bar.
constexpr float kNavEdgeGap = 24.0f; // gap at both ends (matches kContentPadding)
constexpr float kNavItemGap = 8.0f;  // spacing between the left-group buttons
constexpr float kNavHeight = 56.0f;  // bar height; buttons are vertically centred

// Master-detail layout (Download / Settings pages): fixed-width left rail of
// ListItem rows, right-hand detail content filling the rest of the width.
constexpr float kRailWidth = 180.0f;

// Master-detail pages (Download / Settings) use a thinner, uniform margin on
// all four sides of the content region, plus a matching gap between the two
// cards -- distinct from kContentPadding, which the non-split pages still use.
constexpr float kPageMargin = 16.0f;
constexpr float kCardGap = 16.0f;

// Curated accent swatches offered by the Settings > Appearance color picker.
// Names are plain English swatch names, not translated (matching how e.g.
// "Blue"/"Teal" read as proper nouns for a Material palette in every
// language this app currently ships).
struct AccentOption {
    ImGuiMD2::Swatch swatch;
    const char* name;
};
constexpr AccentOption kAccents[] = {
    {ImGuiMD2::Swatch::Blue, "Blue"},     {ImGuiMD2::Swatch::Teal, "Teal"},
    {ImGuiMD2::Swatch::Indigo, "Indigo"}, {ImGuiMD2::Swatch::Purple, "Purple"},
    {ImGuiMD2::Swatch::Green, "Green"},   {ImGuiMD2::Swatch::Amber, "Amber"},
    {ImGuiMD2::Swatch::Orange, "Orange"}, {ImGuiMD2::Swatch::Red, "Red"},
    {ImGuiMD2::Swatch::Pink, "Pink"},
};
constexpr int kAccentCount = static_cast<int>(sizeof(kAccents) / sizeof(kAccents[0]));
const char* const kAccentNames[kAccentCount] = {
    "Blue", "Teal", "Indigo", "Purple", "Green", "Amber", "Orange", "Red", "Pink",
};

// Material Icons "remove" glyph (U+E15B) as the minimise icon. There is no
// Icons:: constant for it; valid because the embedded icon font covers PUA.
constexpr const char* kMinimizeIcon = "\xee\x85\x9b";

// Predicts the auto width of an ImGuiMD2::ContainedButton for `visible_utf8`,
// replicating Button's own formula (imgui_md2/src/components.cpp): the visible
// text is ASCII-uppercased (CJK left untouched, matching MD2's Uppercase),
// measured in the Button text-style font, plus 32px of horizontal padding.
// Used to right-align the Tasks button precisely. Pass the plain translation
// (no "###" suffix) — Button strips that itself before measuring.
float NavButtonWidth(const char* visible_utf8) {
    std::string text = visible_utf8;
    for (char& c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 128) c = static_cast<char>(std::toupper(u));
    }
    ImFont* font = ImGuiMD2::GetTheme().fonts.Get(ImGuiMD2::TextStyle::Button);
    if (font) ImGui::PushFont(font);
    const float width = ImGui::CalcTextSize(text.c_str()).x + 32.0f;
    if (font) ImGui::PopFont();
    return width;
}

// Renders a master-detail layout for a page's left-rail sub-navigation: a
// fixed-width (kRailWidth) column of ListItem rows -- click sets *current --
// then hands off to `draw_detail` for the right-hand content filling the
// rest of the row. Rail rows use plain Tr() (no "###" suffix): selection
// state lives in AppState, not widget-internal state, so there is no
// identity to preserve across a language switch. Both rail and detail render
// as elevated cards (BeginCard/EndCard: shadow + rounding + surface fill +
// 16dp inner padding), anchored at `origin` and filling `size` so they align
// to the same edges as the surrounding content region.
template <typename DrawDetail>
void MasterDetail(const char* const* keys, int count, int& current, ImVec2 origin,
                   ImVec2 size, DrawDetail&& draw_detail) {
    ImGui::SetCursorScreenPos(origin);
    ImGuiMD2::BeginCard("##rail", ImVec2(kRailWidth, size.y), 2);
    for (int i = 0; i < count; ++i) {
        if (ImGuiMD2::ListItem(ui::i18n::Tr(keys[i]), nullptr, nullptr, nullptr, i == current)) {
            current = i;
        }
    }
    ImGuiMD2::EndCard();
    ImGui::SameLine(0.0f, kCardGap);
    ImGuiMD2::BeginCard("##detail", ImVec2(size.x - kRailWidth - kCardGap, size.y), 2);
    draw_detail();
    ImGuiMD2::EndCard();
}

} // namespace

void BuildFrame(platform::Window& window, const logic::State& logic_state,
                ui::AppState& app_state) {
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
    ImGuiMD2::Text(ImGuiMD2::TextStyle::Headline6, ui::i18n::Tr("app.title"), on_bar);
    ImGui::SetCursorScreenPos(ImVec2(win_pos.x + kTitlePadding + 0.8f, title_y));
    ImGuiMD2::Text(ImGuiMD2::TextStyle::Headline6, ui::i18n::Tr("app.title"), on_bar);

    // MD2 FloatingActionButtons as caption controls.  56 × 56 dp, vertically
    // centred and right-aligned.  Styled to blend into the app bar: at rest the
    // container matches the bar colour with no shadow, so only the white icon
    // shows.  On hover/press a centred circular shadow fades in (ElevationShadow
    // draws a radial glow for the concentric case) plus a subtle state overlay,
    // keeping the colour shift small.
    const float fab_y = win_pos.y + (bar_height - fab) * 0.5f;
    const float close_x = bar_right - kFabGap - fab;
    const float min_x = close_x - kFabGap - fab;

    ImGuiMD2::FabOptions cap;
    cap.container = bar_color;      // same colour as the app bar
    cap.content = on_bar;           // white icon
    cap.rest_elevation = 0;         // flat at rest — no shadow
    cap.hover_elevation = 6;        // soft circular shadow on hover/press

    ImGui::SetCursorScreenPos(ImVec2(min_x, fab_y));
    if (ImGuiMD2::FloatingActionButton("##win_min", kMinimizeIcon, cap)) {
        window.minimize();
    }
    ImGui::SetCursorScreenPos(ImVec2(close_x, fab_y));
    if (ImGuiMD2::FloatingActionButton("##win_close", ImGuiMD2::Icons::Close, cap)) {
        window.close();
    }

    // Publish the draggable caption region for the Windows native hit-test: the
    // full bar height, minus the two FABs (plus surrounding gaps) on the right.
    window.setCaptionRegion(bar_height, (fab + kFabGap) * 2.0f + kFabGap);

    // Content area. avail_h spans from the content top down to the bottom nav
    // bar's top edge -- the same win_pos.y + win_size.y - kNavHeight formula
    // nav_top uses below -- so a page's master-detail rail/detail children
    // fill exactly the space between the top bar and the bottom nav.
    const float content_top = win_pos.y + bar_height + kContentPadding;

    // Master-detail region (Download / Settings): equal kPageMargin on all
    // four sides, instead of kContentPadding's top/left-only, ~0 right/bottom.
    const float nav_top_y = win_pos.y + win_size.y - kNavHeight;
    const ImVec2 md_min{win_pos.x + kPageMargin, win_pos.y + bar_height + kPageMargin};
    const ImVec2 md_size{win_size.x - 2.0f * kPageMargin, (nav_top_y - kPageMargin) - md_min.y};

    ImGui::SetCursorScreenPos(ImVec2(win_pos.x + kContentPadding, content_top));
    ImGui::BeginGroup();
    switch (app_state.current_page) {
        case Page::Home: {
            ImGuiMD2::Text(ImGuiMD2::TextStyle::Headline6, ui::i18n::Tr("content.heading"));
            ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2, ui::i18n::Tr("content.body"));
            ImGui::Dummy(ImVec2(0.0f, 8.0f));

            static bool demo_toggle = true;
            ImGuiMD2::Switch(ui::i18n::TrLabel("demo.switch", "demo").c_str(), &demo_toggle);
            if (ImGuiMD2::ContainedButton(ui::i18n::TrLabel("action.primary", "primary").c_str())) {
                // Placeholder for future features.
            }

            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            char ticks[96];
            std::snprintf(ticks, sizeof(ticks), ui::i18n::Tr("status.ticks"),
                          static_cast<unsigned long long>(logic_state.ticks), logic_state.uptime_seconds);
            ImGuiMD2::Text(ImGuiMD2::TextStyle::Caption, ticks);
            break;
        }
        case Page::Download: {
            static const char* const kKeys[] = {"download.games", "download.mods"};
            int current = static_cast<int>(app_state.download_section);
            MasterDetail(kKeys, 2, current, md_min, md_size, [&]() {
                switch (app_state.download_section) {
                    case DownloadSection::Games:
                        ImGuiMD2::Text(ImGuiMD2::TextStyle::Headline6, ui::i18n::Tr("download.games"));
                        break;
                    case DownloadSection::Mods:
                        ImGuiMD2::Text(ImGuiMD2::TextStyle::Headline6, ui::i18n::Tr("download.mods"));
                        break;
                    case DownloadSection::Count:
                        break;
                }
                ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2, ui::i18n::Tr("page.wip"));
            });
            app_state.download_section = static_cast<DownloadSection>(current);
            break;
        }
        case Page::Profiles: {
            ImGuiMD2::Text(ImGuiMD2::TextStyle::Headline6, ui::i18n::Tr("nav.profiles"));
            ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2, ui::i18n::Tr("page.wip"));
            break;
        }
        case Page::Settings: {
            static const char* const kKeys[] = {"settings.appearance", "settings.game",
                                                "settings.java", "settings.about"};
            int current = static_cast<int>(app_state.settings_section);
            MasterDetail(kKeys, 4, current, md_min, md_size, [&]() {
                switch (app_state.settings_section) {
                    case SettingsSection::Appearance: {
                        ImGuiMD2::Text(ImGuiMD2::TextStyle::Headline6, ui::i18n::Tr("settings.appearance"));

                        // Dark mode / accent: read the "current" values live
                        // each frame from the active theme + config (rather
                        // than a stale static), so switching pages or the
                        // language never desyncs these controls from what is
                        // actually on screen / on disk.
                        bool dark = ImGuiMD2::GetTheme().is_dark;
                        const int64_t accent_value = config::Config::Instance().GetInt(
                            "theme.accent", static_cast<int64_t>(ImGuiMD2::Swatch::Blue));
                        int accent_idx = 0;
                        for (int i = 0; i < kAccentCount; ++i) {
                            if (static_cast<int64_t>(kAccents[i].swatch) == accent_value) {
                                accent_idx = i;
                                break;
                            }
                        }

                        bool theme_changed = false;
                        if (ImGuiMD2::Switch(ui::i18n::TrLabel("settings.dark_mode", "dark").c_str(),
                                             &dark)) {
                            theme_changed = true;
                        }
                        if (ImGuiMD2::Select(ui::i18n::Tr("settings.accent"), &accent_idx,
                                             kAccentNames, kAccentCount)) {
                            theme_changed = true;
                        }
                        if (theme_changed) {
                            const ImGuiMD2::Swatch accent = kAccents[accent_idx].swatch;
                            ImGuiMD2::Theme t = ui::MakeTheme(dark, accent);
                            // SetTheme() replaces the theme wholesale, fonts
                            // included -- copy the current fonts across or
                            // all text breaks.
                            t.fonts = ImGuiMD2::GetTheme().fonts;
                            ImGuiMD2::SetTheme(t);
                            config::Config::Instance().Set("theme.dark", dark);
                            config::Config::Instance().Set("theme.accent",
                                                            static_cast<int64_t>(accent));
                            config::Config::Instance().Save();
                        }

                        // Manual language switcher (in addition to the
                        // system-locale auto-detect done once in
                        // render/renderer.cpp). Not wrapped in TrLabel():
                        // Select() draws its label with ImGuiMD2::Text(),
                        // which -- unlike Switch/Button -- does not strip a
                        // "##"/"###" suffix from the *visible* text, so a
                        // stable-id suffix here would show up literally in
                        // the caption.
                        int lang_index = ui::i18n::CurrentLanguageIndex();
                        const int lang_count = ui::i18n::LanguageCount();
                        std::vector<const char*> lang_names;
                        lang_names.reserve(static_cast<std::size_t>(lang_count));
                        for (int i = 0; i < lang_count; ++i) {
                            lang_names.push_back(ui::i18n::LanguageDisplayName(i));
                        }
                        if (ImGuiMD2::Select(ui::i18n::Tr("settings.language"), &lang_index,
                                             lang_names.data(), lang_count)) {
                            ui::i18n::SetLanguageIndex(lang_index);
                            config::Config::Instance().Set("language",
                                                            ui::i18n::CurrentLanguageCode());
                            config::Config::Instance().Save();
                        }
                        break;
                    }
                    case SettingsSection::Game:
                        ImGuiMD2::Text(ImGuiMD2::TextStyle::Headline6, ui::i18n::Tr("settings.game"));
                        ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2, ui::i18n::Tr("page.wip"));
                        break;
                    case SettingsSection::Java:
                        ImGuiMD2::Text(ImGuiMD2::TextStyle::Headline6, ui::i18n::Tr("settings.java"));
                        ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2, ui::i18n::Tr("page.wip"));
                        break;
                    case SettingsSection::About:
                        ImGuiMD2::Text(ImGuiMD2::TextStyle::Headline6, ui::i18n::Tr("settings.about"));
                        ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2, ui::i18n::Tr("settings.project_url"));
                        ImGuiMD2::Text(ImGuiMD2::TextStyle::Body1,
                                       "https://github.com/antinomie1/Pluma");
                        break;
                    case SettingsSection::Count:
                        break;
                }
            });
            app_state.settings_section = static_cast<SettingsSection>(current);
            break;
        }
        case Page::Tasks: {
            ImGuiMD2::Text(ImGuiMD2::TextStyle::Headline6, ui::i18n::Tr("nav.tasks"));
            ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2, ui::i18n::Tr("page.wip"));
            break;
        }
        case Page::Count:
            break;
    }
    ImGui::EndGroup();

    // Bottom navigation bar: a surface-coloured strip with a 1px top divider,
    // spanning the full width at the bottom of the window. The left group is
    // left-aligned; the Tasks button is right-aligned. Both ends keep an edge
    // gap so buttons never touch the window border. The active page's button
    // is Contained (primary fill); the rest are flat Text buttons that blend
    // into the surface-coloured bar.
    const float nav_top = win_pos.y + win_size.y - kNavHeight;
    draw->AddRectFilled(ImVec2(win_pos.x, nav_top),
                        ImVec2(bar_right, win_pos.y + win_size.y),
                        theme.colors.surface.U32());
    draw->AddLine(ImVec2(win_pos.x, nav_top), ImVec2(bar_right, nav_top),
                  theme.colors.outline.U32(), 1.0f);

    const float nav_btn_y = nav_top + (kNavHeight - theme.layout.button_height) * 0.5f;

    // Nav button: Contained (primary fill) when its page is active, flat Text
    // (transparent container, so the surface-coloured bar shows through) when
    // it is not. Bold in both states via the real-bold font swapped in below;
    // options.bold (faux-bold double-draw) is only a fallback for when no
    // real-bold font could be built (app_state.nav_bold_font == nullptr).
    auto NavButton = [&](Page page, const char* key, const char* id) -> bool {
        ImGuiMD2::ButtonOptions options;
        options.variant = (app_state.current_page == page) ? ImGuiMD2::ButtonVariant::Contained
                                                            : ImGuiMD2::ButtonVariant::Text;
        options.bold = (app_state.nav_bold_font == nullptr);
        return ImGuiMD2::Button(ui::i18n::TrLabel(key, id).c_str(), options);
    };

    // Draw the nav buttons in the real-bold font when available (embedded
    // Roboto-Bold + a bold CJK system face, built once by the renderer).
    // Temporarily swap the Button text style's font so every nav Button -- and
    // NavButtonWidth's measurement -- uses it; restore afterwards so other
    // Buttons keep their normal weight.
    ImFont*& btn_slot =
        ImGuiMD2::GetTheme().fonts.styles[static_cast<std::size_t>(ImGuiMD2::TextStyle::Button)];
    ImFont* prev_btn_font = btn_slot;
    if (app_state.nav_bold_font) btn_slot = app_state.nav_bold_font;

    // Left group: Home / Download / Profiles / Settings, flowing left-to-right.
    ImGui::SetCursorScreenPos(ImVec2(win_pos.x + kNavEdgeGap, nav_btn_y));
    if (NavButton(Page::Home, "nav.home", "nav_home")) {
        app_state.current_page = Page::Home;
    }
    ImGui::SameLine(0.0f, kNavItemGap);
    if (NavButton(Page::Download, "nav.download", "nav_download")) {
        app_state.current_page = Page::Download;
    }
    ImGui::SameLine(0.0f, kNavItemGap);
    if (NavButton(Page::Profiles, "nav.profiles", "nav_profiles")) {
        app_state.current_page = Page::Profiles;
    }
    ImGui::SameLine(0.0f, kNavItemGap);
    if (NavButton(Page::Settings, "nav.settings", "nav_settings")) {
        app_state.current_page = Page::Settings;
    }

    // Right end: Tasks, right-aligned by predicting its own button width.
    const float tasks_width = NavButtonWidth(ui::i18n::Tr("nav.tasks"));
    ImGui::SetCursorScreenPos(
        ImVec2(bar_right - kNavEdgeGap - tasks_width, nav_btn_y));
    if (NavButton(Page::Tasks, "nav.tasks", "nav_tasks")) {
        app_state.current_page = Page::Tasks;
    }

    ImGui::End();
}

} // namespace ui
