// SPDX-License-Identifier: MPL-2.0
// From Galvakron/ZeldaWWHDRecompMP 8a4400d (MPL-2.0), co-op work by AsadSi and Galvakron; adapted to this port.
//
// Online co-op, the monsters placed in the places the players are in (docs/multiplayer.md, "All monsters shared"): each
// place's are run by one game, its owner (coop_sync.h's owner()); the others' copies are put where the owner's are, with
// its health, a hit on one there going to the owner and a kill to everyone. The sea events' monsters, which the host's
// mods make, are extras.cpp's own.
#pragma once
#include <cstdint>
#include <string>

#include "coop_net.h"

struct Cpu;

namespace mods::coop::monsters {

// each step in play, after the actors' executes (extras.cpp's coop_world): this game's monsters out, the others' put in
void step(Cpu* c, uint32_t link);
// another player's kMonHit or kMonGone
void message(Cpu* c, net::Msg type, const std::string& body);
// `actor` is a placed monster this game runs (its execute goes for the nearest player: extras.cpp's stand-in)
bool runs(uint32_t actor);
// any placed monster in a place someone owns: before() and after() go around its execute
bool takes(uint32_t actor);
// around the execute (fopAc_Execute): another player's hit or kill put on it; the collider it takes hits on
void before(uint32_t actor);
void after(Cpu* c, uint32_t actor);
// the placed monster (set id, room) of this stage was killed while a player was in it (dSv_info_c::isActor: not made)
bool killed(int set, int room);
// `actor` is one of ours another player killed: its drop was theirs (fopAcM_createDisappear)
bool dropless(uint32_t actor);
// (a guest) the host's kDead: who was killed where
void dead_from(const uint8_t* d, size_t n);

}  // namespace mods::coop::monsters
