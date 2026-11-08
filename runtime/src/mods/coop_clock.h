// SPDX-License-Identifier: MPL-2.0
// Online co-op: the party's time of day and sea weather follow the leader's (coop_clock.cpp). Each game's own where the
// game holds them (dungeons, houses, cutscenes, story nights and storms); players outside the party keep their own.
#pragma once
#include <cstddef>
#include <cstdint>

namespace mods::coop::clock {
void step(bool settled);                                            // each step in play (coop_world)
void message(int from, uint8_t type, const uint8_t* d, size_t n);  // net::kClock (the leader's), net::kClockJump (to it)
}  // namespace mods::coop::clock
