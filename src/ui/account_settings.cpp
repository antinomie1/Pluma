#include "ui/account_settings.h"

#include "config/config.h"
#include "net/hash.h"
#include "ui/i18n.h"

#include <imgui.h>
#include <imgui_md2/imgui_md2.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

namespace ui {
namespace {

struct Account {
    std::string name;
    std::string uuid;
    std::string type; // "offline" or "msa"
    std::string token;   // Minecraft access token ("" / unused for offline)
    std::string refresh; // Microsoft refresh token (msa only)
};

// Extra gap between the account list and the "add" button, folded into the
// fixed card height below -- same as game_settings.cpp's kListButtonGap.
constexpr float kListButtonGap = 12.0f;

// Persists `accounts` immediately (write-on-change), following
// game_settings.cpp's SaveGameDirs. Each account spans three flat keys since
// config::Config stores only scalars.
void SaveAccounts(const std::vector<Account>& accounts) {
    config::Config& cfg = config::Config::Instance();
    cfg.Set("accounts.count", static_cast<int64_t>(accounts.size()));
    for (std::size_t i = 0; i < accounts.size(); ++i) {
        char key[64];
        std::snprintf(key, sizeof(key), "accounts.%d.name", static_cast<int>(i));
        cfg.Set(key, accounts[i].name);
        std::snprintf(key, sizeof(key), "accounts.%d.uuid", static_cast<int>(i));
        cfg.Set(key, accounts[i].uuid);
        std::snprintf(key, sizeof(key), "accounts.%d.type", static_cast<int>(i));
        cfg.Set(key, accounts[i].type);
        std::snprintf(key, sizeof(key), "accounts.%d.token", static_cast<int>(i));
        cfg.Set(key, accounts[i].token);
        std::snprintf(key, sizeof(key), "accounts.%d.refresh", static_cast<int>(i));
        cfg.Set(key, accounts[i].refresh);
    }
    cfg.Save();
}

std::vector<Account> LoadAccounts() {
    config::Config& cfg = config::Config::Instance();
    const int64_t count = std::max<int64_t>(cfg.GetInt("accounts.count", 0), 0);

    std::vector<Account> accounts;
    accounts.reserve(static_cast<std::size_t>(count));
    for (int64_t i = 0; i < count; ++i) {
        char key[64];
        Account a;
        std::snprintf(key, sizeof(key), "accounts.%d.name", static_cast<int>(i));
        a.name = cfg.GetString(key, "");
        std::snprintf(key, sizeof(key), "accounts.%d.uuid", static_cast<int>(i));
        a.uuid = cfg.GetString(key, "");
        std::snprintf(key, sizeof(key), "accounts.%d.type", static_cast<int>(i));
        a.type = cfg.GetString(key, "offline");
        std::snprintf(key, sizeof(key), "accounts.%d.token", static_cast<int>(i));
        a.token = cfg.GetString(key, "");
        std::snprintf(key, sizeof(key), "accounts.%d.refresh", static_cast<int>(i));
        a.refresh = cfg.GetString(key, "");
        accounts.push_back(std::move(a));
    }
    return accounts;
}

int64_t SelectedIndex(const std::vector<Account>& accounts) {
    if (accounts.empty()) return -1;
    config::Config& cfg = config::Config::Instance();
    return std::clamp(cfg.GetInt("accounts.selected", 0), int64_t{0},
                      static_cast<int64_t>(accounts.size()) - 1);
}

} // namespace

void CreateOfflineAccount(const std::string& name) {
    if (name.empty()) return;
    std::vector<Account> accounts = LoadAccounts();

    Account account;
    account.name = name;
    account.uuid = net::OfflineUuid(name);
    account.type = "offline";
    accounts.push_back(std::move(account));
    SaveAccounts(accounts);

    if (accounts.size() == 1) {
        config::Config& cfg = config::Config::Instance();
        cfg.Set("accounts.selected", int64_t{0});
        cfg.Save();
    }
}

void CommitMicrosoftAccount(const std::string& name, const std::string& uuid,
                            const std::string& access_token, const std::string& refresh_token) {
    if (uuid.empty()) return;
    std::vector<Account> accounts = LoadAccounts();

    // Refresh tokens in place if this Microsoft account is already present.
    const auto existing = std::find_if(accounts.begin(), accounts.end(), [&](const Account& a) {
        return a.type == "msa" && a.uuid == uuid;
    });
    int64_t selected_index;
    if (existing != accounts.end()) {
        existing->name = name;
        existing->token = access_token;
        existing->refresh = refresh_token;
        selected_index = static_cast<int64_t>(std::distance(accounts.begin(), existing));
    } else {
        Account account;
        account.name = name;
        account.uuid = uuid;
        account.type = "msa";
        account.token = access_token;
        account.refresh = refresh_token;
        accounts.push_back(std::move(account));
        selected_index = static_cast<int64_t>(accounts.size()) - 1;
    }
    SaveAccounts(accounts);

    config::Config& cfg = config::Config::Instance();
    cfg.Set("accounts.selected", selected_index);
    cfg.Save();
}

std::string SelectedAccountName() {
    const std::vector<Account> accounts = LoadAccounts();
    const int64_t selected = SelectedIndex(accounts);
    return selected < 0 ? std::string() : accounts[static_cast<std::size_t>(selected)].name;
}

std::string SelectedAccountUuid() {
    const std::vector<Account> accounts = LoadAccounts();
    const int64_t selected = SelectedIndex(accounts);
    return selected < 0 ? std::string() : accounts[static_cast<std::size_t>(selected)].uuid;
}

std::string SelectedAccountToken() {
    const std::vector<Account> accounts = LoadAccounts();
    const int64_t selected = SelectedIndex(accounts);
    if (selected < 0) return "0";
    const std::string& token = accounts[static_cast<std::size_t>(selected)].token;
    return token.empty() ? "0" : token; // offline accounts launch with the "0" placeholder
}

std::string SelectedAccountType() {
    const std::vector<Account> accounts = LoadAccounts();
    const int64_t selected = SelectedIndex(accounts);
    if (selected < 0) return "legacy";
    return accounts[static_cast<std::size_t>(selected)].type == "msa" ? "msa" : "legacy";
}

void BuildAccountSettings(AppState& app_state) {
    config::Config& cfg = config::Config::Instance();

    std::vector<Account> accounts = LoadAccounts();
    int64_t selected = SelectedIndex(accounts);
    bool accounts_changed = false;
    bool selected_changed = false;

    char header[128];
    std::snprintf(header, sizeof(header), ui::i18n::Tr("profiles.accounts_header"),
                  static_cast<int>(accounts.size()));

    // Exact card height, matching game_settings.cpp's list card. Rows are
    // two-line (username + "Offline"), so they use ListRowHeightTwoLine.
    static bool list_expanded = true;
    const float row_gap = ImGui::GetStyle().ItemSpacing.y;
    const float header_h = ImGui::GetFrameHeight();
    const float content_h = !list_expanded ? 0.0f
                            : accounts.empty()
                                ? ImGui::GetTextLineHeight()
                                : static_cast<float>(accounts.size()) *
                                      ImGuiMD2::Metrics::ListRowHeightTwoLine();
    const int content_rows =
        !list_expanded ? 0 : (accounts.empty() ? 1 : static_cast<int>(accounts.size()));
    const float buttons_h = ImGui::GetFrameHeight();
    const int gap_count = 1 + std::max(0, content_rows - 1) + (list_expanded ? 1 : 0);
    const float extra_button_gap = list_expanded ? (kListButtonGap + row_gap) : 0.0f;
    const float list_card_height = header_h + content_h + buttons_h +
                                   static_cast<float>(gap_count) * row_gap + extra_button_gap +
                                   2.0f * ImGuiMD2::Metrics::CardPadding();

    ImGuiMD2::BeginCard("##accounts_card", ImVec2(0.0f, list_card_height), 2);

    list_expanded = ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen);
    if (list_expanded) {
        if (accounts.empty()) {
            ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2, ui::i18n::Tr("profiles.accounts.empty"));
        } else {
            for (std::size_t i = 0; i < accounts.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                bool remove_clicked = false;
                const bool is_selected = (static_cast<int64_t>(i) == selected);
                const char* subtitle = accounts[i].type == "msa"
                                           ? ui::i18n::Tr("profiles.account.microsoft")
                                           : ui::i18n::Tr("profiles.account.offline");
                if (ImGuiMD2::ListItem(accounts[i].name.c_str(), subtitle, nullptr, nullptr,
                                       is_selected, true, ImGuiMD2::Icons::Delete, &remove_clicked) &&
                    !remove_clicked) {
                    selected = static_cast<int64_t>(i);
                    selected_changed = true;
                }
                ImGui::PopID();

                if (remove_clicked) {
                    accounts.erase(accounts.begin() + static_cast<std::ptrdiff_t>(i));
                    accounts_changed = true;
                    if (selected >= static_cast<int64_t>(accounts.size())) {
                        selected = static_cast<int64_t>(accounts.size()) - 1;
                    }
                    selected_changed = true;
                    break;
                }
            }
        }
    }

    if (list_expanded) {
        ImGui::Dummy(ImVec2(0.0f, kListButtonGap));
    }
    if (ImGuiMD2::ContainedButton(ui::i18n::Tr("profiles.accounts.add_microsoft"))) {
        app_state.open_ms_login_request = true;
    }
    ImGui::SameLine();
    if (ImGuiMD2::OutlinedButton(ui::i18n::Tr("profiles.accounts.add_offline"))) {
        app_state.open_new_account_request = true;
    }

    if (accounts_changed) {
        SaveAccounts(accounts);
    }
    if (selected_changed) {
        cfg.Set("accounts.selected", selected < 0 ? int64_t{0} : selected);
        cfg.Save();
    }

    ImGuiMD2::EndCard();
}

} // namespace ui
