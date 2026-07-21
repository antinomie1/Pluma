#include "ui/theme.h"

namespace ui {

ImGuiMD2::Theme MakeDefaultTheme() {
    // Build from Blue 700 (rather than the default 500) so MD2's accessible
    // on-color resolves to white for the app bar and contained buttons at WCAG
    // AA contrast, while staying a standard Blue palette shade. Amber A200 is the
    // secondary accent the FloatingActionButtons use.
    ImGuiMD2::Theme theme = ImGuiMD2::Theme::Light(
        ImGuiMD2::Palette(ImGuiMD2::Swatch::Blue, ImGuiMD2::Shade::S700),
        ImGuiMD2::Palette(ImGuiMD2::Swatch::Blue, ImGuiMD2::Shade::A400));
    // The app-bar FABs draw from the secondary color: a vivid blue container with
    // white icons (force on_secondary so the icons stay white on blue).
    theme.colors.on_secondary = ImGuiMD2::Color::FromHex(0xffffff);
    theme.name = "pluma_blue";
    return theme;
}

} // namespace ui
