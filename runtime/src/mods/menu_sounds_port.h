// SPDX-License-Identifier: MPL-2.0
// Game menu sounds in the Settings overlay: the port's side (mods/menu_sounds.cpp); the rules are
// overlay/menu_sounds.h.
#pragma once
#include "../overlay/menu_sounds.h"

struct Cpu;

namespace menu_sounds {
// the "Menu sounds" option (Audio tab; audioMenuSounds, WWHD_MENU_SOUNDS): on by default
bool option();
void set_option(bool on);
// render thread, once per overlay frame: what the overlay saw (f.time is set here)
void observe(Frame f);
// game thread, each pad read (VPADRead): plays a queued sound through the game
void service(Cpu* c);
}  // namespace menu_sounds
