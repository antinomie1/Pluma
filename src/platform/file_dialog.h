#pragma once

#include <string>

// Native "pick a folder" dialog, backing Settings > Game's "add directory"
// button (src/ui/game_settings.cpp). Follows java_locator.h's convention:
// plain std::string return (empty = cancelled/no picker available), no RAII
// handle wrapper.
namespace platform {

// Opens a native folder-picker dialog. Returns the chosen absolute path, or
// "" if cancelled (or, on POSIX, if neither zenity nor kdialog is
// available). Blocking/modal, same threading expectation as
// PickJavaExecutable() -- safe to call from the render thread.
std::string PickDirectory();

} // namespace platform
