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

}  // namespace mods::coop::world
