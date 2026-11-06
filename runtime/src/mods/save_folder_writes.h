// What hle/fs.cpp and the hosts need from save_folder.h, without its includes (windows.h names OpenFile too).
#pragma once
#include <string>

namespace save_folder {
bool writes_through_temporary();  // a chosen save folder is in use: save files are written to a temporary file
// before the game writes `save`: its temporary file (save + ".wwhd-part") is ready; false: the open must fail
bool prepare_written(const std::string& save, bool keep_contents);
// after the game closed it: the temporary file becomes the save; false: it stays, and the player is told
bool commit_written(const std::string& save);
// after the game closed it with a failed write (a full disk, an I/O error): the save stays as it was
void discard_written(const std::string& save);
void release_lock();  // the game ends: this computer's lock goes
}  // namespace save_folder
