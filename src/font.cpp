#include "font.h"

namespace Font {
    ImFont* Font = nullptr;
    ImFont* bigFont = nullptr;
    ImFont* boldFont = nullptr;
    
    void InitFont(float main_scale) {
        ImGuiIO& io = ImGui::GetIO();
        Font = io.Fonts->AddFontFromMemoryCompressedBase85TTF(defFont_compressed_data_base85, 24.0f * main_scale, nullptr, io.Fonts->GetGlyphRangesDefault());
        bigFont = io.Fonts->AddFontFromMemoryCompressedBase85TTF(defFont_compressed_data_base85, 32.0f * main_scale, nullptr, io.Fonts->GetGlyphRangesDefault());
        boldFont = io.Fonts->AddFontFromMemoryCompressedBase85TTF(boldFont_compressed_data_base85, 30.0f * main_scale, nullptr, io.Fonts->GetGlyphRangesDefault());
    }
}
