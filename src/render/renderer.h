#pragma once

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
};

} // namespace render
