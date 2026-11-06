// SPDX-License-Identifier: MPL-2.0
// From Galvakron/ZeldaWWHDRecompMP 8a4400d (MPL-2.0), co-op work by AsadSi and Galvakron (its extras.cpp's
// co-op parts); adapted to this port.
//
// Online co-op, the game thread's entry (docs/online-coop.md): once per full logic step the session is polled (also
// at the title), and in play this game's pose goes out, the others are drawn, and the world's messages that act in the
// game are put in: the boat's cannon shots, the players' bombs, friendly fire; the players push each other. The fork's
// shared Tetra's ship, deck cannons, sea events and the sea monsters its own mods make are not part of this port (they
// belong to those mods).
#pragma once
#include <cstdint>

struct Cpu;

namespace mods::coop::world {

void step(Cpu* c);       // interp.cpp, after the actors' executes, full logic passes only
void before_execute(Cpu* c);   // interp.cpp, just before them: a friend's blow on this game's Link
// (C28) for coop_pvp: a boat's cannon ball made here (its speeds along its heading, gravity; 0 when not made); this
// game's Link put at a place; a blow on him (quarter hearts, the way it goes) at his next execute (never his last
// quarter heart: a knockout is coop_pvp's)
uint32_t spawn_ball(Cpu* c, uint32_t link, const float pos[3], const int16_t angle[3], float speed_f, float speed_y, float gravity);
void put_link_at(uint32_t link, const float at[3]);
void hurt(uint8_t quarters, int16_t way);

}  // namespace mods::coop::world
