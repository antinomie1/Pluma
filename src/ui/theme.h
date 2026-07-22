#pragma once

#include <imgui_md2/imgui_md2.h>

namespace ui {

// Builds a Material Design 2 theme for the given dark/light mode and accent
// swatch. `dark` picks Theme::Dark/Light with shades chosen to keep the
// on-color contrast MakeDefaultTheme() already relies on (S200/A200 for dark,
// S700/A400 for light); on_secondary is then forced to white so the app-bar
// FABs' icons stay legible regardless of accent. Fonts are filled in by the
// render layer (LoadBundledFonts) before the theme is applied, or must be
// copied from the previous theme (t.fonts = GetTheme().fonts) when switching
// at runtime.
ImGuiMD2::Theme MakeTheme(bool dark, ImGuiMD2::Swatch accent);

// The application default: a light Material Design 2 theme built around the
// Blue swatch. Equivalent to MakeTheme(false, Swatch::Blue).
ImGuiMD2::Theme MakeDefaultTheme();

} // namespace ui
