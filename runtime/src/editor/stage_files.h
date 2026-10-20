// World editor: the game's own stage and room tables, read from the player's game files (never copied
// anywhere): which stages and rooms exist (the room archives in content/Common/Stage and the stage
// packs) and the spawn points of a room (the PLYR entries of its room.dzr, which the game's stage
// change uses: dStage_playerInit picks the entry whose point number matches).
#pragma once
#include <cstdint>
#include <functional>
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
// Where the player's game has an object archive ("Oyashi"): "Pack/<pack>" (a member of one of the packs,
// read from their headers only) or "Object" (content/Common/Object/<name>.szs); empty when it has none.
std::string game_archive(const std::string& game_dir, const std::string& archive);
// The object archives of the player's game (content/Common/Object and the packs' members), by name, sorted.
std::vector<std::string> game_archive_names(const std::string& game_dir);
// The bone names of the models in an object archive's BFRES (the archive's own model first: the one with
// the most bones), without repeats; empty when the archive or its models can't be read.
std::vector<std::string> archive_bones(const std::string& game_dir, const std::string& archive);
// The same from BFRES data (tests).
std::vector<std::string> bfres_bones(const std::vector<uint8_t>& bfres);
// The spawn points of a stage's room from its room archive; empty when the file is missing or unreadable.
std::vector<Spawn> room_spawns(const std::string& game_dir, const std::string& stage, int room);
// Stage archives that are not in the game's files (the mod stages, mod_stages.h): a stage file name
// ("<name>_Room0.szs") -> a local file, or "". room_spawns consults it first.
void set_stage_archive_source(std::function<std::string(const std::string&)> source);

}  // namespace editor
