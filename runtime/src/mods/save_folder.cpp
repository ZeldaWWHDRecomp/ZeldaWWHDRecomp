// save_folder_writes.h: hle/fs.cpp's and the hosts' view of a chosen save folder (save_folder.h).
#include "save_folder.h"
#include "save_folder_writes.h"
#include "../runtime.h"

namespace save_folder {
bool writes_through_temporary() { return safe_writes(); }
bool prepare_written(const std::string& save, bool keep_contents) {
    std::string error;
    if (prepare_part(fs::path(save), keep_contents, error)) return true;
    LOG("[save-folder] the game can't write %s: %s", save.c_str(), error.c_str());
    return false;
}
bool commit_written(const std::string& save) {
    if (commit_part(fs::path(save))) return true;
    LOG("[save-folder] %s could not replace %s; kept for the next start", part_of(fs::path(save)).string().c_str(), save.c_str());
    write_not_finished(fs::path(save));
    return false;
}
void discard_written(const std::string& save) {
    std::error_code ec;
    fs::remove(part_of(fs::path(save)), ec);
    LOG("[save-folder] writing %s failed (a full disk or an I/O error); the save is unchanged", save.c_str());
    write_failed(fs::path(save));
}
void release_lock() { release(); }
}  // namespace save_folder
