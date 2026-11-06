#pragma once
// Which of the game's own windows is in front, and what that lets through (issue #406).
//
// The game has two windows of its own on desktop: the TV window and the separate GamePad window.
// Controllers drive the game while either of them is the focused window; with Controls >
// "Controller input in the background" on, also while another app is in front. Keyboard keys and
// mouse buttons are not decided here: SDL only sends them to a focused game window, and they are
// released whenever a game window loses the focus (input_sdl.cpp, mouse_sdl.cpp).
//
// SDL's own controller filter (SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS off) follows SDL's keyboard
// focus, which on Windows can stay empty after the focus moves between two windows of the same
// program (SDL_windowsevents.c, WIN_UpdateFocus). The desktop hosts therefore let SDL read the
// controllers always and decide here, from the window system's own answer.
#include <SDL3/SDL.h>

namespace window_focus {
// None: no game window has the focus (another app is in front). NoWindows: the host has no game window
// at all (headless tools and tests; SDL's own filter lets controllers through then too).
enum class Front { None, TV, GamePad, NoWindows };

// controllers reach the game (buttons, sticks, motion sensors, rumble)
constexpr bool controllers(Front front, bool background_option) { return front != Front::None || background_option; }
// keyboard and mouse: only while a game window has the focus, whatever the background option says
constexpr bool keyboard_and_mouse(Front front) { return front == Front::TV || front == Front::GamePad; }

// The game window in front: SDL's keyboard focus, or (Windows) the window system's foreground window.
// Other SDL windows than these two are not the game's. Main thread.
Front front(SDL_Window* tv, SDL_Window* gamepad);
// Windows: the window system's foreground window is this one (its HWND); false elsewhere.
bool os_foreground(SDL_Window* window);
#ifdef WWHD_TEST_WINDOW_FOCUS
// input_sdl_test: stands for the window system's foreground window (Windows' GetForegroundWindow) on any
// platform, with SDL's keyboard focus left as it is (it can stay empty on Windows: issue #406)
void test_set_os_foreground(SDL_Window* window);
#endif
}
