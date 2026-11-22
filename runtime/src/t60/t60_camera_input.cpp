// True 60 (decomp build only): the mouse / swipe camera's movement in dCamera_c::manualCamera at N steps per 30 Hz frame.
//
// mods/camera.cpp takes the mouse and swipe movement once per 30 Hz frame (filter_pad, at the frame's one live input
// read) and hands it, in degrees, to manualCamera's hooks (PORT_CAMERA_MANUAL_AT @02510448 / @025104A0: the yaw speed;
// PORT_CAMERA_RATE @0251053C: the pitch rate), which consume it: the first call takes the movement, later calls get 0.
// At 30 Hz manualCamera runs once a frame. Converted (true 60), it runs on every step of the frame and scales the
// stick's per-frame amounts by dt; the movement taken on the first step alone would then turn the camera by dt of it
// (half the mouse / swipe rate at N = 2) and the later steps not at all. Here, while the running 30 Hz frame has N > 1
// steps, the movement is latched by the frame on its first take and every take of the same frame gets the frame's
// amount: the x dt of the steps sums to it. An unconverted manualCamera (tick30 step only) takes it once, as before.
// N = 1: the take as before (the movement, then 0). Slot 0 the yaw (degrees right), 1 the pitch (degrees down).
#include <cstdint>

#include "t60/t60.h"

float t60_camera_frame_take(int slot, float* pending) {
    static uint32_t frame[2] = {~0u, ~0u};
    static float amount[2] = {0, 0};
    if (!t60::active()) {
        frame[slot] = ~0u;
        const float v = *pending;
        *pending = 0;
        return v;
    }
    if (frame[slot] != wwhd_t60_frame) {
        frame[slot] = wwhd_t60_frame;
        amount[slot] = *pending;
    }
    *pending = 0;
    return amount[slot];
}
