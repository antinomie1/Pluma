#pragma once

struct ImFont;

namespace ui {

// Render-thread-only navigation state: which page the bottom nav bar has
// selected. Order matches the nav button order (Home/Download/Profiles/
// Settings ‖ Tasks), so int(Page) doubles as the nav index. Owned by
// render::Renderer (see renderer.h's cjk_font_ for the same
// member-held/render-thread-exclusive/no-lock pattern) and threaded through
// ui::BuildFrame each frame.
enum class Page : int { Home, Download, Profiles, Settings, Tasks, Count };

// Sub-navigation within the Download page's master-detail layout (left rail
// selection). Render-thread-exclusive, same reasoning as Page.
enum class DownloadSection : int { Games, Mods, Count };

// Sub-navigation within the Settings page's master-detail layout (left rail
// selection). Render-thread-exclusive, same reasoning as Page.
enum class SettingsSection : int { Appearance, Game, Java, About, Count };

struct AppState {
    Page current_page = Page::Home;
    DownloadSection download_section = DownloadSection::Games;
    SettingsSection settings_section = SettingsSection::Appearance;

    // Real-bold font for the nav buttons (embedded Roboto-Bold + a
    // runtime-discovered bold CJK system face), set once by the renderer
    // after the font atlas is built. Null means no real bold face could be
    // built; frame.cpp falls back to faux-bold (ButtonOptions::bold) in that
    // case. Render-thread-exclusive, same reasoning as current_page.
    ImFont* nav_bold_font = nullptr;
};

} // namespace ui
