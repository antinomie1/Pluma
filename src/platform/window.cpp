#include "platform/window.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include "platform/win32_chrome.h"
#endif

namespace platform {
namespace {

GLFWwindow* as_glfw(void* handle) { return static_cast<GLFWwindow*>(handle); }

Window* self(GLFWwindow* window) {
    return static_cast<Window*>(glfwGetWindowUserPointer(window));
}

// Translate a GLFW key code into our GLFW-independent core::Key. Unmapped keys
// resolve to Unknown and are dropped by the render layer.
core::Key translate_key(int key) {
    using K = core::Key;
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
        return static_cast<K>(static_cast<int>(K::A) + (key - GLFW_KEY_A));
    }
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
        return static_cast<K>(static_cast<int>(K::Num0) + (key - GLFW_KEY_0));
    }
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) {
        return static_cast<K>(static_cast<int>(K::F1) + (key - GLFW_KEY_F1));
    }
    switch (key) {
        case GLFW_KEY_SPACE: return K::Space;
        case GLFW_KEY_ENTER: return K::Enter;
        case GLFW_KEY_KP_ENTER: return K::Enter;
        case GLFW_KEY_ESCAPE: return K::Escape;
        case GLFW_KEY_BACKSPACE: return K::Backspace;
        case GLFW_KEY_TAB: return K::Tab;
        case GLFW_KEY_DELETE: return K::Delete;
        case GLFW_KEY_INSERT: return K::Insert;
        case GLFW_KEY_LEFT: return K::Left;
        case GLFW_KEY_RIGHT: return K::Right;
        case GLFW_KEY_UP: return K::Up;
        case GLFW_KEY_DOWN: return K::Down;
        case GLFW_KEY_HOME: return K::Home;
        case GLFW_KEY_END: return K::End;
        case GLFW_KEY_PAGE_UP: return K::PageUp;
        case GLFW_KEY_PAGE_DOWN: return K::PageDown;
        case GLFW_KEY_LEFT_SHIFT: return K::LeftShift;
        case GLFW_KEY_RIGHT_SHIFT: return K::RightShift;
        case GLFW_KEY_LEFT_CONTROL: return K::LeftCtrl;
        case GLFW_KEY_RIGHT_CONTROL: return K::RightCtrl;
        case GLFW_KEY_LEFT_ALT: return K::LeftAlt;
        case GLFW_KEY_RIGHT_ALT: return K::RightAlt;
        case GLFW_KEY_LEFT_SUPER: return K::LeftSuper;
        case GLFW_KEY_RIGHT_SUPER: return K::RightSuper;
        case GLFW_KEY_MINUS: return K::Minus;
        case GLFW_KEY_EQUAL: return K::Equal;
        case GLFW_KEY_COMMA: return K::Comma;
        case GLFW_KEY_PERIOD: return K::Period;
        case GLFW_KEY_SLASH: return K::Slash;
        case GLFW_KEY_SEMICOLON: return K::Semicolon;
        case GLFW_KEY_APOSTROPHE: return K::Apostrophe;
        case GLFW_KEY_LEFT_BRACKET: return K::LeftBracket;
        case GLFW_KEY_RIGHT_BRACKET: return K::RightBracket;
        case GLFW_KEY_BACKSLASH: return K::Backslash;
        case GLFW_KEY_GRAVE_ACCENT: return K::GraveAccent;
        default: return K::Unknown;
    }
}

void refresh_sizes(GLFWwindow* window, core::SharedValue<core::WindowMetrics>& metrics) {
    int w = 0, h = 0, fw = 0, fh = 0;
    float sx = 1.0f, sy = 1.0f;
    glfwGetWindowSize(window, &w, &h);
    glfwGetFramebufferSize(window, &fw, &fh);
    glfwGetWindowContentScale(window, &sx, &sy);
    metrics.update([&](core::WindowMetrics& m) {
        m.width = w;
        m.height = h;
        m.fb_width = fw;
        m.fb_height = fh;
        m.scale_x = sx;
        m.scale_y = sy;
    });
}

} // namespace

Window::Window(const std::string& title, int width, int height) {
    if (glfwInit() != GLFW_TRUE) {
        std::fprintf(stderr, "[platform] glfwInit failed\n");
        std::abort();
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    // Keep native decorations/styles so DWM shadow, snapping and rounded
    // corners survive; the visual title bar is stripped by the win32 subclass.
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "[platform] glfwCreateWindow failed\n");
        glfwTerminate();
        std::abort();
    }
    // Below this, the settings master-detail rail/cards and app-bar controls
    // start overlapping/clipping instead of reflowing -- the UI has no
    // narrower layout to fall back to.
    glfwSetWindowSizeLimits(window, 700, 400, GLFW_DONT_CARE, GLFW_DONT_CARE);

    handle_ = window;
    glfwSetWindowUserPointer(window, this);

    core::WindowMetrics initial;
    initial.width = width;
    initial.height = height;
    metrics_.store(initial);
    refresh_sizes(window, metrics_);

    // Closed until the render thread actually releases the GL context (see
    // beginInteractiveResize()); starting open would let beginInteractiveResize()
    // race ahead of the render thread and grab the context while it's still in use.
    resize_context_ready_gate_.close();

    glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) {
        core::InputEvent e;
        e.type = core::InputEvent::Type::MouseMove;
        e.x = static_cast<float>(x);
        e.y = static_cast<float>(y);
        self(w)->events_.push(e);
    });
    glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int) {
        core::InputEvent e;
        e.type = core::InputEvent::Type::MouseButton;
        e.pressed = (action == GLFW_PRESS);
        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            e.button = core::MouseButton::Right;
        } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
            e.button = core::MouseButton::Middle;
        } else {
            e.button = core::MouseButton::Left;
        }
        self(w)->events_.push(e);
    });
    glfwSetScrollCallback(window, [](GLFWwindow* w, double dx, double dy) {
        core::InputEvent e;
        e.type = core::InputEvent::Type::Scroll;
        e.x = static_cast<float>(dx);
        e.y = static_cast<float>(dy);
        self(w)->events_.push(e);
    });
    glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int, int action, int) {
        if (action == GLFW_REPEAT) {
            return; // ImGui derives repeat from held state.
        }
        core::InputEvent e;
        e.type = core::InputEvent::Type::Key;
        e.pressed = (action == GLFW_PRESS);
        e.key = translate_key(key);
        if (e.key != core::Key::Unknown) {
            self(w)->events_.push(e);
        }
    });
    glfwSetCharCallback(window, [](GLFWwindow* w, unsigned int codepoint) {
        core::InputEvent e;
        e.type = core::InputEvent::Type::Char;
        e.codepoint = codepoint;
        self(w)->events_.push(e);
    });
    // glfwSetWindowSizeCallback deliberately not registered: the framebuffer
    // callback below fires 1:1 with it on Windows and refresh_sizes() already
    // reads both the window size and framebuffer size, so registering both
    // would just double-lock the metrics mutex per resize tick for no benefit.
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int, int) {
        refresh_sizes(w, self(w)->metrics_);
    });
    glfwSetWindowContentScaleCallback(window, [](GLFWwindow* w, float, float) {
        refresh_sizes(w, self(w)->metrics_);
    });
    glfwSetWindowFocusCallback(window, [](GLFWwindow* w, int focused) {
        self(w)->metrics_.update([&](core::WindowMetrics& m) { m.focused = (focused != 0); });
    });
    glfwSetWindowIconifyCallback(window, [](GLFWwindow* w, int iconified) {
        Window* self_window = self(w);
        const bool minimized = (iconified != 0);
        self_window->metrics_.update([&](core::WindowMetrics& m) { m.iconified = minimized; });
        if (minimized) {
            self_window->render_gate_.close(); // park the render thread
        } else {
            self_window->render_gate_.open();  // resume rendering
        }
    });
    glfwSetWindowCloseCallback(window, [](GLFWwindow* w) {
        self(w)->close_requested_.store(true);
    });

#ifdef _WIN32
    install_win32_chrome(*this);
#endif

    // The context is current on this (main) thread after creation; release it so
    // the render thread can take ownership.
    glfwMakeContextCurrent(nullptr);
}

Window::~Window() {
    render_gate_.stop();
    resize_pause_gate_.stop();
    resize_context_ready_gate_.stop();
    if (handle_ != nullptr) {
        glfwDestroyWindow(as_glfw(handle_));
        handle_ = nullptr;
    }
    glfwTerminate();
}

void Window::waitEvents() { glfwWaitEvents(); }

void Window::processPending() {
    if (minimize_requested_.exchange(false)) {
        glfwIconifyWindow(as_glfw(handle_));
    }
}

bool Window::shouldClose() const {
    return close_requested_.load() || glfwWindowShouldClose(as_glfw(handle_)) == GLFW_TRUE;
}

void Window::makeContextCurrent() { glfwMakeContextCurrent(as_glfw(handle_)); }
void Window::detachContext() { glfwMakeContextCurrent(nullptr); }
void Window::swapBuffers() { glfwSwapBuffers(as_glfw(handle_)); }
void Window::setSwapInterval(int interval) { glfwSwapInterval(interval); }

void Window::beginInteractiveResize() {
    resize_pause_requested_.store(true, std::memory_order_release);
    resize_pause_gate_.close();
    // Wait for the render thread to actually detach the context. If stop()
    // fires instead (shutdown racing a drag), bail without touching the
    // context -- the render thread is on its way out either way.
    if (!resize_context_ready_gate_.wait()) {
        return;
    }
    resize_context_ready_gate_.close(); // reset for the next resize cycle
    makeContextCurrent();
}

void Window::renderResizeTick() {
    if (resize_render_callback_) {
        resize_render_callback_();
    }
}

void Window::endInteractiveResize() {
    detachContext();
    resize_pause_requested_.store(false, std::memory_order_release);
    resize_pause_gate_.open();
}

void Window::setResizeRenderCallback(std::function<void()> callback) {
    resize_render_callback_ = std::move(callback);
}

void Window::minimize() {
    minimize_requested_.store(true);
    glfwPostEmptyEvent();
}

void Window::close() {
    close_requested_.store(true);
    glfwPostEmptyEvent();
}

void Window::setCaptionRegion(float height, float right_exclude) {
    caption_height_.store(height, std::memory_order_relaxed);
    caption_right_exclude_.store(right_exclude, std::memory_order_relaxed);
}

} // namespace platform
