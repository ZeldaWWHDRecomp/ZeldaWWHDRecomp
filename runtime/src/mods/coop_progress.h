// SPDX-License-Identifier: MPL-2.0
// From Galvakron/ZeldaWWHDRecompMP 8a4400d (MPL-2.0), co-op work by AsadSi and Galvakron; adapted to this port.
//
// Online co-op, the shared progress (docs/online-coop.md): what one player gets or does in the story is the others'
// too. Each step every game compares its save data (dSv_save_c, zeldaret/tww's layout, which the HD game keeps) with
// the step before and sends what it changed, field by field; the others put it in theirs. Anchor's way (Ship of
// Harkinian), by comparison instead of hooks: the story's event bits, each stage's chests, switches, pickups, visited
// rooms, small keys and dungeon items, the ocean's salvage bits, the items, songs, pearls, Triforce, charts, maps and
// capacities. Rupees a player gains are gained by everyone too, but each keeps their own wallet: what one spends is
// theirs alone. Not shared (Windwaker-coop's choice): health, magic, ammunition, bottle contents, bags.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct Cpu;

namespace mods::coop::progress {

// another game's changes (net::kProgress), put in at the next step
void received(const uint8_t* d, size_t n);
// every step in play: `live` once this game plays in the host's world (sync::arrived()); its own changes out, the
// others' in (a chest another player opened, in a room loaded here, is opened here too). Not live: nothing compared, the
// others' changes kept for the arrival
void step(Cpu* c, bool live);
// the session ended: the others' changes still kept are dropped
void stop();
// this game's shared progress as one message (net::kProgress's format), for a player who joins: put in by the union
// merge, so nobody loses progress (Anchor's team state)
std::vector<std::string> team_state(bool counts = true);
// the merge a server's team store gives the shared field at off..off+size (X3): 0 bits (OR), 1 a larger value, 2 filled
// where empty; -1 for none it keeps (not shared, or a count as it comes: small keys)
int store_mode(uint16_t off, uint8_t size);
// another player's shutter opening or closing (net::kDoorMove), shown here
void door_moved(const uint8_t* d, size_t n);
// rupees another player gained (net::kRupees), added here at the next step
void rupees_received(const uint8_t* d, size_t n);

}  // namespace mods::coop::progress
