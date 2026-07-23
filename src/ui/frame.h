#pragma once

namespace platform {
class Window;
struct GameMonitorState;
class GameMonitor;
}

namespace net {
class DownloadManager;
}

namespace ui {

struct AppState;

// Builds one frame of UI between ImGui::NewFrame() and ImGui::Render(). Pure
// ImGui/imgui-md2 (no GL, no direct GLFW); window controls route through
// platform::Window. `monitor` is used only to hand off a launched game process
// for monitoring (GameMonitor::TrackGame). Called on the render thread only.
void BuildFrame(platform::Window& window, const platform::GameMonitorState& game_state,
                ui::AppState& app_state, net::DownloadManager& downloads,
                platform::GameMonitor& monitor);

} // namespace ui
