// SPDX-License-Identifier: MPL-2.0
// The mod's room paths in the game (story_room_path.h has the manifest's paths, their numbers and the
// record). Each declared path has a record at a fixed place in guest memory for as long as it is
// declared (full save states capture the records; after a load they are written again from the
// declarations) and a number in its room. A mod's actor that names a path is created with that number
// as its path number; the room-path lookup (dPath_GetRoomPath, story_enemies.cpp) answers it with the
// record only after the game's own lookup found nothing, and only for that actor.
//
// Game thread only (the load, the editor's live updates and the lookup all run there).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "story_room_path.h"

namespace story_room_paths {

// guest memory for the records (story_room_path::kMemoryBytes); the declared paths are written at once
void set_memory(uint32_t guest);
uint32_t memory();

// A mod's paths (load or editor). A path that stays in the same stage and room keeps its record and its
// number; a path that is new or moved gets a free record and the lowest free number of its room. False
// (nothing changed) when the records or a room's numbers run out.
bool declare(const std::string& mod, const std::vector<story_room_path::Path>& paths, std::string& why);
// any mod declared a path
bool declared();
// a declared path's number (0: no such path) and record address (0: none, or no memory yet)
uint8_t number(const std::string& mod, const std::string& id);
uint32_t record(const std::string& mod, const std::string& id);
// The lookup's answer for `mod`'s path `id` when the game asks for `number` in `room` of the stage
// named `stage`: its record when the path is that one, else 0.
uint32_t answer(const std::string& mod, const std::string& id, uint32_t number, int room, const std::string& stage);
// The game's next-path lookup (dPath_GetNextRoomPath) for one of these records: whether `address` is a
// record's path header, and the record of its next path in `room` (0: none).
bool is_record(uint32_t address);
uint32_t next_record(uint32_t address, int room);
// After a full state load: every record written again from the declarations.
void state_loaded();

struct Info {
    std::string mod, id, stage;
    unsigned room = 0, points = 0;
    bool closed = false;
    uint8_t number = 0;
    uint32_t address = 0;
    bool valid = false;  // the record in memory holds this path
};
std::vector<Info> list();

// tests
void reset();

}  // namespace story_room_paths
