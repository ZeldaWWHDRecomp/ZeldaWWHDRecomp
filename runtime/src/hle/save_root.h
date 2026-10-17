// Where the game's /vol/save lives on the host: the --save folder, or another folder while the world
// editor's test mode is on (editor/editor_game.cpp: in-game saves then go to the test save).
#pragma once
#include <string>

namespace hle_fs {
void set_save_root(const std::string& dir);  // "" = the --save folder (config::save_dir)
std::string save_root();
}  // namespace hle_fs
