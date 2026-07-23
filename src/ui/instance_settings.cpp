#include "ui/instance_settings.h"

#include "config/config.h"
#include "ui/card.h"
#include "ui/i18n.h"
#include "ui/java_settings.h"

#include <imgui.h>
#include <imgui_md2/imgui_md2.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ui {
namespace {

constexpr std::int64_t kDefaultMemoryMb = 2048;

// Flat-key prefix for one instance's settings, e.g. "instance.MyPack.".
std::string Prefix(const std::string& name) { return "instance." + name + "."; }

bool OverrideEnabled(const std::string& name) {
    return config::Config::Instance().GetBool((Prefix(name) + "override").c_str(), false);
}

// Java picker for one instance: a Select among the configured installs
// (ui::InstalledJavaPaths()), or a hint when none is configured. The chosen
// install is stored as its path string under instance.<name>.java_path (not an
// index, so it survives the list being reordered/pruned). `effective_path` is
// what the picker should currently show as selected -- the instance value when
// overriding, else the global selected Java -- so the row still reflects the
// effective Java even while the override switch is off (and the whole card is
// disabled).
void BuildInstanceJavaSelect(const std::string& java_key, bool override_on,
                             const std::string& effective_path) {
    AutoCard([&] {
        const std::vector<std::string> javas = ui::InstalledJavaPaths();
        if (javas.empty()) {
            ImGuiMD2::Text(ImGuiMD2::TextStyle::Body2, ui::i18n::Tr("instance.java.empty"));
            return;
        }

        std::vector<const char*> items;
        items.reserve(javas.size());
        for (const std::string& p : javas) items.push_back(p.c_str());

        int current = 0;
        for (std::size_t i = 0; i < javas.size(); ++i) {
            if (javas[i] == effective_path) {
                current = static_cast<int>(i);
                break;
            }
        }

        if (ImGuiMD2::Select(ui::i18n::Tr("instance.java.select"), &current, items.data(),
                             static_cast<int>(items.size())) &&
            override_on) {
            config::Config& cfg = config::Config::Instance();
            cfg.Set(java_key.c_str(), javas[static_cast<std::size_t>(current)]);
            cfg.Save();
        }
    });
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

std::string InstanceJavaPath(const std::string& name) {
    if (!OverrideEnabled(name)) return ui::SelectedJavaPath();
    const std::string picked =
        config::Config::Instance().GetString((Prefix(name) + "java_path").c_str(), "");
    // Empty (never picked, or the picked install was later removed and left the
    // key blank) falls back to the global selected Java rather than launching
    // with no runtime.
    return picked.empty() ? ui::SelectedJavaPath() : picked;
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
            cfg.Set((prefix + "java_path").c_str(), ui::SelectedJavaPath());
        }
        cfg.Save();
    }
    ImGuiMD2::Text(ImGuiMD2::TextStyle::Caption, ui::i18n::Tr("instance.override_desc"));
    ImGuiMD2::EndCard();

    // Java picker + memory + JVM args. When overriding they edit the instance
    // keys; otherwise they show the effective (global) values, disabled, so the
    // resolved settings stay visible.
    const std::string java_key = prefix + "java_path";
    const std::string mem_key =
        override_on ? (prefix + "memory_mb") : std::string("java.memory_mb");
    const std::string args_key =
        override_on ? (prefix + "jvm_args") : std::string("java.jvm_args");

    ImGui::Dummy(ImVec2(0.0f, 16.0f)); // matches frame.cpp's kCardGap between cards
    ImGui::BeginDisabled(!override_on);
    BuildInstanceJavaSelect(java_key, override_on, InstanceJavaPath(name));
    ImGui::Dummy(ImVec2(0.0f, 16.0f));
    ui::MemoryAndJvmArgsControls(mem_key.c_str(), args_key.c_str());
    ImGui::EndDisabled();
}

} // namespace ui
