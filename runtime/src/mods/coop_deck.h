// SPDX-License-Identifier: MPL-2.0
// Online co-op: standing on a friend's boat (GAPS C16).
//
// The King of Red Lions has no deck in the game: Link sits in it, and nothing stands on it. A friend's boat here is
// only their puppet's models, so a deck is made for it: a flat body of the game's own moving background collision
// (dBgW, as the mods' world objects make theirs, story_world_collision_native.h) at the boat as it is shown, moved
// with it each step. Link standing on it is carried as on any moving platform (dBgS_MoveBGProc_TypicalRotY).
#pragma once

#include <cstdint>

struct Cpu;

namespace mods::coop::deck {

constexpr int kDecks = 4;  // one per party number (a party has up to 4)

// a friend's boat as this game shows it this step (world place, heading); owner: their party number
struct Boat { bool on = false; float pos[3] = {}; int16_t yaw = 0; int8_t room = 0; };

// each step, after the actors ran: the decks made, moved to the boats, the others taken away (own_ship: this game's
// own boat, the decks' owner in the collision; 0 without one: no decks)
void update(Cpu* c, const Boat boats[kDecks], uint32_t own_ship);
// just before the actors run: each deck moved to its place (Link on it carried)
void move(Cpu* c);
// the sea's height at x, z in this game (its own waves); false off the sea
bool water(Cpu* c, float x, float z, float& y);
// all decks taken away (the session off, another stage)
void clear(Cpu* c);
// the party number whose deck Link stands on, or -1
int standing_on(uint32_t link);
// that deck's place as shown this step
bool frame(int owner, float pos[3], int16_t& yaw);
// Link put onto that deck, behind its middle (a "Go to" a sailing friend); false without that deck
bool hop(uint32_t link, int owner);

}  // namespace mods::coop::deck
