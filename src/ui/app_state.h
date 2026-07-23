#pragma once

#include "net/msa.h"

#include <string>

struct ImFont;

namespace ui {

// Render-thread-only navigation state: which page the bottom nav bar has
// selected. Order matches the nav button order (Home/Download/Settings ‖
// Tasks), so int(Page) doubles as the nav index. Owned by render::Renderer (see
// renderer.h's cjk_font_ for the same member-held/render-thread-exclusive/
// no-lock pattern) and threaded through ui::BuildFrame each frame. Account
// management is no longer a top-level page -- it lives as a Home sub-view (see
// accounts_subpage below), reached from the Home action pane's account card.
enum class Page : int { Home, Download, Settings, Tasks, Count };

// Sub-navigation within the Download page's master-detail layout (left rail
// selection). Render-thread-exclusive, same reasoning as Page.
enum class DownloadSection : int { Games, Mods, Count };

// Sub-navigation within the Settings page's master-detail layout (left rail
// selection). Render-thread-exclusive, same reasoning as Page. Rail order ==
// declaration order (see frame.cpp's kKeys), with Download just above About.
enum class SettingsSection : int { Appearance, Game, Java, Download, About, Count };

struct AppState {
    Page current_page = Page::Home;
    DownloadSection download_section = DownloadSection::Games;
    SettingsSection settings_section = SettingsSection::Appearance;

    // Home page: index of the selected instance in the scanned instance list
    // (net::ScanInstances order). Clamped against the live list each frame, so
    // it stays valid across instances being added/removed. Render-thread-
    // exclusive, same reasoning as current_page.
    int home_selected = 0;

    // When non-empty, the Home page shows the per-instance settings view for
    // this instance name (instead of the instance-list/launch view); a Back
    // button clears it. Render-thread-exclusive, same reasoning as above.
    std::string instance_settings_name;

    // When true, the Home page shows the account-management sub-view (the former
    // Profiles page: account list + add Microsoft/offline), reached from the
    // account card in the Home action pane; a Back button clears it. Same
    // Back-to-launch-view pattern as instance_settings_name above.
    bool accounts_subpage = false;

    // Profiles page new-offline-account dialog: the editable username buffer,
    // and a deferred-open flag consumed at the root window scope (the "add"
    // button lives inside the account-list card's child window, so OpenDialog
    // has to be deferred out of it -- same reasoning as
    // open_new_instance_request below).
    char account_name_buf[64] = "";
    bool open_new_account_request = false;

    // Microsoft device-code login. The driver owns a background thread and
    // publishes progress; the render thread polls it each frame in the
    // ##ms_login dialog (frame.cpp) and commits the account on success.
    // open_ms_login_request defers OpenDialog out of the account card's child
    // window, same reasoning as open_new_account_request above.
    net::MsaLogin msa_login;
    bool open_ms_login_request = false;

    // Real-bold font for the nav buttons (embedded Roboto-Bold + a
    // runtime-discovered bold CJK system face), set once by the renderer
    // after the font atlas is built. Null means no real bold face could be
    // built; frame.cpp falls back to faux-bold (ButtonOptions::bold) in that
    // case. Render-thread-exclusive, same reasoning as current_page.
    ImFont* nav_bold_font = nullptr;

    // Download page (Games branch, frame.cpp) state -- all render-thread-
    // exclusive, same reasoning as current_page.

    // Version-type filter checkboxes above the version list. Only release is
    // on by default; snapshot/old_beta/old_alpha are off (matches most
    // launchers' default view -- non-release versions are a deliberate opt-in).
    bool filter_release = true;
    bool filter_snapshot = false;
    bool filter_old_beta = false;
    bool filter_old_alpha = false;

    // Set the first time the Download page is shown, to auto-trigger one
    // manifest refresh so the version list isn't empty until the user clicks
    // Refresh. Render-thread-exclusive, same reasoning as current_page.
    bool download_auto_refreshed = false;

    // New-instance dialog: which version id it's showing (set when a version
    // row is clicked), and the editable instance-name buffer (TextField needs
    // a persistent buffer to edit in place -- primed from dialog_version_id
    // when the row is clicked).
    std::string dialog_version_id;
    char instance_name_buf[128] = "";
    // Set when a version row is clicked; consumed at the root window scope to
    // call ImGuiMD2::OpenDialog there. The click happens inside a nested child
    // window (the scrollable version list), but ImGui keys a popup to the ID
    // stack at OpenPopup time -- so OpenDialog must run in the SAME scope as
    // the matching BeginDialog (the root window), or the ids never match and
    // the dialog never opens. This flag defers the open across that boundary.
    bool open_new_instance_request = false;

    // Root-level snackbar (rendered once at the end of BuildFrame,
    // independent of the current page) confirming a task was enqueued.
    // Snackbar() owns its own timeout countdown once snackbar_open is set.
    bool snackbar_open = false;
    char snackbar_msg[160] = "";
};

} // namespace ui
