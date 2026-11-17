// The parts of the iCloud save (cloud_save_uikit.mm) that need no CloudKit: the start-up choice between this
// device's save and iCloud's, which files are the save, and how many backups are kept.
#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace cloud_save {

enum class Start {
    Nothing,   // neither side has a save
    Same,      // both have the same save
    Upload,    // only this device's changed since it last synced (or iCloud has none)
    Download,  // only iCloud's changed (or this device has none)
    Ask,       // both changed: the player picks
};

// From the saves' hashes ("": none) and the one this device last synced. A side whose hash still matches it hasn't
// changed since. REMOTE_ANY: iCloud has a save record, even one whose hash couldn't be read.
inline Start start_choice(const std::string& local, const std::string& synced, bool remote_any, const std::string& remote) {
    if (!remote_any) return local.empty() ? Start::Nothing : Start::Upload;
    if (remote == local) return Start::Same;
    if (local.empty() || local == synced) return Start::Download;
    if (remote == synced) return Start::Upload;
    return Start::Ask;
}

// The game's save files are "cking*.sav", letters, digits and "_". Anything else in the folder (iCloud Drive's
// "cking 2.sav" conflict copies, a player's own copies) isn't part of the save.
inline bool is_save_file(const std::string& name) {
    const std::string ext = ".sav";
    if (name.size() <= ext.size() || name.compare(name.size() - ext.size(), ext.size(), ext) != 0) return false;
    return std::all_of(name.begin(), name.end() - ext.size(),
                       [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; });
}

// The folders in DIR whose names start with PREFIX, oldest first beyond the newest KEEP, removed (their names end
// in the time they were made, so they sort by age). With the save in iCloud Drive they count against the player's
// iCloud storage.
inline void prune_backups(const std::filesystem::path& dir, const std::string& prefix, size_t keep) {
    std::error_code ec;
    std::vector<std::filesystem::path> found;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.is_directory(ec) && e.path().filename().string().rfind(prefix, 0) == 0) found.push_back(e.path());
    if (found.size() <= keep) return;
    std::sort(found.begin(), found.end());
    for (size_t i = 0; i + keep < found.size(); i++) std::filesystem::remove_all(found[i], ec);
}

}  // namespace cloud_save
