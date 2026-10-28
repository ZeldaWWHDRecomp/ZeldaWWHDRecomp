// SPDX-License-Identifier: MPL-2.0
// The item ring's pictures: the game's own item icons (BtnItemIcon_00 Icon128_NN^l in the player's 2D pack,
// read by game_layouts.h on the player's machine; nothing of the game ships), as HUD images of the ring's
// owner. The icon frame of an item is the game's own table (hdMenuItem itemForSlot's: 57 entries of 16
// bytes, item number and frame, USA 100E7AB8; story_lyt_screen.cpp).
#pragma once
#include <cstdint>

#include "guest_hud.h"

struct Cpu;

namespace item_ring_icons {
// The HUD image of `item`'s icon for `owner` (game thread), 0 while it has none (the ring draws its
// name instead). Decoded once per item; again after a state load drops the HUD images.
guestmods::hud::Handle handle(Cpu* c, uint8_t item, const char* owner);
// Before a ring's handles are taken: lets the cached images of other items go when the ones of `items`
// might not fit the HUD's image quota
void keep_only(const uint8_t* items, int n, const char* owner);
// the icon frame of an item in the game's table, -1 if it has none
int frame_of(uint8_t item);
}  // namespace item_ring_icons
