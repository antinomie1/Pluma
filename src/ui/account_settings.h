#pragma once

// Profiles page: an offline-account list. Mirrors ui/game_settings.cpp's
// list+selection shape (a selectable list with per-row delete and a currently-
// active entry), persisted through config::Config's flat scalar store via
// indexed keys (accounts.count, accounts.<i>.name/uuid/type,
// accounts.selected) since config has no array support. Offline UUIDs are
// derived deterministically from the username (net::OfflineUuid).
//
// Threading: render-thread-exclusive, same as game_settings/java_settings --
// BuildAccountSettings() is only called from inside ui::BuildFrame().
#include "ui/app_state.h"

#include <string>

namespace ui {

// Renders the account-list card: selectable rows (username + "Offline"),
// per-row delete, and an "Add Offline Account" button that requests the
// new-account dialog by setting app_state.open_new_account_request (the dialog
// itself is opened at the root window scope in frame.cpp, same cross-child
// deferral as the download page's new-instance dialog).
void BuildAccountSettings(AppState& app_state);

// Appends a new offline account for `name` (UUID auto-derived), selecting it
// if it is the first account. No-op if `name` is empty.
void CreateOfflineAccount(const std::string& name);

// The selected account's username / offline UUID, or "" if none is
// configured. Symmetric with game_settings.cpp's SelectedGameDir(); consumed
// by the Home page launch flow.
std::string SelectedAccountName();
std::string SelectedAccountUuid();

} // namespace ui
