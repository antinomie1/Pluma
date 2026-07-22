#include "ui/theme.h"

namespace ui {

ImGuiMD2::Theme MakeTheme(bool dark, ImGuiMD2::Swatch accent) {
    // Light: build from S700 (rather than the default S500) so MD2's
    // accessible on-color resolves to white for the app bar and contained
    // buttons at WCAG AA contrast, while staying a standard palette shade.
    // A400 is the secondary accent the FloatingActionButtons use.
    // Dark: S200/A200 keep the same relative brightness relationship on a
    // dark surface.
    ImGuiMD2::Theme theme =
        dark ? ImGuiMD2::Theme::Dark(ImGuiMD2::Palette(accent, ImGuiMD2::Shade::S200),
                                     ImGuiMD2::Palette(accent, ImGuiMD2::Shade::A200))
             : ImGuiMD2::Theme::Light(ImGuiMD2::Palette(accent, ImGuiMD2::Shade::S700),
                                      ImGuiMD2::Palette(accent, ImGuiMD2::Shade::A400));
    // The app-bar FABs draw from the secondary color: a vivid accent container
    // with white icons (force on_secondary so the icons stay white on it).
    theme.colors.on_secondary = ImGuiMD2::Color::FromHex(0xffffff);
    theme.name = dark ? "pluma_dark" : "pluma_light";
    return theme;
}

ImGuiMD2::Theme MakeDefaultTheme() {
    ImGuiMD2::Theme theme = MakeTheme(false, ImGuiMD2::Swatch::Blue);
    theme.name = "pluma_blue";
    return theme;
}

} // namespace ui
