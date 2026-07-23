#pragma once

#include "core/types.h"
#include "net/download_manager.h"
#include "platform/system_fonts.h"
#include "ui/app_state.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace platform {
class Window;
class GameMonitor;
}

namespace render {

// Owns the render thread: the OpenGL context, the ImGui context, and the frame
// loop. Input is drained from the platform event queue and translated to ImGui
// here, so ImGui is only ever touched on this thread.
class Renderer {
public:
    Renderer(platform::Window& window, platform::GameMonitor& monitor);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void start();
    void stop();

    void setTargetFps(int fps) { target_fps_ = fps; }

private:
    void run();
    // Draws and presents exactly one frame: drain input, feed ImGui, build the
    // UI, render, swap. Called from run()'s normal loop and (via the callback
    // registered on window_ in start()) from the main thread's resize-tick
    // handler during a live border-drag -- the two callers are mutually
    // exclusive by construction (see platform::Window's resize gates), so no
    // extra locking is needed. Returns the frame's start timestamp so run()
    // can pace its FPS cap off it; the resize-tick caller ignores it (no cap
    // during a drag -- see run()).
    std::chrono::steady_clock::time_point renderFrame();

    platform::Window& window_;
    platform::GameMonitor& monitor_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    int target_fps_ = 60;

    // Frame-timing baseline for io.DeltaTime, and the input-drain scratch
    // buffer; both read/written only by whichever thread currently holds the
    // GL context (render thread normally, main thread during a resize tick).
    std::chrono::steady_clock::time_point previous_frame_time_{};
    std::vector<core::InputEvent> pending_events_;

    // Backs the CJK glyph merge (see run()). Referenced, not copied, by the
    // ImGui font atlas, so it must outlive ImGui::DestroyContext() in run() —
    // a member field guarantees that (Renderer outlives its own run()).
    platform::MappedFont cjk_font_;

    // Backs the bold-CJK merge used for the nav bar's real-bold font (see
    // run()). Same lifetime reasoning as cjk_font_.
    platform::MappedFont cjk_bold_font_;

    // Current nav page. Render-thread-exclusive, same reasoning as cjk_font_.
    ui::AppState app_state_;

    // Minecraft download subsystem: owns its own worker/publish threads (see
    // net::DownloadManager), started/stopped alongside the render thread's
    // own lifetime in run(). Threaded through ui::BuildFrame each frame, same
    // as app_state_ / the game-monitor snapshot.
    net::DownloadManager downloads_;
};

} // namespace render
