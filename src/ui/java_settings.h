#pragma once

// Settings > Java: a configured-Java-installs list (auto-discovered from
// PATH/JAVA_HOME plus manually browsed-to entries), a JVM memory-size slider
// clamped to the machine's RAM, and a JVM-arguments field. Mirrors
// ui/theme.cpp / ui/i18n.cpp being separate small modules rather than
// growing frame.cpp further. Persistence goes through config::Config's flat
// scalar store via indexed keys (java.paths.count/java.paths.<i>,
// java.memory_mb, java.jvm_args) -- see java_settings.cpp for the load/save
// helpers, since config::Config itself has no array support.
//
// Threading: render-thread-exclusive, same as config::Config / ui::theme /
// ui::i18n -- EnsureJavaAutoDiscovered() is only called once from
// render/renderer.cpp (before the main loop starts), and BuildJavaSettings()
// is only called from inside ui::BuildFrame().
#include <string>

namespace ui {

// Called once at startup (see render/renderer.cpp) -- if config has no Java
// paths configured yet, runs platform::DiscoverJavaOnPath() and persists
// whatever is found.
void EnsureJavaAutoDiscovered();

// Renders the Settings > Java section body. Called from frame.cpp's
// SettingsSection::Java case in place of the former page.wip stub.
void BuildJavaSettings();

// The Java executable to launch the game with: the first configured Java path
// (java.paths.0), or "" if none is configured. Symmetric with
// game_settings.cpp's SelectedGameDir(); used by the Home page's launch flow.
std::string SelectedJavaPath();

// Renders the JVM memory-size slider/field + JVM-arguments field as a card,
// reading/writing the two given config keys. Shared by the global Java
// settings (java.memory_mb/java.jvm_args) and the per-instance settings page
// (instance.<name>.memory_mb / instance.<name>.jvm_args) so both look and
// behave identically. Must be called on the render thread inside a frame,
// like the rest of this module.
void MemoryAndJvmArgsControls(const char* memory_key, const char* jvm_args_key);

} // namespace ui
