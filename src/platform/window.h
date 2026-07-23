#pragma once

#include "core/event_queue.h"
#include "core/sync.h"
#include "core/types.h"

#include <atomic>
#include <functional>
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

    // --- main thread only (interactive resize handoff) ---------------------
    // Driven by the win32 chrome subclass's WM_ENTERSIZEMOVE/WM_SIZE/
    // WM_EXITSIZEMOVE handling. During a live border-drag, the OS-owned modal
    // resize loop owns the main thread, so the render thread hands the GL
    // context over and the main thread synchronously draws+presents one frame
    // per WM_SIZE tick instead of leaving DWM to stretch a stale backbuffer.
    // beginInteractiveResize() blocks until the render thread has released the
    // context.
    void beginInteractiveResize();
    // Draws and presents exactly one frame on the calling (main) thread via
    // the callback registered with setResizeRenderCallback(). No-op if a
    // resize isn't active (callback unset).
    void renderResizeTick();
    void endInteractiveResize();
    // Registered by render::Renderer at start() time; kept as a std::function
    // so platform doesn't gain a dependency on render (see file header).
    void setResizeRenderCallback(std::function<void()> callback);

    // --- any thread -------------------------------------------------------
    // Requested here, actually performed on the main thread in processPending().
    void minimize();
    void close();

    core::EventQueue& events() { return events_; }
    core::Gate& renderGate() { return render_gate_; }
    // Parked/opened by the render thread around an interactive resize; see
    // beginInteractiveResize()/endInteractiveResize() above.
    core::Gate& resizePauseGate() { return resize_pause_gate_; }
    // Opened by the render thread once it has released the GL context, so
    // beginInteractiveResize() knows it's safe to acquire it on the main thread.
    core::Gate& resizeContextReadyGate() { return resize_context_ready_gate_; }
    bool resizePauseRequested() const {
        return resize_pause_requested_.load(std::memory_order_acquire);
    }
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
    core::Gate resize_pause_gate_;
    core::Gate resize_context_ready_gate_;
    std::function<void()> resize_render_callback_;

    std::atomic<bool> close_requested_{false};
    std::atomic<bool> minimize_requested_{false};
    std::atomic<bool> resize_pause_requested_{false};
    std::atomic<float> caption_height_{0.0f};
    std::atomic<float> caption_right_exclude_{0.0f};
};

} // namespace platform
