// World editor: the game's own stage and room tables, read from the player's game files (never copied
// anywhere): which stages and rooms exist (the room archives in content/Common/Stage and the stage
// packs) and the spawn points of a room (the PLYR entries of its room.dzr, which the game's stage
// change uses: dStage_playerInit picks the entry whose point number matches).
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "commands.h"

namespace editor {

// "<stage>_Room<n>.szs" names -> stages with their rooms, sorted (other names are ignored)
std::vector<std::pair<std::string, std::vector<int>>> stage_rooms_from_names(const std::vector<std::string>& names);

struct Spawn {
    int point = 0, room = 0;
    WorldPoint position;
    int angle = 0;
};
// The PLYR entries of a room's dzr data (big-endian chunk table); empty when there are none.
std::vector<Spawn> parse_spawns(const std::vector<uint8_t>& dzr);
// The same, for data that holds a dzr somewhere inside (the HD room BFRES embeds it): found by its chunk table.
std::vector<Spawn> find_spawns(const std::vector<uint8_t>& data);

// ---- the game's files (any thread; reads only) ----
// game_dir: the folder with code/, content/, meta/
std::vector<std::pair<std::string, std::vector<int>>> game_stage_rooms(const std::string& game_dir);
// The spawn points of a stage's room from its room archive; empty when the file is missing or unreadable.
std::vector<Spawn> room_spawns(const std::string& game_dir, const std::string& stage, int room);

}  // namespace editor
