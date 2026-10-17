// hle/fs.cpp's view of a chosen save folder (save_folder.h).
#include "save_folder.h"
#include "save_folder_writes.h"

namespace save_folder {
bool writes_through_temporary() { return safe_writes(); }
bool replace_written(const std::string& temporary, const std::string& save) { return host::replace_file(temporary, save); }
}  // namespace save_folder
