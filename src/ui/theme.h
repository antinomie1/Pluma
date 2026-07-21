#pragma once

#include <imgui_md2/imgui_md2.h>

namespace ui {

// The application default: a light Material Design 2 theme built around the
// Blue swatch. Fonts are filled in by the render layer (LoadBundledFonts) before
// the theme is applied.
ImGuiMD2::Theme MakeDefaultTheme();

} // namespace ui
