// cloud_save_sync.h: the iCloud save's start-up choice, which files are the save, and the backups kept.
#include "gfx/cloud_save_sync.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

using cloud_save::Start;
using cloud_save::start_choice;
namespace fs = std::filesystem;

int main() {
    // nothing in iCloud: upload this device's, if it has one
    assert(start_choice("", "", false, "") == Start::Nothing);
    assert(start_choice("a", "", false, "") == Start::Upload);
    assert(start_choice("a", "a", false, "") == Start::Upload);  // (iCloud's was removed)
    // the same save on both
    assert(start_choice("a", "", true, "a") == Start::Same);
    assert(start_choice("a", "b", true, "a") == Start::Same);
    // only iCloud's changed, or this device has none
    assert(start_choice("a", "a", true, "b") == Start::Download);
    assert(start_choice("", "", true, "b") == Start::Download);
    assert(start_choice("", "a", true, "b") == Start::Download);
    // only this device's changed
    assert(start_choice("c", "b", true, "b") == Start::Upload);
    // both changed since the last sync, or this device never synced and they differ
    assert(start_choice("c", "a", true, "b") == Start::Ask);
    assert(start_choice("c", "", true, "b") == Start::Ask);
    // a record whose hash couldn't be read is a change on iCloud's side
    assert(start_choice("a", "a", true, "") == Start::Download);
    assert(start_choice("c", "a", true, "") == Start::Ask);

    for (const char* name : {"cking.sav", "cking_playlog.sav", "cking_pic0_11.sav", "cking_pic_order2.sav"})
        assert(cloud_save::is_save_file(name));
    for (const char* name : {"cking 2.sav", "cking (1).sav", ".cking.sav.download", "cking.sav.icloud", ".sav", "cking.SAV.bak",
                             "cking", "cking-copy.sav"})
        assert(!cloud_save::is_save_file(name));

    const fs::path dir = fs::temp_directory_path() / ("cloud_save_sync_test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::remove_all(dir);
    for (const char* name : {"icloud-20261101-100000-this-device", "icloud-20261102-100000-icloud",
                             "icloud-20261103-100000-this-device", "icloud-20261103-100000-this-device-2", "other-20260101"})
        fs::create_directories(dir / name / "user");
    cloud_save::prune_backups(dir, "icloud-", 2);
    assert(!fs::exists(dir / "icloud-20261101-100000-this-device"));
    assert(!fs::exists(dir / "icloud-20261102-100000-icloud"));
    assert(fs::exists(dir / "icloud-20261103-100000-this-device"));
    assert(fs::exists(dir / "icloud-20261103-100000-this-device-2"));
    assert(fs::exists(dir / "other-20260101"));
    cloud_save::prune_backups(dir / "missing", "icloud-", 2);  // (no folder: nothing to do)
    fs::remove_all(dir);
    std::puts("cloud_save_sync: ok");
    return 0;
}
