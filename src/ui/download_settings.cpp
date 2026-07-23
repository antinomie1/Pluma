#include "ui/download_settings.h"

#include "config/config.h"
#include "ui/i18n.h"

#include <imgui.h>
#include <imgui_md2/imgui_md2.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <string>

namespace ui {
namespace {

constexpr int64_t kDefaultConcurrency = 8;
constexpr int64_t kMinConcurrency = 1;
constexpr int64_t kMaxConcurrency = 32;

constexpr int64_t kDefaultThreads = 4;
constexpr int64_t kMinThreads = 1;
constexpr int64_t kMaxThreads = 16;

constexpr float kFieldHeight = 40.0f; // matches java_settings.cpp's kFieldHeight

// Draws draw_content() as a card whose height follows its content instead of
// a fixed constant. Duplicated from java_settings.cpp's AutoCard (see that
// file's doc comment for the full rationale) rather than shared -- it's not
// exposed via java_settings.h, matching this project's "small per-file UI
// helper" convention for content sized off ImGui::CalcItemWidth() (Select/
// SliderFloat/TextField all do) rather than ImGui::GetContentRegionAvail().
template <typename DrawContent>
void AutoCard(DrawContent&& draw_content) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImGuiMD2::Theme& theme = ImGuiMD2::GetTheme();
    const float padding = ImGuiMD2::Metrics::CardPadding();
    const ImVec2 card_min = ImGui::GetCursorScreenPos();
    const float card_width = ImGui::GetContentRegionAvail().x;

    draw_list->ChannelsSplit(2);
    draw_list->ChannelsSetCurrent(1);

    ImGui::SetCursorScreenPos(ImVec2(card_min.x, card_min.y + padding));
    ImGui::Indent(padding);
    ImGui::PushItemWidth(card_width - 2.0f * padding);
    draw_content();
    ImGui::PopItemWidth();
    ImGui::Unindent(padding);
    const float content_bottom = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y;

    const ImVec2 card_max(card_min.x + card_width, content_bottom + padding);

    draw_list->ChannelsSetCurrent(0);
    ImGuiMD2::ElevationShadow(draw_list, card_min, card_max, theme.shapes.medium, 2);
    draw_list->AddRectFilled(card_min, card_max, theme.colors.surface.U32(), theme.shapes.medium);
    draw_list->ChannelsMerge();

    ImGui::SetCursorScreenPos(card_min);
    ImGui::Dummy(ImVec2(card_max.x - card_min.x, card_max.y - card_min.y));
}

// One "label, then a slider paired with a directly-editable integer field" row
// -- the exact idiom java_settings.cpp's JVM memory row uses, duplicated here
// (rather than parameterized) since each call site needs its own persistent
// text-edit buffer (a function-local static), which a shared helper can't
// provide per call site without its own template-per-instantiation trick.
// `config_key` is written+saved immediately on commit (slider drag, or
// Enter/blur on the field).
void IntSliderField(const char* label_key, const char* config_key, int64_t min_v, int64_t max_v,
                    int64_t def_v, char* text_buf, std::size_t text_buf_size,
                    int64_t* text_synced_value) {
    config::Config& cfg = config::Config::Instance();
    int64_t value = std::clamp(cfg.GetInt(config_key, def_v), min_v, max_v);

    // Both rows use the same literal "##slider"/"##field" widget ids, so scope
    // them under the (unique) config key -- otherwise the concurrency and
    // threads sliders/fields collide on the same ImGuiID and fight over one
    // shared active/edit state.
    ImGui::PushID(config_key);

    ImGuiMD2::Text(ImGuiMD2::TextStyle::Body1, ui::i18n::Tr(label_key));

    int64_t new_value = value;
    bool changed = false;

    constexpr float kFieldWidth = 96.0f;
    constexpr float kMaxSliderWidth = 220.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float slider_width =
        std::clamp(ImGui::CalcItemWidth() - kFieldWidth - spacing, 80.0f, kMaxSliderWidth);

    ImGui::BeginGroup();
    const float row_top_y = ImGui::GetCursorPosY();
    const float slider_row_height = ImGuiMD2::Metrics::TouchTarget();

    ImGui::SetNextItemWidth(slider_width);
    float value_float = static_cast<float>(value);
    if (ImGuiMD2::SliderFloat("##slider", &value_float, static_cast<float>(min_v),
                              static_cast<float>(max_v), "")) {
        new_value = static_cast<int64_t>(value_float + 0.5f);
        new_value = std::clamp(new_value, min_v, max_v);
        changed = true;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosY(row_top_y + (slider_row_height - kFieldHeight) * 0.5f);

    if (*text_synced_value != value) {
        std::snprintf(text_buf, text_buf_size, "%d", static_cast<int>(value));
        *text_synced_value = value;
    }

    ImGuiMD2::TextFieldOptions field_options;
    field_options.variant = ImGuiMD2::TextFieldVariant::Outlined;
    field_options.size = ImVec2(kFieldWidth, kFieldHeight);
    ImGuiMD2::TextField("##field", text_buf, text_buf_size, field_options,
                        ImGuiInputTextFlags_CharsDecimal);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        const int64_t typed = std::strtoll(text_buf, nullptr, 10);
        new_value = std::clamp(typed, min_v, max_v);
        changed = true;
    }
    ImGui::EndGroup();

    ImGui::PopID();

    if (changed) {
        cfg.Set(config_key, new_value);
        cfg.Save();
    }
}

} // namespace

void BuildDownloadSettings() {
    // No headline text here, matching BuildJavaSettings' precedent (the
    // Settings rail's own selected row already identifies the section) --
    // straight into the card.
    AutoCard([&] {
        config::Config& cfg = config::Config::Instance();

        // Mirror source: Auto (BMCLAPI first, official fallback) / official-
        // only / BMCLAPI-only.
        const std::string mirror_value = cfg.GetString("download.mirror", "auto");
        int mirror_idx = 0;
        if (mirror_value == "official") mirror_idx = 1;
        else if (mirror_value == "bmclapi") mirror_idx = 2;

        const char* mirror_names[] = {
            ui::i18n::Tr("settings.download.mirror.auto"),
            ui::i18n::Tr("settings.download.mirror.official"),
            ui::i18n::Tr("settings.download.mirror.bmclapi"),
        };
        if (ImGuiMD2::Select(ui::i18n::Tr("settings.download.mirror"), &mirror_idx, mirror_names,
                             3)) {
            static const char* const kMirrorValues[] = {"auto", "official", "bmclapi"};
            cfg.Set("download.mirror", kMirrorValues[mirror_idx]);
            cfg.Save();
        }

        static char concurrency_text[16] = "";
        static int64_t concurrency_synced = -1;
        IntSliderField("settings.download.concurrency", "download.concurrency", kMinConcurrency,
                       kMaxConcurrency, kDefaultConcurrency, concurrency_text,
                       sizeof(concurrency_text), &concurrency_synced);

        static char threads_text[16] = "";
        static int64_t threads_synced = -1;
        IntSliderField("settings.download.threads", "download.threads", kMinThreads, kMaxThreads,
                       kDefaultThreads, threads_text, sizeof(threads_text), &threads_synced);
    });
}

} // namespace ui
