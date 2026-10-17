// What hle/fs.cpp and the hosts need from save_folder.h, without its includes (windows.h names OpenFile too).
#pragma once
#include <string>

namespace save_folder {
bool writes_through_temporary();  // a chosen save folder is in use: save files are written to a temporary file
bool replace_written(const std::string& temporary, const std::string& save);  // the temporary file becomes the save
void release_lock();  // the game ends: this computer's lock goes
}  // namespace save_folder
