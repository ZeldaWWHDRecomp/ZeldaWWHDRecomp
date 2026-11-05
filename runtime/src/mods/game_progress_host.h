// SPDX-License-Identifier: MPL-2.0
// Built-in mod "Game progress" (game_progress.cpp has the details): the switch, its option, the count for the
// Settings window, and the game thread's service.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "game_progress.h"

struct Cpu;

namespace game_progress {

bool enabled();
void set_enabled(bool on);
// the percent on the TV picture (option "on-screen", saved as mod.game-progress.on-screen)
bool on_screen();
void set_on_screen(bool on);
// the save data in play (dSv_save_c), 0 on the title screen, in the file select or with no file loaded (game thread)
uint32_t save_address();
// a save file is in play and counted (any thread)
bool loaded();
// the percent of the save in play, -1 none (any thread)
int percent();
// the count of the save in play and, if asked, its entries' names; false when none (any thread)
bool result(gp_result& out, std::vector<std::string>* names = nullptr);
// once per full logic step (interp.cpp, game thread)
void service(Cpu* c);
// `text` with its "%s" as the name of an item (item > 0), a song (item -1000 - melody), a sea square (place > 0) or a
// dungeon (place -1..-6), in the game's words where they are read (Achievements' texts); any thread
std::string fill_name(const std::string& text, int item, int place);
// reads the game's words for the language in play (game thread; once per language)
void prepare_words();
// the page of the settings page's row "game.progress" (game_settings.cpp, once)
void register_page();

}  // namespace game_progress
