#include "logic/engine.h"
#include "platform/window.h"
#include "render/renderer.h"

// Pluma entry point. Thread layout:
//   - main thread  : owns GLFW, blocks in waitEvents() (~0 CPU when idle).
//   - render thread: owns the GL + ImGui context and the frame loop.
//   - logic thread : ticks business logic independently.
int main() {
    platform::Window window("Pluma", 800, 450);

    logic::Engine engine;
    engine.start();

    render::Renderer renderer(window, engine);
    renderer.setTargetFps(60);
    renderer.start();

    while (!window.shouldClose()) {
        window.waitEvents();     // blocks until an event or postEmptyEvent()
        window.processPending(); // apply cross-thread window ops (minimise, ...)
    }

    renderer.stop();
    engine.stop();
    return 0;
}
