#pragma once

namespace platform {

class Window;

// Installs the borderless-with-native-chrome behaviour on Windows: strips the
// visual title bar while keeping the resize frame, DWM drop shadow and rounded
// corners. No-op on other platforms (this header is only referenced from the
// Windows build of window.cpp).
void install_win32_chrome(Window& window);

} // namespace platform
