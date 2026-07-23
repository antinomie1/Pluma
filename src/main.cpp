#include "platform/game_monitor.h"
#include "platform/window.h"
#include "render/renderer.h"

// Pluma entry point. Thread layout:
//   - main thread   : owns GLFW, blocks in waitEvents() (~0 CPU when idle).
//   - render thread : owns the GL + ImGui context and the frame loop.
//   - monitor thread: polls launched game processes (platform::GameMonitor).
int main() {
    platform::Window window("Pluma", 800, 450);

    platform::GameMonitor monitor;
    monitor.start();

    render::Renderer renderer(window, monitor);
    renderer.setTargetFps(60);
    renderer.start();

    while (!window.shouldClose()) {
        window.waitEvents();     // blocks until an event or postEmptyEvent()
        window.processPending(); // apply cross-thread window ops (minimise, ...)
    }

    renderer.stop();
    monitor.stop();
    return 0;
}
