#pragma once
#include <SDL3/SDL.h>
namespace input {
// Main-thread calls from the SDL window/event loop.
void handle_event(const SDL_Event& event);
void update();
void set_prompt_window(SDL_Window* window);
void set_gamepad_window(SDL_Window* window);
// Before SDL_Init: on a Steam Deck (steam_deck), lets SDL see the built-in controller next to Steam
// Input's virtual pad, for its gyro only (motion/deck_gyro.h, issue #566; WWHD_DECK_GYRO=0: off)
void prepare_deck_gyro(bool steam_deck);
// While the game asks for text the window title shows the prompt (the frame-rate title waits).
bool text_prompt_active();
// Android: a touch device that belongs to a connected controller (GameSir and others register a
// virtual touch screen for their buttons); its touches are not the player's fingers
bool touch_from_controller(SDL_TouchID id);
}

// Stable physical key ID for an SDL scancode (or -1), shared with focused-window shortcuts.
namespace input { int physical_keycode(int scancode); }
