#pragma once

// Per-instance (per-game) settings, overriding the global settings for one
// installed instance. Deliberately a subset of the global Settings page: the
// same JVM memory / JVM-arguments controls as Settings > Java (minus the Java
// installations list), gated behind an "enable custom settings" switch. With
// the switch off the instance follows the global values; with it on, the
// values are stored per instance under config's flat keys
// instance.<name>.override / .memory_mb / .jvm_args.
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

} // namespace ui
