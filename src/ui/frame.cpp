#include "ui/frame.h"

#include "config/config.h"
#include "net/download_manager.h"
#include "net/launcher.h"
#include "net/manifest.h"
#include "platform/game_monitor.h"
#include "platform/process.h"
#include "platform/window.h"
#include "ui/account_settings.h"
#include "ui/app_state.h"
#include "ui/download_settings.h"
#include "ui/game_settings.h"
#include "ui/i18n.h"
#include "ui/instance_settings.h"
#include "ui/java_settings.h"
#include "ui/theme.h"

#include <imgui.h>
#include <imgui_md2/imgui_md2.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ui {
namespace {

constexpr float kTitlePadding = 24.0f;
constexpr float kContentPadding = 24.0f;

// Default Azure AD application id for Microsoft login (PrismLauncher's public
// client, which has the device-code/public-client flow enabled). Overridable
// via the auth.client_id config key -- register your own Azure app to avoid
// depending on a borrowed client id.
constexpr const char* kDefaultMsaClientId = "508f8b36-3caa-44cd-be00-3ba2967c541d";
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

// Material Icons "play_arrow"/"pause" glyphs (U+E037/U+E034) for the Tasks
// page's per-row resume/pause action. Same situation as kMinimizeIcon above:
// no ImGuiMD2::Icons:: constant exists for either.
constexpr const char* kPlayIcon = "\xee\x80\xb7";
constexpr const char* kPauseIcon = "\xee\x80\xb4";

// Resolves the download.mirror config string ("auto"/"official"/"bmclapi",
// defaulting to "auto") to its net::MirrorMode -- shared by the Download
// page's refresh button and the new-instance dialog's confirm handler so
// both always agree with what Settings > Download currently has selected.
net::MirrorMode MirrorModeFromConfig(config::Config& cfg) {
    const std::string mode = cfg.GetString("download.mirror", "auto");
    if (mode == "official") return net::MirrorMode::OfficialOnly;
    if (mode == "bmclapi") return net::MirrorMode::BmclapiOnly;
    return net::MirrorMode::Auto;
}

// Tasks page label lookups -- net::TaskInfo::Phase/Status to their i18n key.
const char* PhaseLabel(net::TaskInfo::Phase phase) {
    switch (phase) {
        case net::TaskInfo::Phase::Json: return ui::i18n::Tr("tasks.phase.json");
        case net::TaskInfo::Phase::Client: return ui::i18n::Tr("tasks.phase.client");
        case net::TaskInfo::Phase::Libraries: return ui::i18n::Tr("tasks.phase.libraries");
        case net::TaskInfo::Phase::Assets: return ui::i18n::Tr("tasks.phase.assets");
        case net::TaskInfo::Phase::Done: return ui::i18n::Tr("tasks.phase.done");
    }
    return "";
}
const char* StatusLabel(net::TaskInfo::Status status) {
    switch (status) {
        case net::TaskInfo::Status::Queued: return ui::i18n::Tr("tasks.status.queued");
        case net::TaskInfo::Status::Running: return ui::i18n::Tr("tasks.status.downloading");
        case net::TaskInfo::Status::Paused: return ui::i18n::Tr("tasks.status.paused");
        case net::TaskInfo::Status::Done: return ui::i18n::Tr("tasks.status.done");
        case net::TaskInfo::Status::Error: return ui::i18n::Tr("tasks.status.error");
    }
    return "";
}

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
// identity to preserve across a language switch. The rail always renders as
// an elevated card (BeginCard/EndCard: shadow + rounding + surface fill +
// 16dp inner padding); the detail side does too by default (`detail_card =
// true`), anchored at `origin` and filling `size` so they align to the same
// edges as the surrounding content region.
//
// Pass `detail_card = false` when `draw_detail` builds its own elevated
// card(s) (e.g. Java settings' separate installs/JVM cards): imgui_md2's
// SurfaceForElevation() returns the same flat white for any elevation in
// light mode (imgui_md2/src/components.cpp), so a card nested inside this
// card-in-card would be visually indistinguishable from its container except
// for a faint shadow line. With `detail_card = false`, the detail region is
// a plain (background-colored, not surface-colored) scrollable area instead,
// so a caller's own cards float against the page background and actually
// read as separate cards.
template <typename DrawDetail>
void MasterDetail(const char* const* keys, int count, int& current, ImVec2 origin,
                   ImVec2 size, DrawDetail&& draw_detail, bool detail_card = true) {
    ImGui::SetCursorScreenPos(origin);
    ImGuiMD2::BeginCard("##rail", ImVec2(kRailWidth, size.y), 2);
    for (int i = 0; i < count; ++i) {
        if (ImGuiMD2::ListItem(ui::i18n::Tr(keys[i]), nullptr, nullptr, nullptr, i == current)) {
            current = i;
        }
    }
    ImGuiMD2::EndCard();
    ImGui::SameLine(0.0f, kCardGap);

    const ImVec2 detail_size(size.x - kRailWidth - kCardGap, size.y);
    if (detail_card) {
        ImGuiMD2::BeginCard("##detail", detail_size, 2);
        // BeginCard's child window disables scrolling (fine for the short
        // static pages it was designed for), but a detail page's content can
        // grow taller than the fixed card height. Nest a plain scrollable
        // child so overflow is reachable instead of silently clipped,
        // without changing BeginCard's behavior for every other card.
        ImGui::BeginChild("##detail_scroll", ImVec2(0.0f, 0.0f));
        draw_detail();
        ImGui::EndChild();
        ImGuiMD2::EndCard();
    } else {
        const ImGuiMD2::Theme& theme = ImGuiMD2::GetTheme();
        // ChildBg's global default is theme.colors.surface (see
        // imgui_md2/src/theme.cpp's ApplyTheme -- every plain child is
        // surface-colored, matching cards, unless overridden), so this needs
        // an explicit override to the page's own background tone.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.colors.background.Vec4());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(ImGuiMD2::Metrics::CardPadding(), ImGuiMD2::Metrics::CardPadding()));
        ImGui::BeginChild("##detail_plain", detail_size, ImGuiChildFlags_AlwaysUseWindowPadding);
        draw_detail();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
}

} // namespace

void BuildFrame(platform::Window& window, const platform::GameMonitorState& game_state,
                ui::AppState& app_state, net::DownloadManager& downloads,
                platform::GameMonitor& monitor) {
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
            // Per-instance settings sub-view: shown in place of the launch
            // view while an instance's settings are being edited (opened from
            // the action pane's "settings" button, dismissed with Back).
            if (!app_state.instance_settings_name.empty()) {
                const ImGuiMD2::Theme& theme = ImGuiMD2::GetTheme();
                ImGui::SetCursorScreenPos(md_min);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.colors.background.Vec4());
                ImGui::PushStyleVar(
                    ImGuiStyleVar_WindowPadding,
                    ImVec2(ImGuiMD2::Metrics::CardPadding(), ImGuiMD2::Metrics::CardPadding()));
                ImGui::BeginChild("##instance_settings_view", md_size,
                                  ImGuiChildFlags_AlwaysUseWindowPadding);
                if (ImGuiMD2::OutlinedButton(
                        ui::i18n::TrLabel("instance.back", "instance_back").c_str())) {
                    app_state.instance_settings_name.clear();
                }
                ImGui::SameLine();
                // Center the title against the Back button's height.
                ImGui::AlignTextToFramePadding();
                ImGuiMD2::Text(ImGuiMD2::TextStyle::Headline6,
                               app_state.instance_settings_name.c_str());
                ImGui::Dummy(ImVec2(0.0f, 12.0f));
                ui::BuildInstanceSettings(app_state.instance_settings_name);
                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
                break;
            }

            // Account-management sub-view: the former Profiles page (account
            // list + add Microsoft/offline), shown in place of the launch view
            // and dismissed with Back. Same child-window shell as the instance
            // settings sub-view above; reached from the account card in the
            // right-hand action pane below.
            if (app_state.accounts_subpage) {
                const ImGuiMD2::Theme& theme = ImGuiMD2::GetTheme();
                ImGui::SetCursorScreenPos(md_min);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.colors.background.Vec4());
                ImGui::PushStyleVar(
                    ImGuiStyleVar_WindowPadding,
                    ImVec2(ImGuiMD2::Metrics::CardPadding(), ImGuiMD2::Metrics::CardPadding()));
                ImGui::BeginChild("##accounts_view", md_size, ImGuiChildFlags_AlwaysUseWindowPadding);
                if (ImGuiMD2::OutlinedButton(
                        ui::i18n::TrLabel("instance.back", "accounts_back").c_str())) {
                    app_state.accounts_subpage = false;
                }
                // Title vertically centered against the Back button and faux-
                // bolded (double-drawn 1px apart): there is no real bold
                // Headline6 face, so this matches the faux-bold fallback used
                // elsewhere for CJK rather than swapping to a lighter weight.
                // PushFont makes the 3-arg AddText / CalcTextSize use the
                // Headline6 face at its LegacySize (no removed FontSize field).
                const float back_top = ImGui::GetItemRectMin().y;
                const float back_h = ImGui::GetItemRectSize().y;
                ImGui::SameLine();
                const ImVec2 title_pos = ImGui::GetCursorScreenPos();
                const char* title = ui::i18n::Tr("nav.profiles");
                ImFont* h6 = theme.fonts.Get(ImGuiMD2::TextStyle::Headline6);
                if (h6 != nullptr) ImGui::PushFont(h6);
                const float title_y = back_top + (back_h - ImGui::GetFontSize()) * 0.5f;
                const ImU32 title_col = theme.colors.on_surface.U32();
                ImDrawList* title_dl = ImGui::GetWindowDrawList();
                title_dl->AddText(ImVec2(title_pos.x, title_y), title_col, title);
                title_dl->AddText(ImVec2(title_pos.x + 1.0f, title_y), title_col, title);
                const ImVec2 title_sz = ImGui::CalcTextSize(title);
                if (h6 != nullptr) ImGui::PopFont();
                ImGui::Dummy(ImVec2(title_sz.x + 1.0f, back_h));
                ImGui::Dummy(ImVec2(0.0f, 12.0f));
                ui::BuildAccountSettings(app_state);
                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();

                // Dialogs at page scope (outside the child window) so OpenDialog's
                // popup id matches BeginDialog -- same cross-child deferral as the
                // download page's new-instance dialog.
                if (app_state.open_new_account_request) {
                    app_state.account_name_buf[0] = '\0';
                    ImGuiMD2::OpenDialog("##new_account");
                    app_state.open_new_account_request = false;
                }
                if (ImGuiMD2::BeginDialog("##new_account")) {
                    ImGuiMD2::TextH6(ui::i18n::Tr("profiles.dialog.title"));

                    ImGuiMD2::TextFieldOptions name_options;
                    name_options.variant = ImGuiMD2::TextFieldVariant::Outlined;
                    ImGuiMD2::TextField(ui::i18n::Tr("profiles.dialog.name"),
                                        app_state.account_name_buf,
                                        sizeof(app_state.account_name_buf), name_options);

                    if (ImGuiMD2::ContainedButton(
                            ui::i18n::TrLabel("action.confirm", "new_account_confirm").c_str())) {
                        if (app_state.account_name_buf[0] != '\0') {
                            ui::CreateOfflineAccount(app_state.account_name_buf);
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGuiMD2::TextButton(
                            ui::i18n::TrLabel("action.cancel", "new_account_cancel").c_str())) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGuiMD2::EndDialog();
                }

                // Microsoft device-code login dialog. Starting the login spins up
                // a background thread (net::MsaLogin); this dialog polls its state
                // each frame and, on success, commits the account through config
                // on the render thread (the worker never touches config).
                if (app_state.open_ms_login_request) {
                    const std::string client_id = config::Config::Instance().GetString(
                        "auth.client_id", kDefaultMsaClientId);
                    app_state.msa_login.Start(client_id);
                    ImGuiMD2::OpenDialog("##ms_login");
                    app_state.open_ms_login_request = false;
                }
                if (ImGuiMD2::BeginDialog("##ms_login")) {
                    const net::LoginState st = app_state.msa_login.state();
                    ImGuiMD2::TextH6(ui::i18n::Tr("profiles.msa.title"));
                    ImGui::Dummy(ImVec2(0.0f, 8.0f));

                    switch (st.phase) {
                        case net::LoginState::Phase::Requesting:
                            ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2,
                                           ui::i18n::Tr("profiles.msa.requesting"));
                            break;
                        case net::LoginState::Phase::AwaitingUser:
                            ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2,
                                           ui::i18n::Tr("profiles.msa.instructions"));
                            ImGuiMD2::Text(ImGuiMD2::TextStyle::Body1,
                                           st.verification_uri.c_str());
                            ImGui::Dummy(ImVec2(0.0f, 8.0f));
                            ImGuiMD2::TextH5(st.user_code.c_str());
                            ImGui::Dummy(ImVec2(0.0f, 8.0f));
                            if (ImGuiMD2::OutlinedButton(ui::i18n::Tr("profiles.msa.copy_code"))) {
                                ImGui::SetClipboardText(st.user_code.c_str());
                            }
                            ImGui::SameLine();
                            if (ImGuiMD2::OutlinedButton(ui::i18n::Tr("profiles.msa.copy_link"))) {
                                ImGui::SetClipboardText(st.verification_uri.c_str());
                            }
                            break;
                        case net::LoginState::Phase::Authenticating:
                            ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2,
                                           ui::i18n::Tr("profiles.msa.authenticating"));
                            break;
                        case net::LoginState::Phase::Success:
                            ui::CommitMicrosoftAccount(st.name, st.uuid, st.access_token,
                                                       st.refresh_token);
                            app_state.msa_login.Reset();
                            std::snprintf(app_state.snackbar_msg, sizeof(app_state.snackbar_msg),
                                          ui::i18n::Tr("profiles.msa.logged_in"), st.name.c_str());
                            app_state.snackbar_open = true;
                            ImGui::CloseCurrentPopup();
                            break;
                        case net::LoginState::Phase::Error:
                            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 380.0f);
                            ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2, st.error.c_str());
                            ImGui::PopTextWrapPos();
                            break;
                        default:
                            ImGui::CloseCurrentPopup();
                            break;
                    }

                    ImGui::Dummy(ImVec2(0.0f, 12.0f));
                    if (st.phase == net::LoginState::Phase::Error) {
                        if (ImGuiMD2::ContainedButton(
                                ui::i18n::TrLabel("action.close", "ms_login_close").c_str())) {
                            app_state.msa_login.Reset();
                            ImGui::CloseCurrentPopup();
                        }
                    } else if (st.phase != net::LoginState::Phase::Success) {
                        // Cancel joins the worker (may briefly block if a request
                        // is in flight; the common cancel -- during the poll wait
                        // -- returns within ~100ms).
                        if (ImGuiMD2::TextButton(
                                ui::i18n::TrLabel("action.cancel", "ms_login_cancel").c_str())) {
                            app_state.msa_login.Cancel();
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGuiMD2::EndDialog();
                }
                break;
            }

            // Two-pane launch home: a wide instance list on the left (scanned
            // from the selected game directory's versions/), and a narrower
            // action pane on the right with the Start button pinned to its
            // bottom. Not MasterDetail() -- that's a fixed narrow-rail layout;
            // this is the inverse (wide list + slim action column).
            const std::string game_dir = ui::SelectedGameDir();
            const std::vector<net::InstalledInstance> instances = net::ScanInstances(game_dir);

            if (instances.empty()) {
                app_state.home_selected = 0;
            } else {
                app_state.home_selected =
                    std::clamp(app_state.home_selected, 0, static_cast<int>(instances.size()) - 1);
            }
            const net::InstalledInstance* selected_instance =
                instances.empty() ? nullptr
                                   : &instances[static_cast<std::size_t>(app_state.home_selected)];

            // Which instances are currently running, per the game-monitor thread's
            // process monitor (game_state.running_games, refreshed each tick).
            const auto is_running = [&](const std::string& name) {
                return std::find(game_state.running_games.begin(),
                                 game_state.running_games.end(),
                                 name) != game_state.running_games.end();
            };

            constexpr float kListFraction = 0.62f;
            const float left_w = (md_size.x - kCardGap) * kListFraction;
            const float right_w = (md_size.x - kCardGap) - left_w;

            // Left: instance list card.
            ImGui::SetCursorScreenPos(md_min);
            ImGuiMD2::BeginCard("##home_instances", ImVec2(left_w, md_size.y), 2);
            char inst_header[96];
            std::snprintf(inst_header, sizeof(inst_header), ui::i18n::Tr("home.instances_header"),
                          static_cast<int>(instances.size()));
            ImGuiMD2::Text(ImGuiMD2::TextStyle::Subtitle1, inst_header);
            ImGui::BeginChild("##home_instance_list", ImVec2(0.0f, 0.0f));
            if (instances.empty()) {
                ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2, ui::i18n::Tr("home.empty"));
            } else {
                for (std::size_t i = 0; i < instances.size(); ++i) {
                    const bool is_selected = (static_cast<int>(i) == app_state.home_selected);
                    const char* trailing =
                        is_running(instances[i].name) ? ui::i18n::Tr("home.running") : nullptr;
                    if (ImGuiMD2::ListItem(instances[i].name.c_str(),
                                           instances[i].version_id.c_str(), nullptr, trailing,
                                           is_selected)) {
                        app_state.home_selected = static_cast<int>(i);
                    }
                }
            }
            ImGui::EndChild();
            ImGuiMD2::EndCard();

            // Right column, split top/bottom: an account summary card above,
            // the action pane (instance info + settings + Start) below. Both are
            // positioned explicitly (not SameLine) so they can stack.
            const float right_x = md_min.x + left_w + kCardGap;
            const float account_card_h =
                2.0f * ImGuiMD2::Metrics::CardPadding() + ImGuiMD2::Metrics::ListRowHeightTwoLine();

            // Top: current-account card -- one clickable row (username + account
            // type, or a "not signed in" prompt) that opens the account-
            // management sub-view (the former Profiles page).
            ImGui::SetCursorScreenPos(ImVec2(right_x, md_min.y));
            ImGuiMD2::BeginCard("##home_account", ImVec2(right_w, account_card_h), 2);
            {
                const std::string acct_name = ui::SelectedAccountName();
                const bool has_account = !acct_name.empty();
                const char* acct_title =
                    has_account ? acct_name.c_str() : ui::i18n::Tr("home.no_account");
                const char* acct_sub =
                    has_account ? (ui::SelectedAccountType() == "msa"
                                       ? ui::i18n::Tr("profiles.account.microsoft")
                                       : ui::i18n::Tr("profiles.account.offline"))
                                : ui::i18n::Tr("home.tap_to_add");
                if (ImGuiMD2::ListItem(acct_title, acct_sub, nullptr, nullptr, false)) {
                    app_state.accounts_subpage = true;
                }
            }
            ImGuiMD2::EndCard();

            // Bottom: action pane. Selected-instance info at the top, Start
            // button pinned to the bottom.
            ImGui::SetCursorScreenPos(ImVec2(right_x, md_min.y + account_card_h + kCardGap));
            ImGuiMD2::BeginCard("##home_actions",
                                ImVec2(right_w, md_size.y - account_card_h - kCardGap), 2);
            if (selected_instance != nullptr) {
                ImGuiMD2::Text(ImGuiMD2::TextStyle::Subtitle1, selected_instance->name.c_str());
                ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2, selected_instance->version_id.c_str());
            }
            // Bottom-pinned action cluster: "Instance Settings" (when an
            // instance is selected) stacked directly above "Start Game".
            const float action_button_h = ImGuiMD2::Metrics::TouchTarget();
            const float action_gap = ImGui::GetStyle().ItemSpacing.y;
            const float cluster_h = selected_instance != nullptr
                                        ? (2.0f * action_button_h + action_gap)
                                        : action_button_h;
            const float pad_to_bottom = ImGui::GetContentRegionAvail().y - cluster_h;
            if (pad_to_bottom > 0.0f) {
                ImGui::Dummy(ImVec2(0.0f, pad_to_bottom));
            }
            if (selected_instance != nullptr &&
                ImGuiMD2::OutlinedButton(
                    ui::i18n::TrLabel("instance.settings", "instance_settings").c_str(),
                    ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
                app_state.instance_settings_name = selected_instance->name;
            }
            // Enabled whenever an instance is selected and not already running;
            // missing Java/account is reported via the snackbar on click
            // (rather than a silent disable) so the reason is visible. A running
            // instance shows "Running" and is disabled (relaunching the same
            // isolated instance would collide on its game directory).
            const bool selected_running =
                selected_instance != nullptr && is_running(selected_instance->name);
            ImGui::BeginDisabled(selected_instance == nullptr || selected_running);
            if (ImGuiMD2::ContainedButton(
                    ui::i18n::TrLabel(selected_running ? "home.running" : "home.start", "home_start")
                        .c_str(),
                    ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)) &&
                selected_instance != nullptr) {
                config::Config& cfg = config::Config::Instance();
                const std::string java = ui::InstanceJavaPath(selected_instance->name);
                const std::string player = ui::SelectedAccountName();
                if (java.empty()) {
                    std::snprintf(app_state.snackbar_msg, sizeof(app_state.snackbar_msg), "%s",
                                  ui::i18n::Tr("home.launch.no_java"));
                    app_state.snackbar_open = true;
                } else if (player.empty()) {
                    std::snprintf(app_state.snackbar_msg, sizeof(app_state.snackbar_msg), "%s",
                                  ui::i18n::Tr("home.launch.no_account"));
                    app_state.snackbar_open = true;
                } else {
                    net::LaunchParams lp;
                    lp.game_dir = game_dir;
                    lp.instance_name = selected_instance->name;
                    lp.java_exe = java;
                    lp.memory_mb =
                        static_cast<int>(ui::InstanceMemoryMb(selected_instance->name));
                    lp.jvm_args = ui::InstanceJvmArgs(selected_instance->name);
                    lp.player_name = player;
                    lp.player_uuid = ui::SelectedAccountUuid();
                    lp.access_token = ui::SelectedAccountToken();
                    lp.user_type = ui::SelectedAccountType();
                    lp.isolate_instance = cfg.GetBool("game.isolation", true);
                    lp.width = static_cast<int>(cfg.GetInt("game.res.width", 0));
                    lp.height = static_cast<int>(cfg.GetInt("game.res.height", 0));
                    lp.fullscreen = cfg.GetBool("game.fullscreen", false);
                    lp.extra_game_args = cfg.GetString("game.args", "");

                    const net::LaunchCommand lc = net::PrepareLaunch(lp);
                    const platform::ProcessHandle proc =
                        lc.error.empty() ? platform::LaunchProcess(lc.exe, lc.args, lc.cwd)
                                         : platform::ProcessHandle{0};
                    if (!lc.error.empty()) {
                        std::snprintf(app_state.snackbar_msg, sizeof(app_state.snackbar_msg),
                                      ui::i18n::Tr("home.launch.failed"), lc.error.c_str());
                    } else if (proc != 0) {
                        // Hand the live process to the game-monitor thread;
                        // it drives the "running" state the list shows below.
                        monitor.TrackGame(lp.instance_name, proc);
                        std::snprintf(app_state.snackbar_msg, sizeof(app_state.snackbar_msg),
                                      ui::i18n::Tr("home.launch.started"), lp.instance_name.c_str());
                    } else {
                        std::snprintf(app_state.snackbar_msg, sizeof(app_state.snackbar_msg),
                                      ui::i18n::Tr("home.launch.failed"), "spawn failed");
                    }
                    app_state.snackbar_open = true;
                }
            }
            ImGui::EndDisabled();
            ImGuiMD2::EndCard();
            break;
        }
        case Page::Download: {
            // First time this page is shown, kick off one manifest refresh so
            // the version list is populated without the user having to click
            // Refresh. RefreshManifest is a no-op while one is already in
            // flight, so this is safe even if it races the button.
            if (!app_state.download_auto_refreshed) {
                downloads.RefreshManifest(
                    MirrorModeFromConfig(config::Config::Instance()));
                app_state.download_auto_refreshed = true;
            }

            static const char* const kKeys[] = {"download.games", "download.mods"};
            int current = static_cast<int>(app_state.download_section);
            MasterDetail(kKeys, 2, current, md_min, md_size, [&]() {
                switch (app_state.download_section) {
                    case DownloadSection::Games: {
                        ImGuiMD2::Text(ImGuiMD2::TextStyle::Headline6, ui::i18n::Tr("download.games"));

                        config::Config& cfg = config::Config::Instance();

                        // Refresh (left) and the version-type filters (right)
                        // share one row. The checkboxes are TouchTarget (48dp)
                        // tall, the button is shorter. To keep the four boxes
                        // perfectly aligned with each other AND vertically
                        // center the button, the button and the checkbox group
                        // are drawn as two independent lines (both anchored to
                        // row_top_y) rather than chained with SameLine -- a
                        // SameLine chain would leak the button's centering
                        // offset into the first checkbox and stagger the row.
                        const float row_top_y = ImGui::GetCursorPosY();
                        const float row_left_x = ImGui::GetCursorPosX();
                        const float row_h = ImGuiMD2::Metrics::TouchTarget();
                        const float btn_h = ImGuiMD2::Metrics::ButtonMinHeight();
                        const float spacing_x = ImGui::GetStyle().ItemSpacing.x;

                        // Predict the checkbox group's width up front (each box
                        // is TouchTarget + its label measured in the Body1 font,
                        // matching Checkbox's own impl in components.cpp) so the
                        // group can be right-aligned.
                        const char* const type_labels[] = {
                            ui::i18n::Tr("download.type.release"),
                            ui::i18n::Tr("download.type.snapshot"),
                            ui::i18n::Tr("download.type.old_beta"),
                            ui::i18n::Tr("download.type.old_alpha")};
                        bool* const type_flags[] = {
                            &app_state.filter_release, &app_state.filter_snapshot,
                            &app_state.filter_old_beta, &app_state.filter_old_alpha};

                        ImFont* body1_font = ImGuiMD2::GetTheme().fonts.Get(ImGuiMD2::TextStyle::Body1);
                        float group_w = 3.0f * spacing_x; // 3 gaps between the 4 boxes
                        if (body1_font) ImGui::PushFont(body1_font);
                        for (const char* label : type_labels) {
                            group_w += ImGuiMD2::Metrics::TouchTarget() + ImGui::CalcTextSize(label).x;
                        }
                        if (body1_font) ImGui::PopFont();

                        // Refresh button: on the left, vertically centered
                        // within the taller checkbox row.
                        ImGui::SetCursorPos(ImVec2(row_left_x, row_top_y + (row_h - btn_h) * 0.5f));
                        if (ImGuiMD2::ContainedButton(
                                ui::i18n::TrLabel("download.refresh", "download_refresh").c_str())) {
                            downloads.RefreshManifest(MirrorModeFromConfig(cfg));
                        }
                        const float button_w = ImGui::GetItemRectSize().x;

                        // Checkbox group: its own line at row_top_y so all four
                        // stay aligned. Right-aligned to the version list's right
                        // edge (the list child reserves a scrollbar, so its rows
                        // end one ScrollbarSize short of the content edge -- match
                        // that inset), clamped so it never overlaps the button.
                        const float right_edge =
                            ImGui::GetContentRegionMax().x - ImGui::GetStyle().ScrollbarSize;
                        const float group_x =
                            std::max(row_left_x + button_w + spacing_x, right_edge - group_w);
                        ImGui::SetCursorPos(ImVec2(group_x, row_top_y));
                        for (int i = 0; i < 4; ++i) {
                            if (i > 0) ImGui::SameLine();
                            ImGuiMD2::Checkbox(type_labels[i], type_flags[i]);
                        }

                        // Continue below the whole row (the checkbox line is the
                        // taller of the two) with the normal inter-row gap.
                        ImGui::SetCursorPos(ImVec2(row_left_x, row_top_y + row_h));
                        ImGui::Dummy(ImVec2(0.0f, 8.0f));

                        const net::ManifestSnapshot snapshot = downloads.manifest();
                        ImGui::BeginChild("##version_list", ImVec2(0.0f, 0.0f));
                        switch (snapshot.status) {
                            case net::ManifestSnapshot::Status::Idle:
                                break;
                            case net::ManifestSnapshot::Status::Loading:
                                ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2, ui::i18n::Tr("download.loading"));
                                ImGuiMD2::LinearProgressIndeterminate("##manifest_loading");
                                break;
                            case net::ManifestSnapshot::Status::Error:
                                ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2,
                                               ui::i18n::Tr("download.load_failed"));
                                if (!snapshot.error.empty()) {
                                    ImGuiMD2::Text(ImGuiMD2::TextStyle::Caption, snapshot.error.c_str());
                                }
                                break;
                            case net::ManifestSnapshot::Status::Ready:
                                for (const net::VersionEntry& entry : snapshot.versions) {
                                    const bool show =
                                        (entry.type == "release" && app_state.filter_release) ||
                                        (entry.type == "snapshot" && app_state.filter_snapshot) ||
                                        (entry.type == "old_beta" && app_state.filter_old_beta) ||
                                        (entry.type == "old_alpha" && app_state.filter_old_alpha);
                                    if (!show) continue;
                                    if (ImGuiMD2::ListItem(entry.id.c_str(), nullptr, nullptr,
                                                           entry.release_time.c_str())) {
                                        app_state.dialog_version_id = entry.id;
                                        std::snprintf(app_state.instance_name_buf,
                                                      sizeof(app_state.instance_name_buf), "%s",
                                                      entry.id.c_str());
                                        // Defer the actual OpenDialog to the
                                        // root scope below (see the flag's
                                        // comment in app_state.h) -- calling it
                                        // here, inside this child window, keys
                                        // the popup to the wrong ID stack.
                                        app_state.open_new_instance_request = true;
                                    }
                                }
                                break;
                        }
                        ImGui::EndChild();
                        break;
                    }
                    case DownloadSection::Mods:
                        ImGuiMD2::Text(ImGuiMD2::TextStyle::Headline6, ui::i18n::Tr("download.mods"));
                        ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2, ui::i18n::Tr("page.wip"));
                        break;
                    case DownloadSection::Count:
                        break;
                }
            });
            app_state.download_section = static_cast<DownloadSection>(current);

            // Open the dialog here, at the root window scope (NOT inside the
            // version-list child where the row was clicked), so OpenDialog's
            // popup id matches the BeginDialog below -- see the flag's comment
            // in app_state.h.
            if (app_state.open_new_instance_request) {
                ImGuiMD2::OpenDialog("##new_instance");
                app_state.open_new_instance_request = false;
            }

            // New-instance dialog: BeginDialog must be called every frame
            // (it's a thin wrapper over ImGui::BeginPopupModal, which only
            // actually opens once OpenDialog()'s matching ImGui::OpenPopup
            // was called) -- cheap no-op while closed.
            if (ImGuiMD2::BeginDialog("##new_instance")) {
                ImGuiMD2::TextH6(ui::i18n::Tr("download.dialog.title"));

                ImGuiMD2::TextFieldOptions name_options;
                name_options.variant = ImGuiMD2::TextFieldVariant::Outlined;
                ImGuiMD2::TextField(ui::i18n::Tr("download.dialog.name"), app_state.instance_name_buf,
                                    sizeof(app_state.instance_name_buf), name_options);

                const net::ManifestSnapshot snapshot = downloads.manifest();
                const net::VersionEntry* selected_entry = nullptr;
                for (const net::VersionEntry& entry : snapshot.versions) {
                    if (entry.id == app_state.dialog_version_id) {
                        selected_entry = &entry;
                        break;
                    }
                }
                if (selected_entry != nullptr) {
                    char info[256];
                    std::snprintf(info, sizeof(info), "%s: %s   %s: %s   %s: %s",
                                  ui::i18n::Tr("download.dialog.info.id"), selected_entry->id.c_str(),
                                  ui::i18n::Tr("download.dialog.info.type"), selected_entry->type.c_str(),
                                  ui::i18n::Tr("download.dialog.info.time"),
                                  selected_entry->release_time.c_str());
                    ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2, info);
                }

                if (ImGuiMD2::ContainedButton(
                        ui::i18n::TrLabel("action.confirm", "new_instance_confirm").c_str())) {
                    if (selected_entry != nullptr && app_state.instance_name_buf[0] != '\0') {
                        config::Config& cfg = config::Config::Instance();
                        net::InstallParams params;
                        params.game_dir = ui::SelectedGameDir();
                        params.instance_name = app_state.instance_name_buf;
                        params.mirror = MirrorModeFromConfig(cfg);
                        params.concurrency = static_cast<int>(cfg.GetInt("download.concurrency", 8));
                        params.threads_per_file = static_cast<int>(cfg.GetInt("download.threads", 4));
                        downloads.EnqueueInstall(*selected_entry, params);

                        std::snprintf(app_state.snackbar_msg, sizeof(app_state.snackbar_msg),
                                      ui::i18n::Tr("download.task_added"), params.instance_name.c_str());
                        app_state.snackbar_open = true;
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGuiMD2::TextButton(
                        ui::i18n::TrLabel("action.cancel", "new_instance_cancel").c_str())) {
                    ImGui::CloseCurrentPopup();
                }

                ImGuiMD2::EndDialog();
            }
            break;
        }
        case Page::Settings: {
            static const char* const kKeys[] = {"settings.appearance", "settings.game",
                                                "settings.java", "settings.download",
                                                "settings.about"};
            int current = static_cast<int>(app_state.settings_section);
            // Java/Game/Download settings each draw their own separate cards
            // -- see MasterDetail()'s detail_card doc comment for why that
            // needs the plain (uncarded) detail region.
            const bool detail_card = app_state.settings_section != SettingsSection::Java &&
                                     app_state.settings_section != SettingsSection::Game &&
                                     app_state.settings_section != SettingsSection::Download;
            MasterDetail(kKeys, 5, current, md_min, md_size, [&]() {
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
                        ui::BuildGameSettings();
                        break;
                    case SettingsSection::Java:
                        ui::BuildJavaSettings();
                        break;
                    case SettingsSection::About:
                        ImGuiMD2::Text(ImGuiMD2::TextStyle::Headline6, ui::i18n::Tr("settings.about"));
                        ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2, ui::i18n::Tr("settings.project_url"));
                        ImGuiMD2::Text(ImGuiMD2::TextStyle::Body1,
                                       "https://github.com/antinomie1/Pluma");
                        break;
                    case SettingsSection::Download:
                        ui::BuildDownloadSettings();
                        break;
                    case SettingsSection::Count:
                        break;
                }
            }, detail_card);
            app_state.settings_section = static_cast<SettingsSection>(current);
            break;
        }
        case Page::Tasks: {
            ImGuiMD2::Text(ImGuiMD2::TextStyle::Headline6, ui::i18n::Tr("nav.tasks"));

            const std::vector<net::TaskInfo> task_list = downloads.tasks();
            if (task_list.empty()) {
                ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2, ui::i18n::Tr("tasks.empty"));
                break;
            }

            // Fixed-width right-hand column (progress bar, speed text, two
            // icon buttons); the name/status text on the left wraps to
            // whatever's left of the row.
            constexpr float kRightColumnWidth = 220.0f;
            constexpr float kProgressWidth = 140.0f;

            for (const net::TaskInfo& task : task_list) {
                ImGui::PushID(static_cast<int>(task.id));

                const float row_width = ImGui::GetContentRegionAvail().x;
                const float left_width =
                    std::max(80.0f, row_width - kRightColumnWidth - ImGui::GetStyle().ItemSpacing.x);
                const float row_top_y = ImGui::GetCursorPosY();

                ImGui::BeginGroup();
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + left_width);
                ImGuiMD2::Text(ImGuiMD2::TextStyle::Subtitle1, task.name.c_str());
                char status_line[160];
                std::snprintf(status_line, sizeof(status_line), "%s - %s", PhaseLabel(task.phase),
                              StatusLabel(task.status));
                ImGuiMD2::Text(ImGuiMD2::TextStyle::Caption, status_line);
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();

                ImGui::SameLine(left_width + ImGui::GetStyle().ItemSpacing.x);
                ImGui::SetCursorPosY(row_top_y);
                ImGui::BeginGroup();
                const float fraction = task.bytes_total > 0
                                          ? static_cast<float>(task.bytes_done) /
                                                static_cast<float>(task.bytes_total)
                                          : 0.0f;
                ImGuiMD2::LinearProgress(fraction, ImVec2(kProgressWidth, 4.0f));
                char speed_text[64];
                std::snprintf(speed_text, sizeof(speed_text), ui::i18n::Tr("tasks.speed"),
                              task.speed_bps / (1024.0 * 1024.0));
                ImGuiMD2::Text(ImGuiMD2::TextStyle::Caption, speed_text);
                ImGui::EndGroup();

                ImGui::SameLine();
                ImGui::SetCursorPosY(row_top_y);
                const bool is_paused = (task.status == net::TaskInfo::Status::Paused);
                const bool pause_enabled = task.status == net::TaskInfo::Status::Running ||
                                          task.status == net::TaskInfo::Status::Paused;
                if (ImGuiMD2::IconButton("##pause_resume", is_paused ? kPlayIcon : kPauseIcon, false,
                                         pause_enabled)) {
                    if (is_paused) {
                        downloads.Resume(task.id);
                    } else {
                        downloads.Pause(task.id);
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGuiMD2::Tooltip(ui::i18n::Tr(is_paused ? "action.resume" : "action.pause"));
                }
                ImGui::SameLine();
                // Cancel deletes versions/<name>/ -- disabled once a task is
                // Done so a finished install can't be destroyed by accident.
                const bool cancel_enabled = task.status != net::TaskInfo::Status::Done;
                if (ImGuiMD2::IconButton("##cancel", ImGuiMD2::Icons::Close, false, cancel_enabled)) {
                    downloads.Cancel(task.id);
                }
                if (ImGui::IsItemHovered()) {
                    ImGuiMD2::Tooltip(ui::i18n::Tr("action.cancel"));
                }

                ImGui::PopID();
                ImGuiMD2::Divider();
            }
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

    // Left group: Home / Download / Settings, flowing left-to-right. (Account
    // management moved off the nav into a Home sub-view -- see accounts_subpage.)
    ImGui::SetCursorScreenPos(ImVec2(win_pos.x + kNavEdgeGap, nav_btn_y));
    if (NavButton(Page::Home, "nav.home", "nav_home")) {
        app_state.current_page = Page::Home;
    }
    ImGui::SameLine(0.0f, kNavItemGap);
    if (NavButton(Page::Download, "nav.download", "nav_download")) {
        app_state.current_page = Page::Download;
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

    // Root-level snackbar (confirms a download task was enqueued): rendered
    // once here regardless of the current page, since the dialog that
    // triggers it (the Download page's new-instance dialog) closes itself
    // immediately on confirm. Anchored to the center content region (md rect,
    // computed above) so it sits inside the content area, above the bottom nav
    // bar, rather than overlapping it at the viewport's edge.
    const ImVec2 md_max(md_min.x + md_size.x, md_min.y + md_size.y);
    ImGuiMD2::Snackbar("##task_snackbar", app_state.snackbar_msg, &app_state.snackbar_open,
                       nullptr, 4.0f, &md_min, &md_max);

    ImGui::End();
}

} // namespace ui
