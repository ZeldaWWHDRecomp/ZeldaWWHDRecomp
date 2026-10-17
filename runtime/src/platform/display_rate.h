// SDL host: the TV window's display refresh rate for frame interpolation (display_rate.cpp)
#pragma once
struct SDL_Window;

namespace display_rate {
// main thread, about twice a second: reports the refresh rate of the TV window's display to
// interp::set_display_hz (Android: also requests the chosen cadence, at least 60 Hz)
void poll(SDL_Window* tv);
// Android surface replacement/resume; thread safe. Forces the next poll to reapply the request.
void surface_changed();
}  // namespace display_rate
