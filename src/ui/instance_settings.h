#pragma once

// Per-instance (per-game) settings, overriding the global settings for one
// installed instance. Deliberately a subset of the global Settings page: a Java
// picker (choosing among the same configured installs Settings > Java manages,
// but not editing that list here) plus the same JVM memory / JVM-arguments
// controls, gated behind an "enable custom settings" switch. With the switch
// off the instance follows the global values; with it on, the values are stored
// per instance under config's flat keys
// instance.<name>.override / .java_path / .memory_mb / .jvm_args.
//
// Threading: render-thread-exclusive, same as the other ui settings modules --
// BuildInstanceSettings() is only called from inside ui::BuildFrame().
#include <cstdint>
#include <string>

namespace ui {

// Renders the per-instance settings body for `instance_name` (the override
// switch + the shared memory/JVM-args controls).
void BuildInstanceSettings(const std::string& instance_name);

// Resolves the memory (MB) / JVM args the launch flow should use for
// `instance_name`: the per-instance override values when its override switch
// is on, otherwise the global java.memory_mb / java.jvm_args.
std::int64_t InstanceMemoryMb(const std::string& instance_name);
std::string InstanceJvmArgs(const std::string& instance_name);

// Resolves the Java executable the launch flow should use for `instance_name`:
// the per-instance java.path when its override switch is on (falling back to
// the global selected Java if the instance value is unset/empty), otherwise the
// global ui::SelectedJavaPath().
std::string InstanceJavaPath(const std::string& instance_name);

} // namespace ui
