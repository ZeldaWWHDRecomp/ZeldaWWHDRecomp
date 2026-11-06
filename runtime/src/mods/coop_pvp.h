// SPDX-License-Identifier: MPL-2.0
// Online co-op, PvP at sea (GAPS C28; docs/online-coop.md "PvP at sea"). Through a server whose pvp.sea is opt-in (each
// player's toggle, on by default) or on, players at sea can fight: the boat's cannon, arrows, bombs, the boomerang and
// the sword. The other players' puppets get hit areas in the game's own collision checks (an actor-less dCcD_Cyl each,
// as the game allows: dCcD_GStts NoActor), which take only those weapons; a hit by one of this game's own goes to the
// server, which checks it (both on, neither in a safe zone or protected, distance, rate, blocks, parties) and passes it
// to the victim, whose game checks what it can see and takes it as a monster's blow. A blow that would take the last
// heart is a knockout instead: no game over, full hearts, the server's share of the wallet gone, back at the nearest
// safe zone's edge, protected. Party members fight only by the party's friendly fire (coop_world's, as before).
#pragma once
#include <cstdint>

struct Cpu;

namespace mods::coop::pvp {

void before_execute(Cpu* c);                       // before the actors' executes: last step's hits read, a hit put on Link
void step(Cpu* c, uint32_t link, bool settled);    // after them, in play: the session's PvP, this step's hit areas set
// this game's boat fired its cannon (coop_world): its ball, where and which way it went, its speeds
void fired(uint32_t ball, const float pos[3], int16_t yaw, float speed_f, float speed_y);
// a ball or bomb made here only to show another player's (coop_world, coop_items): its blast hurts no player here
void shown_only(uint32_t bomb);
void register_overlay();                           // main (before the overlay seals): the marks over PvP players

}  // namespace mods::coop::pvp
