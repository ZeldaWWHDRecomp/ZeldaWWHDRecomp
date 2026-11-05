/* SPDX-License-Identifier: MPL-2.0
 * GameCube player models, per save file and live: a converted Link.bfres (runtime/src/gcmodel, kept as a Link.szs)
 * put into the game's memory over the Link.bfres the game loaded, when the save file played has one; HD Link put
 * back when it has none. The game's pack stays as it is; no restart, no renderer code: the game draws the model
 * with HD's materials, shaders and animations (player_models_core.h says why that works).
 *
 * From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (v0.9.9, MPL-2.0), runtime/src/mods/player_models.cpp;
 * the glue to this port: the save file from game_progress, the pack the game opened (fs.cpp), save-state loads,
 * models named by an id and a Link.szs path (the mod manager's player model packages).
 *
 * Which model is in memory is noted in the area's header and checked against sample words: a save state may bring
 * another, with or without the area. Nothing chosen and nothing in memory: the service returns at once and the
 * game's memory is never touched.
 */
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace player_models {

inline constexpr int kFiles = 3;

struct Choice {
    std::string id;   // "" = HD Link
    std::string szs;  // the model's converted Link.szs
};
// what each save file (1..3 = [0..2]) shows; any thread, taken over live
void set_choices(const std::array<Choice, kFiles>& files);
// a model id -> its Link.szs, for a model a save state brought that no file has (to take it out again); "" unknown
void set_resolver(std::function<std::string(const std::string& id)> resolve);
// a new game was started in save file `file` (0..2) that had a model: it shows HD Link now; the caller forgets
// the choice (game main thread)
void set_new_game_listener(std::function<void(int file)> listener);

// hle/fs.cpp: a file the game opens (Common/Pack/permanent_3d.pack: the pack whose Link.bfres is in memory)
void note_open(const std::string& guest_path, const std::string& host_path);

// game main thread, frame start (interp.cpp)
void service();

// save states: the area's used part (0 when nothing is in)
uint32_t area(uint32_t* used);

// the model in memory for logs and tests ("" HD Link or not known yet)
std::string current();

}  // namespace player_models
