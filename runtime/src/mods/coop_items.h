// SPDX-License-Identifier: MPL-2.0
// From Galvakron/ZeldaWWHDRecompMP 8a4400d (MPL-2.0), co-op work by AsadSi and Galvakron; adapted to this port.
//
// Online co-op, the players' items (docs/multiplayer.md): what Link makes as he uses an item is his. Its models, as it
// draws them, go with his pose (coop_pose.cpp), so the others' puppets hold, throw and fire them; a bomb of his that
// goes off goes off on the others' games too, as a real bomb.
#pragma once
#include <cstddef>
#include <cstdint>

struct Cpu;

namespace mods::coop::items {

void made(int16_t proc, uint32_t actor);  // extras.cpp's fopAcM_fastCreate hook, while Link executes
int16_t mine(uint32_t actor);             // one of them: its kind (0: not his)
void step(Cpu* c);                        // each step in play: his bombs that went off, to the others
// an actor made at a place in Link's process layer and room (fopAcM_fastCreate), as it is made here
uint32_t spawn(Cpu* c, uint32_t link, int16_t proc, uint32_t param, const float pos[3]);
void bombed(Cpu* c, uint32_t link, const uint8_t* d, size_t n);  // another player's bomb went off (net::kBombed)

}  // namespace mods::coop::items
