#pragma once

#include "core/event_queue.h"
#include "core/sync.h"
#include "core/types.h"

#include <atomic>
#include <string>

// platform is the ONLY module that includes <GLFW/glfw3.h>. This header is
// deliberately GLFW-free: the native handle is exposed as an opaque void* so
// downstream modules (render, ui) never pull in GLFW.
namespace platform {

class Window {
public:
    Window(const std::string& title, int width, int height);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // --- main thread only -------------------------------------------------
    // Blocks with ~0 CPU until an event arrives; woken by input, resize, or a
    // cross-thread postEmptyEvent().
    void waitEvents();
    // Applies window operations requested from other threads (minimise, ...).
    void processPending();
    bool shouldClose() const;

    // --- render thread only (GLFW context ops are thread-safe) ------------
    void makeContextCurrent();
    void detachContext();
    void swapBuffers();
    void setSwapInterval(int interval);

    // --- any thread -------------------------------------------------------
    // Requested here, actually performed on the main thread in processPending().
    void minimize();
    void close();

    core::EventQueue& events() { return events_; }
    core::Gate& renderGate() { return render_gate_; }
    core::WindowMetrics metricsSnapshot() const { return metrics_.load(); }

    // Caption/drag region hint published by the UI each frame, consumed by the
    // Windows native hit-test. Values are in logical units.
    void setCaptionRegion(float height, float right_exclude);
    float captionHeight() const { return caption_height_.load(std::memory_order_relaxed); }
    float captionRightExclude() const { return caption_right_exclude_.load(std::memory_order_relaxed); }

    // Native GLFWwindow* as an opaque pointer (used by win32 chrome only).
    void* handle() const { return handle_; }

private:
    void* handle_ = nullptr;

    core::EventQueue events_;
    core::SharedValue<core::WindowMetrics> metrics_;
    core::Gate render_gate_;

    std::atomic<bool> close_requested_{false};
    std::atomic<bool> minimize_requested_{false};
    std::atomic<float> caption_height_{0.0f};
    std::atomic<float> caption_right_exclude_{0.0f};
};

} // namespace platform
