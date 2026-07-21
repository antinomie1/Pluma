#pragma once

namespace platform {
class Window;
}

namespace logic {
struct State;
}

namespace ui {

// Builds one frame of UI between ImGui::NewFrame() and ImGui::Render(). Pure
// ImGui/imgui-md2 (no GL, no direct GLFW); window controls route through
// platform::Window. Called on the render thread only.
void BuildFrame(platform::Window& window, const logic::State& logic_state);

} // namespace ui
