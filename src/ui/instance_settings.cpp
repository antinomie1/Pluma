#include "ui/instance_settings.h"

#include "config/config.h"
#include "ui/i18n.h"
#include "ui/java_settings.h"

#include <imgui.h>
#include <imgui_md2/imgui_md2.h>

#include <cstdint>
#include <string>

namespace ui {
namespace {

constexpr std::int64_t kDefaultMemoryMb = 2048;

// Flat-key prefix for one instance's settings, e.g. "instance.MyPack.".
std::string Prefix(const std::string& name) { return "instance." + name + "."; }

bool OverrideEnabled(const std::string& name) {
    return config::Config::Instance().GetBool((Prefix(name) + "override").c_str(), false);
}

} // namespace

std::int64_t InstanceMemoryMb(const std::string& name) {
    config::Config& cfg = config::Config::Instance();
    const std::int64_t global = cfg.GetInt("java.memory_mb", kDefaultMemoryMb);
    if (!OverrideEnabled(name)) return global;
    return cfg.GetInt((Prefix(name) + "memory_mb").c_str(), global);
}

std::string InstanceJvmArgs(const std::string& name) {
    config::Config& cfg = config::Config::Instance();
    const std::string global = cfg.GetString("java.jvm_args", "");
    if (!OverrideEnabled(name)) return global;
    return cfg.GetString((Prefix(name) + "jvm_args").c_str(), global.c_str());
}

void BuildInstanceSettings(const std::string& name) {
    config::Config& cfg = config::Config::Instance();
    const std::string prefix = Prefix(name);
    const std::string override_key = prefix + "override";
    bool override_on = cfg.GetBool(override_key.c_str(), false);

    // "Enable custom settings" toggle, in its own card (one switch row + a
    // caption), same shape as game_settings.cpp's isolation toggle.
    const float toggle_card_h = ImGuiMD2::Metrics::TouchTarget() +
                                ImGui::GetStyle().ItemSpacing.y + ImGui::GetTextLineHeight() +
                                2.0f * ImGuiMD2::Metrics::CardPadding();
    ImGuiMD2::BeginCard("##instance_override_card", ImVec2(0.0f, toggle_card_h), 2);
    if (ImGuiMD2::Switch(ui::i18n::TrLabel("instance.override", "instance_override").c_str(),
                         &override_on)) {
        cfg.Set(override_key.c_str(), override_on);
        // Seed the instance values from the current global ones the first time
        // custom settings are enabled (memory key absent -> GetInt returns -1),
        // so the user starts from a sensible base rather than a blank 2048/"".
        if (override_on && cfg.GetInt((prefix + "memory_mb").c_str(), -1) < 0) {
            cfg.Set((prefix + "memory_mb").c_str(), cfg.GetInt("java.memory_mb", kDefaultMemoryMb));
            cfg.Set((prefix + "jvm_args").c_str(), cfg.GetString("java.jvm_args", ""));
        }
        cfg.Save();
    }
    ImGuiMD2::Text(ImGuiMD2::TextStyle::Caption, ui::i18n::Tr("instance.override_desc"));
    ImGuiMD2::EndCard();

    // Memory + JVM args: the shared control from Settings > Java. When
    // overriding it edits the instance keys; otherwise it shows the global
    // values, disabled, so the effective settings are still visible.
    const std::string mem_key =
        override_on ? (prefix + "memory_mb") : std::string("java.memory_mb");
    const std::string args_key =
        override_on ? (prefix + "jvm_args") : std::string("java.jvm_args");
    ImGui::Dummy(ImVec2(0.0f, 16.0f)); // matches frame.cpp's kCardGap between cards
    ImGui::BeginDisabled(!override_on);
    ui::MemoryAndJvmArgsControls(mem_key.c_str(), args_key.c_str());
    ImGui::EndDisabled();
}

} // namespace ui
