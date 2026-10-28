#pragma once
// The game's DSP overload check (JASDriver::updateDSP, USA 028137E0) and why the port turns it off.
//
// The game mixes its sound itself (JAudio's virtual DSP on its audio thread). updateDSP times each
// DSP sub-frame with OSGetTick and, when one took longer than the gap before the frame's first
// sub-frame / 1.1, prints "kill DSP channel" and stops a channel (f_02815B40): DSP overload on the
// console. Here the audio thread is a host thread: a late audio frame (the host was busy, the game
// core was taken, a stall) followed by catch-up frames reads as overload, and looping ambience
// (waves, wind) stops until the game starts it again. Mixing on the host has no such overload.
//
// The ratio 1.1 is a float constant only updateDSP reads (USA 10170030, the lfs at 02813870); set
// to 0 the check never fires (sub-frame gap / current >= 0 always). A save state restores the old
// constant, so the AX frame puts the 0 back each time it finds 1.1 there.
// WWHD_DSP_KILL=1 keeps the game's own check (the old behaviour).
// Idea and address from the GreenNaugahyde Android fork (MPL-2.0, its issue #23)
// and SwitchWakerHD (thanks @Pulpparty).
#include <cstdint>

namespace dsp_guard {

// (the address is written in ax.cpp, inside GD(), which maps it for the EU build)
constexpr uint32_t kKillRatioBits = 0x3F8CCCCD;  // 1.1f
constexpr uint32_t kNeverKillBits = 0;           // 0.0f

// The word to store at the constant, or none (false): only the game's exact 1.1 is replaced, so a
// build whose data differs there is never touched.
inline bool replacement(uint32_t current, bool game_check, uint32_t& out) {
    if (game_check || current != kKillRatioBits) return false;
    out = kNeverKillBits;
    return true;
}

}  // namespace dsp_guard
