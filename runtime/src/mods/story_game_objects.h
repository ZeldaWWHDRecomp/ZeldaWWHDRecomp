// Game objects in the running game (story_game_objects_catalogue.h has the catalogue and the manifest
// rules). A placed game object is the game's own actor on the retail profile: story_actors creates it by
// name into the room layer like the game's stage loader does (the object name table gives its process
// and subtype), with the recipe's fixed parameter word (switch, save bit and event fields none), and
// tracks it by process id only. Nothing of the game's code is hooked for it: these objects have no
// switches, save bits, events or drops, and no other code searches for them. Once the game made it,
// the actor is read back (process, parameter word, subtype); a mismatch deletes it and quarantines the
// placement (logged), and so does a creation the game gives up on three times in one room visit.
#pragma once
#include <cstdint>
#include <initializer_list>
#include <string>

#include "story_game_objects_catalogue.h"

struct Cpu;
namespace story_game_objects {

using Call = uint32_t (*)(Cpu*, uint32_t, std::initializer_list<uint32_t>);

// What the game creates for a catalogue entry's variant.
struct Native {
    uint16_t process = 0;
    uint8_t subtype = 0xFF;
    uint32_t params = 0;
    bool own_heap = true;  // the recipe's: once made, the object has a heap of its own (read back after it)
};
// The entry's stage actor name through the game's object name table (dStage_searchName); `scratch`: 16
// bytes of guest memory for the name. False with a reason when the game doesn't know the name or gives it
// another subtype than the recipe (another game version). Cached per name.
bool resolve(Cpu* c, Call call, uint32_t scratch, const Entry& e, const std::string& variant, Native& out, std::string& why);

enum class ReadBack { Match, Mismatch };
// The created actor at `actor` against what it was created with.
ReadBack read_back(uint32_t actor, const Native& n);

// WWHD_MOD_ACTOR_HEAPS=1: the game heaps before a creation and the instance's own heap once it exists.
bool heap_trace();
void trace_before_create(Cpu* c, Call call, const std::string& mod, const std::string& id);
void trace_created(Cpu* c, Call call, const std::string& mod, const std::string& id, uint32_t actor);

// Shared batches (grass, flowers, small and bushy trees): the free units of the stage's batch of `kind`
// ("grass", "flower", "tree", "wood"), counted in the game's own batch; -1 when the stage has none (the
// game makes a stage's batch with its first own placement; a mod never makes one).
int batch_free(const std::string& kind);
// How many logic steps a room (by its process id) has been ready: a batch is planted once the room's own
// placements are done (kBatchSettleSteps).
inline constexpr unsigned kBatchSettleSteps = 30;
void note_room(uint32_t room_process);
unsigned room_age(uint32_t room_process);
// A batch placement's planted mark for the pid slot: the room process it was planted in (a room visit).
inline uint32_t planted_mark(uint32_t room_process) { return 0xFE000000u | (room_process & 0x00FFFFFFu); }
inline bool is_planted_mark(uint32_t slot) { return (slot >> 24) == 0xFE; }

// the native name lookups seen so far (tests)
void forget_resolved();

}  // namespace story_game_objects
