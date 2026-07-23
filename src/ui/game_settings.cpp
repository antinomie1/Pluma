#include "ui/game_settings.h"

#include "config/config.h"
#include "platform/file_dialog.h"
#include "ui/i18n.h"

#include <imgui.h>
#include <imgui_md2/imgui_md2.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace ui {
namespace {

constexpr int64_t kDefaultSelected = 0;

// Extra breathing room between the directory list and the "Add Directory"
// button (on top of the ambient row gap), so the button doesn't crowd the
// last list row. Folded into list_card_height below so the fixed-size card
// still fits it.
constexpr float kListButtonGap = 12.0f;

// Persists `dirs` and saves immediately -- matches java_settings.cpp's
// SaveJavaPaths / the Appearance section's "write on change" convention.
void SaveGameDirs(const std::vector<std::string>& dirs) {
    config::Config& cfg = config::Config::Instance();
    cfg.Set("game.dirs.count", static_cast<int64_t>(dirs.size()));
    for (std::size_t i = 0; i < dirs.size(); ++i) {
        char key[64];
        std::snprintf(key, sizeof(key), "game.dirs.%d", static_cast<int>(i));
        cfg.Set(key, dirs[i]);
    }
    cfg.Save();
}

// Translates the flat game.dirs.count / game.dirs.<i> keys into a vector --
// config::Config itself has no array support, see java_settings.cpp's
// LoadJavaPaths for the original of this pattern.
std::vector<std::string> LoadGameDirs() {
    config::Config& cfg = config::Config::Instance();
    const int64_t count = std::max<int64_t>(cfg.GetInt("game.dirs.count", 0), 0);

    std::vector<std::string> dirs;
    dirs.reserve(static_cast<std::size_t>(count));
    for (int64_t i = 0; i < count; ++i) {
        char key[64];
        std::snprintf(key, sizeof(key), "game.dirs.%d", static_cast<int>(i));
        dirs.push_back(cfg.GetString(key, ""));
    }
    return dirs;
}

} // namespace

void EnsureGameDirSeeded() {
    config::Config& cfg = config::Config::Instance();
    if (cfg.GetInt("game.dirs.count", 0) > 0) {
        return; // already configured
    }
    const std::string default_dir = (std::filesystem::current_path() / ".minecraft").string();
    SaveGameDirs({default_dir});
    cfg.Set("game.dir.selected", kDefaultSelected);
    cfg.Save();
}

std::string SelectedGameDir() {
    const std::vector<std::string> dirs = LoadGameDirs();
    if (dirs.empty()) return std::string();
    config::Config& cfg = config::Config::Instance();
    const int64_t selected =
        std::clamp(cfg.GetInt("game.dir.selected", kDefaultSelected), int64_t{0},
                  static_cast<int64_t>(dirs.size()) - 1);
    return dirs[static_cast<std::size_t>(selected)];
}

void BuildGameSettings() {
    config::Config& cfg = config::Config::Instance();

    // Read the live list every frame rather than caching in a stale static,
    // matching java_settings.cpp / the Appearance section's convention.
    std::vector<std::string> dirs = LoadGameDirs();
    bool dirs_changed = false;

    int64_t selected = dirs.empty() ? int64_t{-1}
                                    : std::clamp(cfg.GetInt("game.dir.selected", kDefaultSelected),
                                                int64_t{0}, static_cast<int64_t>(dirs.size()) - 1);
    bool selected_changed = false;

    char header[128];
    std::snprintf(header, sizeof(header), ui::i18n::Tr("settings.game.dirs_header"),
                  static_cast<int>(dirs.size()));

    // Exact (not guessed) card height, following java_settings.cpp's
    // BuildJavaSettings list card -- see that function's comment for why
    // list_expanded has to track the CollapsingHeader's state across frames.
    static bool list_expanded = true;
    const float row_gap = ImGui::GetStyle().ItemSpacing.y;
    const float header_h = ImGui::GetFrameHeight();
    const int content_rows = !list_expanded ? 0 : (dirs.empty() ? 1 : static_cast<int>(dirs.size()));
    const float content_h = !list_expanded ? 0.0f
                           : dirs.empty() ? ImGui::GetTextLineHeight()
                                          : static_cast<float>(dirs.size()) *
                                                ImGuiMD2::Metrics::ListRowHeight();
    const float buttons_h = ImGui::GetFrameHeight();
    const int gap_count = 1 + std::max(0, content_rows - 1) + (list_expanded ? 1 : 0);
    // The extra Dummy before the button (only drawn when expanded) adds its
    // own height plus one more ambient gap ahead of it.
    const float extra_button_gap = list_expanded ? (kListButtonGap + row_gap) : 0.0f;
    const float list_card_height = header_h + content_h + buttons_h +
                                   static_cast<float>(gap_count) * row_gap + extra_button_gap +
                                   2.0f * ImGuiMD2::Metrics::CardPadding();

    ImGuiMD2::BeginCard("##game_dirs_card", ImVec2(0.0f, list_card_height), 2);

    list_expanded = ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen);
    if (list_expanded) {
        if (dirs.empty()) {
            ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2, ui::i18n::Tr("settings.game.empty"));
        } else {
            for (std::size_t i = 0; i < dirs.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                bool remove_clicked = false;
                const bool is_selected = (static_cast<int64_t>(i) == selected);
                // Clicking the row (not its trailing delete icon) selects it
                // as the active game directory; ListItem excludes the icon's
                // hit region from the row's own, so the two never fire on
                // the same click (see components.h's ListItem doc comment).
                if (ImGuiMD2::ListItem(dirs[i].c_str(), nullptr, nullptr, nullptr, is_selected, true,
                                       ImGuiMD2::Icons::Delete, &remove_clicked) &&
                    !remove_clicked) {
                    selected = static_cast<int64_t>(i);
                    selected_changed = true;
                }
                ImGui::PopID();

                if (remove_clicked) {
                    dirs.erase(dirs.begin() + static_cast<std::ptrdiff_t>(i));
                    dirs_changed = true;
                    if (selected >= static_cast<int64_t>(dirs.size())) {
                        selected = static_cast<int64_t>(dirs.size()) - 1; // -1 if now empty
                    }
                    selected_changed = true;
                    break; // vector mutated -- resume next frame
                }
            }
        }
    }

    // A little extra gap between the directory list and the button (see
    // kListButtonGap) -- only when expanded, i.e. when there's actually a
    // list above it. Its height is accounted for in list_card_height.
    if (list_expanded) {
        ImGui::Dummy(ImVec2(0.0f, kListButtonGap));
    }
    if (ImGuiMD2::ContainedButton(ui::i18n::Tr("settings.game.add"))) {
        const std::string picked = platform::PickDirectory();
        if (!picked.empty() && std::find(dirs.begin(), dirs.end(), picked) == dirs.end()) {
            dirs.push_back(picked);
            dirs_changed = true;
            if (selected < 0) { // first entry ever added
                selected = static_cast<int64_t>(dirs.size()) - 1;
                selected_changed = true;
            }
        }
    }

    if (dirs_changed) {
        SaveGameDirs(dirs);
    }
    if (selected_changed) {
        cfg.Set("game.dir.selected", selected < 0 ? kDefaultSelected : selected);
        cfg.Save();
    }

    ImGuiMD2::EndCard();

    // Version-isolation toggle (default on): keeps each instance's mods/saves/
    // resourcepacks under its own versions/<name>/ folder instead of the
    // shared root. Read live from config every frame, write-on-change, same as
    // the rest of this section. Own fixed-height card: one Switch row
    // (TouchTarget tall) + a caption line, plus the ambient gap between them.
    bool isolation = cfg.GetBool("game.isolation", true);
    const float toggle_card_height = ImGuiMD2::Metrics::TouchTarget() + row_gap +
                                     ImGui::GetTextLineHeight() +
                                     2.0f * ImGuiMD2::Metrics::CardPadding();
    ImGuiMD2::BeginCard("##game_isolation_card", ImVec2(0.0f, toggle_card_height), 2);
    if (ImGuiMD2::Switch(ui::i18n::TrLabel("settings.game.isolation", "game_isolation").c_str(),
                         &isolation)) {
        cfg.Set("game.isolation", isolation);
        cfg.Save();
    }
    ImGuiMD2::Text(ImGuiMD2::TextStyle::Caption, ui::i18n::Tr("settings.game.isolation_desc"));
    ImGuiMD2::EndCard();
}

} // namespace ui
