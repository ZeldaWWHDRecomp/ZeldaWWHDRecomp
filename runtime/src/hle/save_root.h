// Where the game's /vol/save lives on the host: the --save folder, or another folder while the world
// editor's test mode is on (editor/editor_game.cpp: in-game saves then go to the test save).
#pragma once
#include <string>

namespace hle_fs {
void set_save_root(const std::string& dir);  // "" = the --save folder (config::save_dir)
std::string save_root();

// A host about to end the process (the Vision Pro's game window closed): waits until no save file is open for writing
// and none was closed in the last SETTLE seconds (a save is several files, one after another), at most MAX_SECONDS,
// then holds every later save write for good, so the save on disk is whole when the process ends. -> whether it
// settled in time.
bool stop_save_writes(double max_seconds, double settle = 0.5);
}  // namespace hle_fs
