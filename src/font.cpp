/*
 * launcher - desktop launcher application
 * Copyright (C) 2026 antinomie1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License only.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "font.h"

namespace Font {
    ImFont* Font = nullptr;
    ImFont* bigFont = nullptr;
    ImFont* boldFont = nullptr;
    
    void InitFont(float main_scale) {
        ImGuiIO& io = ImGui::GetIO();
        // Prefer Chinese ranges for UI strings; falls back gracefully if glyphs missing.
        const ImWchar* ranges = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();
        Font = io.Fonts->AddFontFromMemoryCompressedBase85TTF(
            defFont_compressed_data_base85, 22.0f * main_scale, nullptr, ranges);
        bigFont = io.Fonts->AddFontFromMemoryCompressedBase85TTF(
            defFont_compressed_data_base85, 30.0f * main_scale, nullptr, ranges);
        // Bold title font is small/Latin; keep default ranges.
        boldFont = io.Fonts->AddFontFromMemoryCompressedBase85TTF(
            boldFont_compressed_data_base85, 28.0f * main_scale, nullptr, io.Fonts->GetGlyphRangesDefault());
        io.FontDefault = Font;
    }
}
