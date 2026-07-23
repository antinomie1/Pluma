#include "render/renderer.h"

#include "config/config.h"
#include "core/event_queue.h"
#include "core/types.h"
#include "logic/engine.h"
#include "platform/window.h"
#include "ui/frame.h"
#include "ui/game_settings.h"
#include "ui/i18n.h"
#include "ui/java_settings.h"
#include "ui/theme.h"

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui_md2/assets.h>
#include <imgui_md2/imgui_md2.h>

#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace render {
namespace {

ImGuiKey to_imgui_key(core::Key key) {
    using K = core::Key;
    if (key >= K::A && key <= K::Z) {
        return static_cast<ImGuiKey>(ImGuiKey_A + (static_cast<int>(key) - static_cast<int>(K::A)));
    }
    if (key >= K::Num0 && key <= K::Num9) {
        return static_cast<ImGuiKey>(ImGuiKey_0 + (static_cast<int>(key) - static_cast<int>(K::Num0)));
    }
    if (key >= K::F1 && key <= K::F12) {
        return static_cast<ImGuiKey>(ImGuiKey_F1 + (static_cast<int>(key) - static_cast<int>(K::F1)));
    }
    switch (key) {
        case K::Space: return ImGuiKey_Space;
        case K::Enter: return ImGuiKey_Enter;
        case K::Escape: return ImGuiKey_Escape;
        case K::Backspace: return ImGuiKey_Backspace;
        case K::Tab: return ImGuiKey_Tab;
        case K::Delete: return ImGuiKey_Delete;
        case K::Insert: return ImGuiKey_Insert;
        case K::Left: return ImGuiKey_LeftArrow;
        case K::Right: return ImGuiKey_RightArrow;
        case K::Up: return ImGuiKey_UpArrow;
        case K::Down: return ImGuiKey_DownArrow;
        case K::Home: return ImGuiKey_Home;
        case K::End: return ImGuiKey_End;
        case K::PageUp: return ImGuiKey_PageUp;
        case K::PageDown: return ImGuiKey_PageDown;
        case K::LeftShift: return ImGuiKey_LeftShift;
        case K::RightShift: return ImGuiKey_RightShift;
        case K::LeftCtrl: return ImGuiKey_LeftCtrl;
        case K::RightCtrl: return ImGuiKey_RightCtrl;
        case K::LeftAlt: return ImGuiKey_LeftAlt;
        case K::RightAlt: return ImGuiKey_RightAlt;
        case K::LeftSuper: return ImGuiKey_LeftSuper;
        case K::RightSuper: return ImGuiKey_RightSuper;
        case K::Minus: return ImGuiKey_Minus;
        case K::Equal: return ImGuiKey_Equal;
        case K::Comma: return ImGuiKey_Comma;
        case K::Period: return ImGuiKey_Period;
        case K::Slash: return ImGuiKey_Slash;
        case K::Semicolon: return ImGuiKey_Semicolon;
        case K::Apostrophe: return ImGuiKey_Apostrophe;
        case K::LeftBracket: return ImGuiKey_LeftBracket;
        case K::RightBracket: return ImGuiKey_RightBracket;
        case K::Backslash: return ImGuiKey_Backslash;
        case K::GraveAccent: return ImGuiKey_GraveAccent;
        default: return ImGuiKey_None;
    }
}

int imgui_mouse_button(core::MouseButton button) {
    switch (button) {
        case core::MouseButton::Right: return 1;
        case core::MouseButton::Middle: return 2;
        case core::MouseButton::Left:
        default: return 0;
    }
}

void feed_input(ImGuiIO& io, const std::vector<core::InputEvent>& events) {
    for (const core::InputEvent& e : events) {
        switch (e.type) {
            case core::InputEvent::Type::MouseMove:
                io.AddMousePosEvent(e.x, e.y);
                break;
            case core::InputEvent::Type::MouseButton:
                io.AddMouseButtonEvent(imgui_mouse_button(e.button), e.pressed);
                break;
            case core::InputEvent::Type::Scroll:
                io.AddMouseWheelEvent(e.x, e.y);
                break;
            case core::InputEvent::Type::Key: {
                const ImGuiKey key = to_imgui_key(e.key);
                if (key != ImGuiKey_None) {
                    io.AddKeyEvent(key, e.pressed);
                }
                break;
            }
            case core::InputEvent::Type::Char:
                io.AddInputCharacter(e.codepoint);
                break;
        }
    }
}

} // namespace

Renderer::Renderer(platform::Window& window, logic::Engine& engine)
    : window_(window), engine_(engine) {}

Renderer::~Renderer() { stop(); }

void Renderer::start() {
    if (running_.exchange(true)) {
        return;
    }
    // Lets platform drive a synchronous repaint per WM_SIZE tick during a live
    // resize without platform depending on render (see window.h).
    window_.setResizeRenderCallback([this] { renderFrame(); });
    thread_ = std::thread(&Renderer::run, this);
}

void Renderer::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    window_.renderGate().stop();      // release the thread if it is parked (minimised)
    window_.resizePauseGate().stop(); // release the thread if it is parked (mid-resize)
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Renderer::run() {
    using clock = std::chrono::steady_clock;

    window_.makeContextCurrent();
    window_.setSwapInterval(1); // VSync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; // cursor shapes deferred
    io.BackendPlatformName = "pluma";

    ImGuiMD2::CreateContext();

    // Load persisted settings before anything else derives from them: an
    // absent/corrupt config.json just leaves the store empty, so every
    // GetXxx() below falls back to its `def` and the app starts on defaults
    // (see config::Config::Load()).
    config::Config::Instance().Load("config.json");
    const std::string configured_lang = config::Config::Instance().GetString("language", "");
    ui::i18n::Initialize(configured_lang.empty() ? nullptr : configured_lang.c_str());

    // First-launch (or nothing-configured-yet) Java auto-discovery, scanning
    // PATH/JAVA_HOME. Render-thread-exclusive, same as the config/theme/i18n
    // reads directly above -- see ui::EnsureJavaAutoDiscovered()'s comment.
    ui::EnsureJavaAutoDiscovered();
    // Same idea for the game (".minecraft") directory list -- seeds it with
    // <cwd>/.minecraft on first launch. See ui::EnsureGameDirSeeded()'s
    // comment.
    ui::EnsureGameDirSeeded();
    downloads_.start();

    const bool configured_dark = config::Config::Instance().GetBool("theme.dark", false);
    const int configured_accent = static_cast<int>(
        config::Config::Instance().GetInt("theme.accent", static_cast<int64_t>(ImGuiMD2::Swatch::Blue)));
    ImGuiMD2::Theme theme =
        ui::MakeTheme(configured_dark, static_cast<ImGuiMD2::Swatch>(configured_accent));
    std::string font_error;
    // Scale the whole MD2 type scale up so text reads larger and heavier (the
    // embedded Medium weight is the boldest available).
    ImGuiMD2::FontLoadOptions font_options;
    font_options.scale = 1.3f;

    // Extend glyph coverage with a runtime-discovered system font (e.g. CJK)
    // on top of the bundled Roboto. Absence is not an error: the app just
    // keeps whatever coverage Roboto already provides.
    cjk_font_ = platform::LoadCjkSystemFont();
    if (cjk_font_.valid()) {
        ImGuiMD2::FontMerge cjk;
        cjk.data = cjk_font_.data();
        cjk.data_size = static_cast<int>(cjk_font_.size());
        cjk.glyph_ranges = nullptr; // 1.92 rasterizes lazily; no range to pre-declare
        font_options.merge_fonts.push_back(cjk);
    }

    ImGuiMD2::LoadBundledFonts(*io.Fonts, theme.fonts, "", font_options, &font_error);
    ImGuiMD2::SetTheme(theme);

    // Real-bold nav font: embedded Roboto-Bold merged with a runtime-discovered
    // bold CJK system face (if any), at the Button text style's pixel size.
    // Absence of a bold CJK face is not an error -- the app_state_.nav_bold_font
    // fallback chain (see AddBoldFont) and, failing that, faux-bold in
    // frame.cpp's NavButton, both degrade gracefully.
    cjk_bold_font_ = platform::LoadCjkSystemFontBold();
    std::vector<ImGuiMD2::FontMerge> bold_merges;
    if (cjk_bold_font_.valid()) {
        ImGuiMD2::FontMerge cjk_bold;
        cjk_bold.data = cjk_bold_font_.data();
        cjk_bold.data_size = static_cast<int>(cjk_bold_font_.size());
        cjk_bold.glyph_ranges = nullptr;
        bold_merges.push_back(cjk_bold);
    }
    const float btn_px = ImGuiMD2::Typography(ImGuiMD2::TextStyle::Button).size * font_options.scale;
    app_state_.nav_bold_font = ImGuiMD2::AddBoldFont(*io.Fonts, btn_px, nullptr, bold_merges);

    ImGui_ImplOpenGL3_Init("#version 150");

    previous_frame_time_ = clock::now();

    while (running_.load(std::memory_order_relaxed)) {
        core::WindowMetrics metrics = window_.metricsSnapshot();

        // Park with ~0 CPU/GPU while minimised; wake on restore or shutdown.
        if (metrics.iconified) {
            if (!window_.renderGate().wait()) {
                break;
            }
            previous_frame_time_ = clock::now();
            continue;
        }

        // Hand the GL context to the main thread for the duration of a live
        // border-drag: release it, signal beginInteractiveResize() that it's
        // safe to acquire, then park until endInteractiveResize() reopens the
        // gate. The main thread drives its own synchronous repaint per
        // WM_SIZE tick via the renderResizeTick() callback in the meantime
        // (see win32_chrome.cpp).
        if (window_.resizePauseRequested()) {
            window_.detachContext();
            window_.resizeContextReadyGate().open();
            if (!window_.resizePauseGate().wait()) {
                break;
            }
            window_.makeContextCurrent();
            previous_frame_time_ = clock::now();
            continue;
        }

        const auto frame_start = renderFrame();

        // FPS cap on top of VSync: sleep out any remaining time in the frame
        // budget. With VSync this is usually a no-op; it enforces the ceiling
        // when VSync is disabled or the display refresh is higher.
        if (target_fps_ > 0) {
            const auto budget = std::chrono::duration<double>(1.0 / target_fps_);
            const auto target = frame_start + std::chrono::duration_cast<clock::duration>(budget);
            std::this_thread::sleep_until(target);
        }
    }

    downloads_.stop();

    ImGui_ImplOpenGL3_Shutdown();
    ImGuiMD2::DestroyContext();
    ImGui::DestroyContext();
    window_.detachContext();
}

std::chrono::steady_clock::time_point Renderer::renderFrame() {
    using clock = std::chrono::steady_clock;

    core::WindowMetrics metrics = window_.metricsSnapshot();
    const auto frame_start = clock::now();

    window_.events().drain(pending_events_);
    ImGuiIO& io = ImGui::GetIO();
    feed_input(io, pending_events_);

    io.DisplaySize = ImVec2(std::floor(static_cast<float>(metrics.width)),
                            std::floor(static_cast<float>(metrics.height)));
    if (metrics.width > 0 && metrics.height > 0) {
        io.DisplayFramebufferScale =
            ImVec2(static_cast<float>(metrics.fb_width) / static_cast<float>(metrics.width),
                   static_cast<float>(metrics.fb_height) / static_cast<float>(metrics.height));
    }
    float dt = std::chrono::duration<float>(frame_start - previous_frame_time_).count();
    previous_frame_time_ = frame_start;
    io.DeltaTime = dt > 0.0f ? dt : 1.0f / 60.0f;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    ImGuiMD2::NewFrame();

    const logic::State logic_state = engine_.snapshot();
    ui::BuildFrame(window_, logic_state, app_state_, downloads_);

    ImGui::Render();

    glViewport(0, 0, metrics.fb_width, metrics.fb_height);
    const ImGuiMD2::Color clear = ImGuiMD2::clear_color();
    glClearColor(clear.r, clear.g, clear.b, clear.a);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    window_.swapBuffers();

    return frame_start;
}

} // namespace render
