#include "ui/java_settings.h"

#include "config/config.h"
#include "platform/java_locator.h"
#include "ui/card.h"
#include "ui/i18n.h"

#include <imgui.h>
#include <imgui_md2/imgui_md2.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace ui {
namespace {

constexpr int64_t kDefaultMemoryMb = 2048;
constexpr int64_t kMinMemoryMb = 512;
// Used only when SystemMemoryBytes() can't determine the real total, so the
// slider still has a sane range instead of collapsing to [kMinMemoryMb,
// kMinMemoryMb].
constexpr uint64_t kFallbackSystemRamMb = 32768;

constexpr float kCardGap = 16.0f; // matches frame.cpp's kCardGap
constexpr float kFieldHeight = 40.0f; // thinner than TextField's 56dp default

// Persists `paths` and saves immediately -- matches the "write on change"
// convention the Appearance section already uses (frame.cpp:286-289).
void SaveJavaPaths(const std::vector<std::string>& paths) {
    config::Config& cfg = config::Config::Instance();
    cfg.Set("java.paths.count", static_cast<int64_t>(paths.size()));
    for (std::size_t i = 0; i < paths.size(); ++i) {
        char key[64];
        std::snprintf(key, sizeof(key), "java.paths.%d", static_cast<int>(i));
        cfg.Set(key, paths[i]);
    }
    cfg.Save();
}

// Translates the flat java.paths.count / java.paths.<i> keys into a vector,
// mirroring the "java.paths.*" schema from the plan -- config::Config itself
// has no array support, so this indexed-key convention lives here instead.
std::vector<std::string> LoadJavaPaths() {
    config::Config& cfg = config::Config::Instance();
    const int64_t count = std::max<int64_t>(cfg.GetInt("java.paths.count", 0), 0);

    std::vector<std::string> paths;
    paths.reserve(static_cast<std::size_t>(count));
    for (int64_t i = 0; i < count; ++i) {
        char key[64];
        std::snprintf(key, sizeof(key), "java.paths.%d", static_cast<int>(i));
        paths.push_back(cfg.GetString(key, ""));
    }

    // Self-heal duplicates already on disk (e.g. from the JAVA_HOME
    // trailing-separator bug java_locator.cpp used to have): two
    // different-looking strings for the same file would otherwise stay
    // forever, since the exact-string dedup used when adding new entries
    // can't catch what's already saved.
    std::vector<std::string> deduped;
    deduped.reserve(paths.size());
    for (const std::string& p : paths) {
        const std::string normalized = platform::NormalizeJavaPath(p);
        const bool duplicate = std::any_of(
            deduped.begin(), deduped.end(), [&](const std::string& existing) {
                return platform::NormalizeJavaPath(existing) == normalized;
            });
        if (!duplicate) {
            deduped.push_back(p);
        }
    }
    if (deduped.size() != paths.size()) {
        SaveJavaPaths(deduped); // persist the cleanup so it doesn't reappear next launch
    }
    return deduped;
}

} // namespace

void EnsureJavaAutoDiscovered() {
    config::Config& cfg = config::Config::Instance();
    if (cfg.GetInt("java.paths.count", 0) > 0) {
        return; // already configured (or a previous run found nothing) -- don't re-scan
    }
    const std::vector<std::string> found = platform::DiscoverJavaOnPath();
    if (found.empty()) {
        return;
    }
    SaveJavaPaths(found);
}

std::string SelectedJavaPath() {
    const std::vector<std::string> paths = LoadJavaPaths();
    return paths.empty() ? std::string() : paths.front();
}

std::vector<std::string> InstalledJavaPaths() { return LoadJavaPaths(); }

void BuildJavaSettings() {
    config::Config& cfg = config::Config::Instance();

    // Read the live list every frame rather than caching in a stale static,
    // matching the Appearance section's convention (frame.cpp:250-315).
    // Loaded before the card so its height can be computed from the actual
    // row count below.
    std::vector<std::string> paths = LoadJavaPaths();
    bool paths_changed = false;

    char header[128];
    std::snprintf(header, sizeof(header), ui::i18n::Tr("settings.java.installs_header"),
                  static_cast<int>(paths.size()));

    // Both the header row and each ListItem row below are fixed heights, so
    // this is exact, not guessed -- the card always hugs its actual content,
    // uncapped. A long install list makes the card (and so the page) taller
    // rather than scrolling internally; the outer detail region is already
    // scrollable (frame.cpp's MasterDetail(), detail_card=false path), so
    // that's fine.
    //
    // list_expanded tracks the CollapsingHeader's open/closed state ACROSS
    // frames, not peeked from it THIS frame: the card's height (and so
    // BeginCard's size, needed up front) has to be decided before the header
    // is even drawn, and it only lives inside BeginCard's own child window
    // (a separate ID-stack/window context CollapsingHeader's open state
    // is keyed to -- peeking it from outside would need a different ID than
    // what CollapsingHeader itself resolves to). So sizing runs one frame
    // behind the actual toggle: the row content already shows/hides
    // correctly on the very frame it's clicked (CollapsingHeader's own
    // return value drives that), and the card boundary catches up on the
    // next frame -- a single-frame lag, imperceptible at any real framerate.
    static bool list_expanded = true; // matches ImGuiTreeNodeFlags_DefaultOpen

    // Every row ImGui lays out here (header, each path row or the empty-state
    // text, the buttons row) gets exactly one gap before it, automatically:
    // ImGui::ItemSize() advances the cursor by a widget's own height *plus*
    // the ambient style.ItemSpacing.y after every single item, with no manual
    // Dummy() needed. That ambient gap is ImGuiMD2::Metrics::Gap() (theme.cpp
    // sets style.ItemSpacing to it), so counting it in explicitly here --
    // rather than folding it into GetFrameHeightWithSpacing()-style helpers
    // for some rows but not others -- is what makes every gap in this card
    // provably the same value instead of drifting per row.
    const float row_gap = ImGui::GetStyle().ItemSpacing.y;
    const float header_h = ImGui::GetFrameHeight();
    const int content_rows =
        !list_expanded ? 0 : (paths.empty() ? 1 : static_cast<int>(paths.size()));
    const float content_h = !list_expanded ? 0.0f
                           : paths.empty() ? ImGui::GetTextLineHeight()
                                           : static_cast<float>(paths.size()) *
                                                 ImGuiMD2::Metrics::ListRowHeight();
    const float buttons_h = ImGui::GetFrameHeight();
    // Gaps: header -> (first content row, or straight to buttons if
    // collapsed/empty-and-hidden); between consecutive content rows; content
    // -> buttons (only when content was actually drawn).
    const int gap_count = 1 + std::max(0, content_rows - 1) + (list_expanded ? 1 : 0);
    // BeginCard's `size` is the card's TOTAL outer size, not just its content
    // area -- AlwaysUseWindowPadding applies the ambient CardPadding() on top
    // and bottom inside that fixed size, so it has to be added here too.
    // Omitting it was the actual bug: content needing exactly
    // (header+rows+gaps+buttons) was being squeezed into a card sized for
    // only that much, leaving no room for the padding -- the buttons landed
    // past the (non-scrolling) child's bottom edge and were clipped.
    const float list_card_height = header_h + content_h + buttons_h +
                                   static_cast<float>(gap_count) * row_gap +
                                   2.0f * ImGuiMD2::Metrics::CardPadding();

    // A real BeginCard child, not AutoCard: ListItem/CollapsingHeader size
    // themselves off ImGui::GetContentRegionAvail(), which only works
    // correctly inside a genuine child window (see AutoCard's doc comment).
    // No inner scroll child needed either now that the height above is
    // exact rather than a capped guess -- content always fits.
    ImGuiMD2::BeginCard("##java_list_card", ImVec2(0.0f, list_card_height), 2);

    // No ImGuiMD2 widget covers a collapsible list, so this is the one place
    // in the section that reaches for raw ImGui -- ImGuiCol_Header/Text etc.
    // are already set from the active MD2 theme (ApplyTheme(), called by
    // ImGuiMD2::SetTheme()), so it still reads as themed.
    list_expanded = ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen);
    if (list_expanded) {
        if (paths.empty()) {
            ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2, ui::i18n::Tr("settings.java.empty"));
        } else {
            for (std::size_t i = 0; i < paths.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                bool remove_clicked = false;
                // Show the full path -- ListItem itself ellipsizes to
                // whatever the row's actual pixel width allows and already
                // reserves a gap ahead of the trailing icon (icon width + 8dp,
                // imgui_md2/src/components.cpp's trailing_reserve), so a
                // fixed app-level character cap here would only ever throw
                // away room the card actually had to show more.
                ImGuiMD2::ListItem(paths[i].c_str(), nullptr, nullptr, nullptr, false, true,
                                   ImGuiMD2::Icons::Delete, &remove_clicked);
                ImGui::PopID();

                if (remove_clicked) {
                    paths.erase(paths.begin() + static_cast<std::ptrdiff_t>(i));
                    paths_changed = true;
                    break; // vector mutated -- stop iterating this frame, resume next frame
                }
            }
        }
    }

    // No manual Dummy() before this row -- the ambient ItemSpacing.y already
    // places exactly row_gap (see above) between it and whatever was drawn
    // last (the final path row, the empty-state text, or the header itself
    // when collapsed).
    if (ImGuiMD2::ContainedButton(ui::i18n::Tr("settings.java.auto_find"))) {
        const std::vector<std::string> found = platform::DiscoverJavaOnPath();
        for (const std::string& p : found) {
            if (std::find(paths.begin(), paths.end(), p) == paths.end()) {
                paths.push_back(p);
                paths_changed = true;
            }
        }
    }
    ImGui::SameLine();
    if (ImGuiMD2::OutlinedButton(ui::i18n::Tr("settings.java.browse"))) {
        const std::string picked = platform::PickJavaExecutable();
        if (!picked.empty() && std::find(paths.begin(), paths.end(), picked) == paths.end()) {
            paths.push_back(picked);
            paths_changed = true;
        }
    }

    if (paths_changed) {
        SaveJavaPaths(paths);
    }

    ImGuiMD2::EndCard();
    ImGui::Dummy(ImVec2(0.0f, kCardGap));
    MemoryAndJvmArgsControls("java.memory_mb", "java.jvm_args");
}

void MemoryAndJvmArgsControls(const char* memory_key, const char* jvm_args_key) {
    config::Config& cfg = config::Config::Instance();
    // Scope every fixed widget id ("##java_memory_slider", ...) under the
    // memory key so two contexts (global Java settings vs. a per-instance
    // page) never collide if they were ever drawn in the same frame.
    ImGui::PushID(memory_key);
    AutoCard([&] {
        // JVM memory size. SliderFloat's min/max give free range-clamping
        // against the machine's actual RAM, so the value can never be pushed
        // above it from either control below; a stale value from a config
        // copied off a higher-RAM machine is clamped on read too.
        //
        // SliderFloat bakes its own label + read-only value text into the
        // same row as the track (imgui_md2/src/components.cpp), sized for
        // its default font metrics -- at Pluma's 1.3x type scale
        // (ui::MakeTheme) that label crowds right up against the track, and
        // it's not editable besides. Draw our own label with real spacing,
        // pair it with a directly-editable numeric field, and pass
        // SliderFloat a blank label/format so it's just the track (still
        // there for a quick drag, kept in sync with the field).
        const uint64_t system_ram_bytes = platform::SystemMemoryBytes();
        uint64_t system_ram_mb =
            system_ram_bytes > 0 ? system_ram_bytes / (1024ull * 1024ull) : kFallbackSystemRamMb;
        if (system_ram_mb < static_cast<uint64_t>(kMinMemoryMb)) {
            system_ram_mb = static_cast<uint64_t>(kMinMemoryMb);
        }

        int64_t memory_mb = cfg.GetInt(memory_key, kDefaultMemoryMb);
        memory_mb = std::clamp(memory_mb, kMinMemoryMb, static_cast<int64_t>(system_ram_mb));

        ImGuiMD2::Text(ImGuiMD2::TextStyle::Body1, ui::i18n::Tr("settings.java.memory"));
        // No manual Dummy() before the slider row -- the ambient
        // ItemSpacing.y (ImGuiMD2::Metrics::Gap()) already places one
        // row-standard gap after this label, matching every other row gap in
        // this card and the list card above.

        int64_t new_memory_mb = memory_mb;
        bool memory_changed = false;

        // Slider first, then the editable field + unit on the same line.
        // Capped well short of the full row width -- letting it fill all the
        // leftover space (the card is wide) made it look stretched -- with
        // the same available-space fallback for narrow cards.
        //
        // Budgeted off CalcItemWidth(), not GetContentRegionAvail(): inside
        // AutoCard the content is drawn directly into the parent window (no
        // child window of its own), so GetContentRegionAvail() measures to
        // the parent's edge -- past the card's own right padding -- and this
        // row would run flush to the card's right edge instead of leaving a
        // matching inset. CalcItemWidth() correctly reflects the width
        // AutoCard pushed (the card's padded content width).
        //
        // The "MB" text width used here is only a rough upfront guess for
        // SliderFloat's own SetNextItemWidth (which needs a width before
        // it's drawn) -- it doesn't need to be pixel-exact, because the row
        // actually used by the JVM-args field below (memory_row_width) is
        // measured from what really got drawn (BeginGroup/EndGroup), not
        // computed from this or any other separate formula. An earlier
        // version tried to derive memory_row_width purely by formula and
        // got it wrong (measured "MB" in the wrong font, then separately in
        // a too-narrow card overflowed past the padding) -- measuring the
        // actual result sidesteps both failure modes at once.
        constexpr float kFieldWidth = 96.0f;
        constexpr float kMaxSliderWidth = 220.0f;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float slider_width = std::clamp(
            ImGui::CalcItemWidth() - kFieldWidth - ImGui::CalcTextSize("MB").x - 2.0f * spacing,
            80.0f, kMaxSliderWidth);

        // SliderFloat's own row is a fixed Metrics::TouchTarget() tall
        // (48dp); the outlined field next to it is the shorter kFieldHeight
        // (40dp, see "细一点"). Captured before drawing so the field (and,
        // after it, the row as a whole) can be centered against the
        // slider's true height below -- SameLine() alone only top-aligns
        // siblings, which left the compact field floating near the top of
        // the taller slider instead of level with it.
        ImGui::BeginGroup();
        const float row_top_y = ImGui::GetCursorPosY();
        const float slider_row_height = ImGuiMD2::Metrics::TouchTarget();

        ImGui::SetNextItemWidth(slider_width);
        float memory_mb_float = static_cast<float>(memory_mb);
        if (ImGuiMD2::SliderFloat("##java_memory_slider", &memory_mb_float,
                                  static_cast<float>(kMinMemoryMb),
                                  static_cast<float>(system_ram_mb), "")) {
            new_memory_mb = static_cast<int64_t>(memory_mb_float + 0.5f);
            new_memory_mb =
                std::clamp(new_memory_mb, kMinMemoryMb, static_cast<int64_t>(system_ram_mb));
            memory_changed = true;
        }
        ImGui::SameLine();
        ImGui::SetCursorPosY(row_top_y + (slider_row_height - kFieldHeight) * 0.5f);

        // Resynced whenever the live (config-backed) value differs from what
        // the buffer was last set to -- covers the initial draw and any
        // change made via the slider above, but leaves an in-progress,
        // uncommitted edit alone (the config value doesn't change until the
        // field is committed).
        // Per-key buffer (keyed by memory_key) so the global page and each
        // instance page keep independent in-progress edits. synced defaults to
        // -1 (an impossible MB value) so the first draw always resyncs.
        struct MemoryEdit {
            std::array<char, 16> text{};
            int64_t synced = -1;
        };
        static std::unordered_map<std::string, MemoryEdit> memory_edit_map;
        MemoryEdit& me = memory_edit_map[memory_key];
        std::array<char, 16>& memory_text = me.text;
        int64_t& memory_text_synced_value = me.synced;
        if (memory_text_synced_value != memory_mb) {
            std::snprintf(memory_text.data(), memory_text.size(), "%d",
                          static_cast<int>(memory_mb));
            memory_text_synced_value = memory_mb;
        }

        ImGuiMD2::TextFieldOptions memory_field_options;
        memory_field_options.variant = ImGuiMD2::TextFieldVariant::Outlined;
        memory_field_options.size = ImVec2(kFieldWidth, kFieldHeight);
        ImGuiMD2::TextField("##java_memory_input", memory_text.data(), memory_text.size(),
                            memory_field_options, ImGuiInputTextFlags_CharsDecimal);
        // Commit on blur/Enter, not per keystroke -- clamping "2" to the 512
        // MB floor while the user is still typing "2048" would stomp the
        // field mid-edit.
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const int64_t typed = std::strtoll(memory_text.data(), nullptr, 10);
            new_memory_mb = std::clamp(typed, kMinMemoryMb, static_cast<int64_t>(system_ram_mb));
            memory_changed = true;
        }
        ImGui::SameLine();
        ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2, "MB");
        ImGui::EndGroup();
        // The group's measured width -- not a separate formula -- so the
        // JVM-args field below can be capped to exactly what this row
        // actually rendered as, however wide that turned out to be. Also
        // correctly advances past the row's true (tallest-element) height
        // regardless of the field/"MB" nudging above, since EndGroup()
        // tracks the union of every drawn item's extent, not just the last.
        const float memory_row_width = ImGui::GetItemRectSize().x;

        if (memory_changed) {
            cfg.Set(memory_key, new_memory_mb);
            cfg.Save();
        }

        // JVM arguments: InputText needs a persistent buffer to edit in
        // place. Per-key (keyed by jvm_args_key) and primed once from config
        // the first time each key is seen, so multiple contexts don't share
        // one buffer -- write-on-change keeps buffer and config in sync after.
        struct JvmEdit {
            std::array<char, 512> buf{};
            bool loaded = false;
        };
        static std::unordered_map<std::string, JvmEdit> jvm_edit_map;
        JvmEdit& je = jvm_edit_map[jvm_args_key];
        std::array<char, 512>& jvm_args_buffer = je.buf;
        bool& jvm_args_loaded = je.loaded;
        if (!jvm_args_loaded) {
            const std::string current = cfg.GetString(jvm_args_key, "");
            std::snprintf(jvm_args_buffer.data(), jvm_args_buffer.size(), "%s", current.c_str());
            jvm_args_loaded = true;
        }

        // No manual Dummy() before the JVM-args field either -- same
        // ambient-gap reasoning as the memory label above.
        ImGuiMD2::TextFieldOptions jvm_args_options;
        jvm_args_options.variant = ImGuiMD2::TextFieldVariant::Outlined;
        // Capped to memory_row_width (not full-width) so this row ends at
        // the same right edge as the memory row above it -- see that
        // variable's comment.
        jvm_args_options.size = ImVec2(memory_row_width, kFieldHeight);
        if (ImGuiMD2::TextField(ui::i18n::Tr("settings.java.jvm_args"), jvm_args_buffer.data(),
                                jvm_args_buffer.size(), jvm_args_options)) {
            cfg.Set(jvm_args_key, std::string(jvm_args_buffer.data()));
            cfg.Save();
        }
    });
    ImGui::PopID();
}

} // namespace ui
