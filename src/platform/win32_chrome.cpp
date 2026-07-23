#include "platform/win32_chrome.h"

#ifdef _WIN32

#include "platform/window.h"

// Require Windows 10 APIs (GetDpiForWindow, DWM corner preference).
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

// Borderless window that keeps native chrome. Technique: keep the standard
// window styles (so DWM composes a drop shadow, snapping and rounded corners),
// then strip the *visual* frame via WM_NCCALCSIZE and re-implement the resize
// borders + caption drag region in WM_NCHITTEST. Single-window application, so
// the subclass state is kept in file-static globals.
namespace platform {
namespace {

WNDPROC g_prev_proc = nullptr;
Window* g_window = nullptr;
// Tracks whether we're inside a WM_ENTERSIZEMOVE/WM_EXITSIZEMOVE span, so
// WM_SIZE only drives a synchronous resize-tick repaint during an actual
// live border-drag (not e.g. a programmatic resize or restore-from-minimize).
bool g_resizing = false;

UINT dpi_for(HWND hwnd) {
    const UINT dpi = GetDpiForWindow(hwnd);
    return dpi != 0 ? dpi : 96;
}

bool is_maximized(HWND hwnd) {
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    return GetWindowPlacement(hwnd, &placement) && placement.showCmd == SW_SHOWMAXIMIZED;
}

// A frameless window reports the same rect for window and client, which makes a
// maximised window spill over the taskbar. Clamp to the monitor work area.
void adjust_maximized(HWND hwnd, RECT& rect) {
    if (!is_maximized(hwnd)) {
        return;
    }
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
    if (monitor == nullptr) {
        return;
    }
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
        rect = info.rcWork;
    }
}

LRESULT hit_test(HWND hwnd, LPARAM lparam) {
    POINT cursor = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    ScreenToClient(hwnd, &cursor);

    RECT rc{};
    GetClientRect(hwnd, &rc);

    const UINT dpi = dpi_for(hwnd);
    const float scale = static_cast<float>(dpi) / 96.0f;
    const int border = MulDiv(8, dpi, 96);

    const bool left = cursor.x < border;
    const bool right = cursor.x >= rc.right - border;
    const bool top = cursor.y < border;
    const bool bottom = cursor.y >= rc.bottom - border;

    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;

    const int caption = static_cast<int>(g_window->captionHeight() * scale);
    const int exclude = static_cast<int>(g_window->captionRightExclude() * scale);
    if (cursor.y < caption && cursor.x < rc.right - exclude) {
        return HTCAPTION;
    }
    return HTCLIENT;
}

LRESULT CALLBACK chrome_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_NCCALCSIZE:
            if (wparam == TRUE) {
                auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
                adjust_maximized(hwnd, params->rgrc[0]);
                return 0; // client area == whole window; frame removed
            }
            break;
        case WM_NCHITTEST:
            return hit_test(hwnd, lparam);
        case WM_GETMINMAXINFO: {
            const LRESULT result = CallWindowProcW(g_prev_proc, hwnd, msg, wparam, lparam);
            const UINT dpi = dpi_for(hwnd);
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize.x = MulDiv(700, dpi, 96);
            info->ptMinTrackSize.y = MulDiv(400, dpi, 96);
            return result;
        }
        case WM_ENTERSIZEMOVE: {
            // Delegate first so GLFW's own handling runs, then hand the GL
            // context to the main thread for the duration of the drag (see
            // Window::beginInteractiveResize()).
            const LRESULT result = CallWindowProcW(g_prev_proc, hwnd, msg, wparam, lparam);
            g_resizing = true;
            g_window->beginInteractiveResize();
            return result;
        }
        case WM_SIZE: {
            // Delegate first so GLFW's size/framebuffer callbacks have already
            // refreshed metrics_, then, if this WM_SIZE is part of a live
            // border-drag, synchronously draw+present one frame so DWM never
            // has to stretch a stale backbuffer between the render thread's
            // (now-paused) swaps.
            const LRESULT result = CallWindowProcW(g_prev_proc, hwnd, msg, wparam, lparam);
            if (g_resizing) {
                g_window->renderResizeTick();
            }
            return result;
        }
        case WM_EXITSIZEMOVE: {
            const LRESULT result = CallWindowProcW(g_prev_proc, hwnd, msg, wparam, lparam);
            g_resizing = false;
            g_window->endInteractiveResize();
            return result;
        }
        default:
            break;
    }
    return CallWindowProcW(g_prev_proc, hwnd, msg, wparam, lparam);
}

} // namespace

void install_win32_chrome(Window& window) {
    HWND hwnd = glfwGetWin32Window(static_cast<GLFWwindow*>(window.handle()));
    if (hwnd == nullptr) {
        return;
    }
    g_window = &window;
    g_prev_proc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&chrome_proc)));

    // DWM drop shadow for a frameless window (a 1px extended frame is enough).
    MARGINS margins = {0, 0, 0, 1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    // Win11 rounded corners (DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_ROUND = 2).
    const DWORD corner = 2;
    DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));

    // Recompute the non-client area so WM_NCCALCSIZE takes effect now.
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

} // namespace platform

#endif // _WIN32
