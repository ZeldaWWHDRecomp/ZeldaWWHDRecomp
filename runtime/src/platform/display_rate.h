// SDL host: the TV window's display refresh rate for frame interpolation (display_rate.cpp)
#pragma once
struct SDL_Window;

namespace display_rate {
// main thread, about twice a second: reports the refresh rate of the TV window's display to
// interp::set_display_hz (Android: also asks for a fast display mode at 120/240 fps)
void poll(SDL_Window* tv);
}  // namespace display_rate
