// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
namespace mods {
inline bool valid_fast_forward_button(uint32_t b) {
    constexpr uint32_t allowed = 0x40000 | 0x20000 | 0x20 | 0x10 | 0x80 | 0x40;
    return b && !(b & (b - 1)) && (b & allowed);
}
inline bool fast_forward_gate(bool enabled, uint8_t event, bool play, bool menu,
                              bool blocked, uint32_t held, uint32_t button) {
    return enabled && event >= 1 && event <= 3 && play && !menu && !blocked && (held & button);
}
bool fast_forward();
void set_fast_forward(bool on);
unsigned fast_forward_rate();
void set_fast_forward_rate(unsigned rate);
uint32_t fast_forward_button();
void set_fast_forward_button(uint32_t button);
bool fast_forward_mute();
void set_fast_forward_mute(bool mute);
bool fast_forward_active();
void fast_forward_input(uint32_t& buttons); // consume only the boost button, during events
void fast_forward_service();               // game frame boundary
void fast_forward_reset();                 // release required after save/load, overlay, focus loss
}
