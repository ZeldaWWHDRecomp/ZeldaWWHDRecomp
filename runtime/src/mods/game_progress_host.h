// SPDX-License-Identifier: MPL-2.0
// Built-in mod "Game progress" (game_progress.cpp has the details): the switch, its option, the count for the
// Settings window, and the game thread's service.
#pragma once
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
// a save file is in play and counted (any thread)
bool loaded();
// the percent of the save in play, -1 none (any thread)
int percent();
// the count of the save in play and, if asked, its entries' names; false when none (any thread)
bool result(gp_result& out, std::vector<std::string>* names = nullptr);
// once per full logic step (interp.cpp, game thread)
void service(Cpu* c);
// the page of the settings page's row "game.progress" (game_settings.cpp, once)
void register_page();

}  // namespace game_progress
