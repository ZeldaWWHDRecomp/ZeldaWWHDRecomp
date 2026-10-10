// Android on-screen controls (#88): the drawn A B X Y say which Wii U button they are, but the
// virtual pad (touch_pad.cpp) reaches the game through the controls mapping like any controller,
// and its face preset (#97) binds the Wii U A to the pad's east button (by position) or to its
// south button (by label). Each drawn face button therefore presses the pad button that the
// current mapping binds to its Wii U button, whatever the preset.
//
// Plain C++ (no SDL) so it can be unit-tested (runtime/tools/input_map_test.cpp).
#pragma once
#include <cstdint>

#include "../input_map.h"

namespace touch_face {

// SDL_GamepadButton numbers of the host pad inputs that are buttons (as input_sdl.cpp reads them);
// -1 for triggers and stick directions, which a button bit can't press
inline int sdl_button(int pad) {
    using namespace input_map;
    switch (pad) {
    case kPadA: return 0;        // SDL_GAMEPAD_BUTTON_SOUTH
    case kPadB: return 1;        // EAST
    case kPadX: return 2;        // WEST
    case kPadY: return 3;        // NORTH
    case kPadOptions: return 4;  // BACK
    case kPadHome: return 5;     // GUIDE
    case kPadMenu: return 6;     // START
    case kPadL3: return 7;       // LEFT_STICK
    case kPadR3: return 8;       // RIGHT_STICK
    case kPadLB: return 9;       // LEFT_SHOULDER
    case kPadRB: return 10;      // RIGHT_SHOULDER
    case kPadDUp: return 11;     // DPAD_UP
    case kPadDDown: return 12;   // DPAD_DOWN
    case kPadDLeft: return 13;   // DPAD_LEFT
    case kPadDRight: return 14;  // DPAD_RIGHT
    default: return -1;
    }
}

// drawn: bit 0..3 = the on-screen A, B, X, Y held. Returns the SDL_GamepadButton bits to press.
// A Wii U button bound to no pad button (unbound, or a trigger or stick in a custom mapping) is
// not pressed.
inline uint32_t buttons(uint32_t drawn, const input_map::Mapping& m) {
    static const int kWiiU[4] = {input_map::kA, input_map::kB, input_map::kX, input_map::kY};
    uint32_t out = 0;
    for (int i = 0; i < 4; i++) {
        if (!(drawn & (1u << i))) continue;
        const int b = sdl_button(m.pad[kWiiU[i]]);
        if (b >= 0) out |= 1u << b;
    }
    return out;
}

}  // namespace touch_face
