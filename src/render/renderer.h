#pragma once

#include "platform/system_fonts.h"
#include "ui/app_state.h"

#include <atomic>
#include <thread>

namespace platform {
class Window;
}

namespace logic {
class Engine;
}

namespace render {

// Owns the render thread: the OpenGL context, the ImGui context, and the frame
// loop. Input is drained from the platform event queue and translated to ImGui
// here, so ImGui is only ever touched on this thread.
class Renderer {
public:
    Renderer(platform::Window& window, logic::Engine& engine);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void start();
    void stop();

    void setTargetFps(int fps) { target_fps_ = fps; }

private:
    void run();

    platform::Window& window_;
    logic::Engine& engine_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    int target_fps_ = 60;

    // Backs the CJK glyph merge (see run()). Referenced, not copied, by the
    // ImGui font atlas, so it must outlive ImGui::DestroyContext() in run() —
    // a member field guarantees that (Renderer outlives its own run()).
    platform::MappedFont cjk_font_;

    // Backs the bold-CJK merge used for the nav bar's real-bold font (see
    // run()). Same lifetime reasoning as cjk_font_.
    platform::MappedFont cjk_bold_font_;

    // Current nav page. Render-thread-exclusive, same reasoning as cjk_font_.
    ui::AppState app_state_;
};

} // namespace render
