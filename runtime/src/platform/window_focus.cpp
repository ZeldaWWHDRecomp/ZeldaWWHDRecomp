// window_focus.h: the game window in front (issue #406).
#include "window_focus.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace window_focus {
#ifdef WWHD_TEST_WINDOW_FOCUS
static SDL_Window* g_test_foreground = nullptr;
void test_set_os_foreground(SDL_Window* window) { g_test_foreground = window; }
#endif
bool os_foreground(SDL_Window* window) {
#ifdef WWHD_TEST_WINDOW_FOCUS
    if (g_test_foreground) return window == g_test_foreground;
#endif
#ifdef _WIN32
    if (!window) return false;
    void* hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    return hwnd && GetForegroundWindow() == static_cast<HWND>(hwnd);
#else
    (void)window;
    return false;
#endif
}

Front front(SDL_Window* tv, SDL_Window* gamepad) {
    if (!tv && !gamepad) return Front::NoWindows;
    SDL_Window* keyboard = SDL_GetKeyboardFocus();
    auto in_front = [&](SDL_Window* w) { return w && (keyboard == w || os_foreground(w)); };
    if (in_front(tv)) return Front::TV;
    if (in_front(gamepad)) return Front::GamePad;
    return Front::None;
}
}
