#pragma once

// AutoCard: draws content as a card whose height follows its content instead of
// a fixed constant. imgui_md2's BeginCard/EndCard need a size upfront (they open
// a fixed-height child window), which doesn't fit content whose height can
// change -- so this draws directly into the current (already-scrollable) window
// with normal auto-layout, measures where the content ended, and paints the
// card's background/shadow behind it via draw-list channel splitting. Content is
// drawn exactly once (unlike a measure-then-redraw approach), so there's no risk
// of double-invoking interactive widgets.
//
// Only safe for content whose own width comes from ImGui::CalcItemWidth()
// (SliderFloat/TextField/Select all do -- see the PushItemWidth below) rather
// than ImGui::GetContentRegionAvail() (ListItem, raw CollapsingHeader): with no
// real child window here, GetContentRegionAvail() measures to the *parent*
// window's edge, past this card's own right padding. Content that needs a real
// child window should use ImGuiMD2::BeginCard with a computed height instead.
#include <imgui.h>
#include <imgui_md2/imgui_md2.h>

#include <utility>

namespace ui {

template <typename DrawContent>
void AutoCard(DrawContent&& draw_content) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImGuiMD2::Theme& theme = ImGuiMD2::GetTheme();
    const float padding = ImGuiMD2::Metrics::CardPadding();
    const ImVec2 card_min = ImGui::GetCursorScreenPos();
    const float card_width = ImGui::GetContentRegionAvail().x;

    draw_list->ChannelsSplit(2);
    draw_list->ChannelsSetCurrent(1); // content -- painted on top of the background below

    // Indent (not a one-off SetCursorScreenPos) so the left inset holds for
    // EVERY row draw_content() draws: ImGui::ItemSize() resets each new line's X
    // to the enclosing window's padded edge after every item, so a plain
    // SetCursorScreenPos would only nudge the very first row.
    ImGui::SetCursorScreenPos(ImVec2(card_min.x, card_min.y + padding));
    ImGui::Indent(padding);
    ImGui::PushItemWidth(card_width - 2.0f * padding);
    draw_content();
    ImGui::PopItemWidth();
    ImGui::Unindent(padding);
    // GetCursorScreenPos() here already includes the trailing ItemSpacing.y after
    // the last widget -- subtract it back out so bottom padding matches the rest.
    const float content_bottom = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y;

    const ImVec2 card_max(card_min.x + card_width, content_bottom + padding);

    draw_list->ChannelsSetCurrent(0); // background/shadow -- behind the content channel
    ImGuiMD2::ElevationShadow(draw_list, card_min, card_max, theme.shapes.medium, 2);
    draw_list->AddRectFilled(card_min, card_max, theme.colors.surface.U32(), theme.shapes.medium);
    draw_list->ChannelsMerge();

    // Register the whole card rect as one item so the parent window's
    // content-size/scroll tracking learns this space was consumed.
    ImGui::SetCursorScreenPos(card_min);
    ImGui::Dummy(ImVec2(card_max.x - card_min.x, card_max.y - card_min.y));
}

} // namespace ui
