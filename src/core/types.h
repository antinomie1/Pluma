#pragma once

#include <cstdint>

// Shared vocabulary types crossing the platform -> render thread boundary.
// Deliberately free of any GLFW/ImGui type so neither dependency leaks between
// modules: platform translates GLFW into these, render translates these into
// ImGui.
namespace core {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

enum class MouseButton : std::uint8_t { Left = 0, Right = 1, Middle = 2 };

// GLFW-independent key identity. Only the keys the UI actually needs are
// enumerated; anything else arrives as Unknown and is dropped.
enum class Key : std::uint16_t {
    Unknown = 0,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Space, Enter, Escape, Backspace, Tab, Delete, Insert,
    Left, Right, Up, Down, Home, End, PageUp, PageDown,
    LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt,
    LeftSuper, RightSuper,
    Minus, Equal, Comma, Period, Slash, Semicolon, Apostrophe,
    LeftBracket, RightBracket, Backslash, GraveAccent,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
};

// A single normalised input event. Kept trivially copyable (plain fields, no
// std::variant) so the queue stays cheap and lock-friendly. Only the fields
// relevant to `type` are meaningful.
struct InputEvent {
    enum class Type : std::uint8_t {
        MouseMove,   // x, y = cursor position (logical/window coords)
        MouseButton, // button, pressed
        Scroll,      // x = horizontal delta, y = vertical delta
        Key,         // key, pressed
        Char,        // codepoint
    };

    Type type{};
    float x = 0.0f;
    float y = 0.0f;
    MouseButton button{};
    bool pressed = false;
    Key key = Key::Unknown;
    unsigned int codepoint = 0;
};

// Latest window geometry/state, published by platform and snapshotted by the
// render thread each frame.
struct WindowMetrics {
    int width = 0;      // logical window size
    int height = 0;
    int fb_width = 0;   // framebuffer size in pixels
    int fb_height = 0;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    bool focused = true;
    bool iconified = false;
};

} // namespace core
