#pragma once

#include <string>

// Settings > Game: the list of configured game (".minecraft") directories a
// new install can target, plus which one is currently selected. Mirrors
// ui/java_settings.h being a separate small module rather than growing
// frame.cpp further. Persistence goes through config::Config's flat scalar
// store via indexed keys (game.dirs.count/game.dirs.<i>, game.dir.selected)
// -- see game_settings.cpp for the load/save helpers, following the exact
// same pattern as java_settings.cpp's java.paths.* keys.
//
// Threading: render-thread-exclusive, same as config::Config / ui::theme /
// ui::i18n -- EnsureGameDirSeeded() is only called once from
// render/renderer.cpp (before the main loop starts), and BuildGameSettings()
// is only called from inside ui::BuildFrame().
namespace ui {

// Called once at startup (see render/renderer.cpp) -- if config has no game
// directories configured yet, seeds the list with
// std::filesystem::current_path()/".minecraft" and persists it.
void EnsureGameDirSeeded();

// Renders the Settings > Game section body. Called from frame.cpp's
// SettingsSection::Game case in place of the former page.wip stub.
void BuildGameSettings();

// The currently selected game directory's absolute path (game.dirs.<
// game.dir.selected>), or "" if none is configured yet (shouldn't happen
// past startup -- EnsureGameDirSeeded() guarantees at least one entry).
// Used by the Download page's new-instance dialog (frame.cpp) to fill in
// InstallParams::game_dir without duplicating the indexed-key read logic.
std::string SelectedGameDir();

} // namespace ui
