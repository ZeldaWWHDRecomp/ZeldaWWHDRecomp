// SPDX-License-Identifier: MPL-2.0
// Placed residents walking a mod route with the game's own route walking (story_resident_route.h has
// the record and the guards). story_actors.cpp owns the declarations: it gives each walking resident
// one record ("slot"), creates it with the route marker and tells this module whose record it is.
//
// The hooks (only installed when a walking resident is declared, or for the world editor):
// - the route lookup of a character's creation (dNpc_PathRun_c::setInf): the route marker of an owned
//   walking resident gets its record, every other call is untouched;
// - the point-passed test (dNpc_PathRun_c::chkPointPass) for an owned walker: along its way;
// - its Walk step (daNpcPeople_c::executeWalk): the authored speed, pauses off, once, the guards;
// - its Turn step (daNpcPeople_c::executeTurn): a turning speed when the character's own is 0;
// - its animation change (daNpcPeople_c::setAnm): a walker with a borrowed walk (Ub, Uw, Sa:
//   story_resident_types.h kBorrowedWalks) plays the retarget built from the player's files in place of
//   its walk slot, which in the game's tables is its idle (the game itself never walks them).
#pragma once
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <vector>
#include "story_resident_route.h"
#include "story_resident_types.h"

struct Cpu;

namespace story_resident_routes {

// guest memory for the records (story_resident_route::kMemoryBytes, captured by full save states)
void set_memory(uint32_t guest);
uint32_t memory();
void configure(void (*original)(Cpu*, uint32_t), uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>));
// the hooks; false (and nothing installed) when another mod replaces one of them
bool install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace);
bool installed();
// the hooked game functions (tests)
uint32_t set_route_function();
uint32_t passed_function();
uint32_t walk_function();
uint32_t turn_function();
uint32_t set_animation_function();

// Borrowed walks: one slot per kBorrowedWalks entry, in guest memory that full save states capture.
// Slot layout: 0x00 magic 'RWK1', 0x04 animation bytes, 0x08 the skeleton it is bound to (0: not yet),
// 0x40 the animation object the character's motion plays (0x38 bytes, the game's layout), 0x80 the
// animation (story_resident_walk_clip::encode).
inline constexpr uint32_t kClipSlotBytes = 0x8000, kClipHeader = 0x80, kClipMagic = 0x52574B31;  // 'RWK1'
inline constexpr uint32_t kClipMemoryBytes = kClipSlotBytes * story_resident_types::kBorrowedWalkCount;
void set_clip_memory(uint32_t guest);
uint32_t clip_memory();
// copies borrowed walk `kind`'s animation into its slot (false: no memory, or too large)
bool set_clip(unsigned kind, const std::vector<uint8_t>& fska);
bool clip_ready(unsigned kind);
// the borrowed walk a walking resident of this subtype needs and has not got (-1: none needed or ready)
int missing_clip(uint8_t subtype);
// Binds `kind` to the skeleton of the character `actor` and returns the animation object to play
// (0: the skeleton is not the one the walk was built for). Game thread.
uint32_t bind_clip(unsigned kind, uint32_t actor);

// Writes `slot`'s route. keep_state: a record that already holds the same route keeps its owner and
// guard state (a restored full state, an unchanged declaration).
bool write(unsigned slot, const story_notebook::ManifestPath& path, const story_notebook::WorldPoint& start, bool keep_state);
void set_owner(unsigned slot, uint32_t pid);
uint32_t owner(unsigned slot);
void clear(unsigned slot);
uint32_t route_address(unsigned slot);  // the route header the character follows
// the slot whose record `actor` (a daNpcPeople_c) follows, or -1
int slot_of(uint32_t actor);

// A live record changed under a walking character (the editor): its point index stays in range and
// a character that had stopped for a skip or a fall walks again. Game thread.
void edited(Cpu* c, uint32_t actor, unsigned slot);

struct View {
    bool valid = false;
    story_resident_route::Status status = story_resident_route::Status::Walking;
    unsigned points = 0;
};
View view(unsigned slot);

}  // namespace story_resident_routes
