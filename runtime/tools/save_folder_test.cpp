// A save folder chosen in the Saves tab (mods/save_folder.h, issue #190): the per-machine choice, the
// copy at the next start, cloud conflict copies, the lock, and the safe answers without a host.
#include "mods/save_folder.h"

#include <cassert>
#include <cstdio>
#include <fstream>

namespace fs = std::filesystem;
using namespace save_folder;

static void write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}
static std::string read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
static size_t backups_with(const fs::path& root, const std::string& reason) {
    size_t n = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(root, ec))
        if (read(e.path() / "backup.json").find("\"" + reason + "\"") != std::string::npos) n++;
    return n;
}

struct Asked {
    std::vector<std::string> titles;
    std::vector<int> answers;  // given in order; -1 once they run out
    Ask fn() {
        return [this](const std::string& title, const std::string&, const std::vector<std::string>& buttons) {
            titles.push_back(title);
            const size_t i = titles.size() - 1;
            const int a = i < answers.size() ? answers[i] : -1;
            assert(a < int(buttons.size()));
            return a;
        };
    }
};

int main() {
    const fs::path root = fs::temp_directory_path() / ("wwhd-save-folder-test-" + std::to_string(now()));
    fs::remove_all(root);
    const fs::path def = root / "data" / "save", config = root / "config", cloud = root / "OneDrive" / "Wind Waker HD";
    const fs::path backups = root / "data" / "save-backups";

    // the game's own files and the copies cloud apps make
    assert(game_file("cking.sav") && game_file("CKING_PLAYLOG.sav") && game_file("cking_pic0_11.sav") && game_file("cking_pic_order2.sav"));
    assert(!game_file("cking-DESKTOP.sav") && !game_file("cking_pic0_0-DESKTOP.sav") && !game_file("cking_pic.sav"));
    assert(main_save_copy("cking-DESKTOP.sav") && main_save_copy("cking (conflicted copy 2026-10-17).sav") && main_save_copy("cking 2.sav"));
    assert(!main_save_copy("cking.sav") && !main_save_copy("cking_pic0_0-DESKTOP.sav") && !main_save_copy("cking.sav.wwhd-part"));

    // the choice: written through a temporary file; an empty one is no file at all
    assert(!read_choice(choice_file(config)));
    assert(write_choice(choice_file(config), {"D:/Saves", "C:/old"}));
    auto c = read_choice(choice_file(config));
    assert(c && c->folder == "D:/Saves" && c->copy_from == "C:/old");
    assert(write_choice(choice_file(config), {}) && !fs::exists(choice_file(config)));
    write(choice_file(config), "not json");
    assert(!read_choice(choice_file(config)));  // broken: as if nothing was chosen
    fs::remove(choice_file(config));

    // the lock
    const int64_t t = now();
    assert(!held_elsewhere(std::nullopt, "A", t));
    assert(held_elsewhere(Lock{"B", t - 30}, "A", t));
    assert(!held_elsewhere(Lock{"A", t - 30}, "A", t));           // this computer's own (an earlier crash)
    assert(!held_elsewhere(Lock{"B", t - kLockStale - 1}, "A", t)); // left over
    write_lock(root / "x.lock", {"PC one", 1234});
    auto l = read_lock(root / "x.lock");
    assert(l && l->machine == "PC one" && l->time == 1234);

    // no choice: nothing changes
    write(def / "user/cking.sav", "local save");
    Asked none;
    auto s = startup(def, config, none.fn(), {});
    assert(s.save_dir == def && !s.quit && !safe_writes() && none.titles.empty());

    // a folder chosen in the Saves tab: the next start copies the save there and uses it
    assert(choose(cloud.string()));
    c = read_choice(choice_file(config));
    assert(c && fs::path(c->folder) == fs::absolute(cloud).lexically_normal() && !c->copy_from.empty());
    s = startup(def, config, none.fn(), {});
    assert(fs::absolute(s.save_dir) == fs::absolute(cloud).lexically_normal() && safe_writes());
    assert(read(cloud / "user/cking.sav") == "local save" && read(def / "user/cking.sav") == "local save");  // copied, not moved
    c = read_choice(choice_file(config));
    assert(c && c->copy_from.empty());  // done
    l = read_lock(lock_file(cloud));
    assert(l && l->machine == machine_name());
    release();
    assert(!fs::exists(lock_file(cloud)));

    // back to the default folder, while the cloud folder has a newer save: the player picks
    write(cloud / "user/cking.sav", "cloud save");
    s = startup(def, config, none.fn(), {});  // (in use again)
    assert(choose(""));
    Asked keep_new{{}, {1}};  // "Use the previous folder's save"
    s = startup(def, config, keep_new.fn(), {});
    assert(s.save_dir == def && !safe_writes() && keep_new.titles.size() == 1);
    assert(read(def / "user/cking.sav") == "cloud save" && backups_with(backups, "save-folder") == 1);
    assert(!fs::exists(choice_file(config)));

    // two copies after a cloud conflict: everything is backed up, the chosen one becomes cking.sav
    assert(choose(cloud.string()));
    s = startup(def, config, none.fn(), {});
    release();
    write(cloud / "user/cking-LAPTOP.sav", "laptop save");
    Asked pick_copy{{}, {1}};
    s = startup(def, config, pick_copy.fn(), {});
    assert(pick_copy.titles.size() == 1 && read(cloud / "user/cking.sav") == "laptop save");
    assert(!fs::exists(cloud / "user/cking-LAPTOP.sav") && backups_with(backups, "cloud-conflict") == 1);
    assert(!fs::exists(cloud / "save-backups"));  // backups stay on this computer
    release();

    // no way to ask: the safe answer keeps cking.sav (and still backs up)
    write(cloud / "user/cking 2.sav", "other");
    s = startup(def, config, nullptr, {});
    assert(read(cloud / "user/cking.sav") == "laptop save" && backups_with(backups, "cloud-conflict") == 2);
    release();

    // another computer playing: warned; "Quit" ends the start
    write_lock(lock_file(cloud), {"SOMEONE-ELSE", now()});
    Asked quit{{}, {1}};
    s = startup(def, config, quit.fn(), {});
    assert(s.quit && quit.titles.size() == 1);
    Asked anyway{{}, {0}};
    s = startup(def, config, anyway.fn(), {});
    assert(!s.quit && read_lock(lock_file(cloud))->machine == machine_name());
    release();

    // review of #238: nothing is replaced or removed without a backup
    write(cloud / "user/cking-DESKTOP.sav", "desktop save");
    fs::rename(backups, root / "data" / "backups-moved");
    write(backups, "a file where the backups folder should be");  // the backup fails
    Asked pick_desktop{{}, {0}};  // the only question: "nothing was changed"
    s = startup(def, config, pick_desktop.fn(), {});
    assert(read(cloud / "user/cking.sav") == "laptop save" && fs::exists(cloud / "user/cking-DESKTOP.sav"));  // untouched
    assert(!pick_desktop.titles.empty());  // told
    release();
    fs::remove(backups);
    fs::rename(root / "data" / "backups-moved", backups);
    fs::remove(cloud / "user/cking-DESKTOP.sav");

    // the temporary file: a leftover older than the save goes; a newer one (a failed rename) stays
    const fs::path sav = cloud / "user/cking.sav";
    write(part_of(sav), "stale part");
    fs::last_write_time(part_of(sav), fs::last_write_time(sav) - std::chrono::hours(1));
    std::string error;
    assert(prepare_part(sav, true, error) && read(part_of(sav)) == "laptop save");  // rebuilt from the save
    assert(prepare_part(sav, false, error));  // a full write: the copy isn't needed
    write(part_of(sav), "newer unsaved write");
    fs::last_write_time(part_of(sav), fs::last_write_time(sav) + std::chrono::hours(1));
    assert(prepare_part(sav, true, error) && read(part_of(sav)) == "newer unsaved write");  // kept
    assert(commit_part(sav, 1, 0) && read(sav) == "newer unsaved write" && !fs::exists(part_of(sav)));

    // a write that could not be finished is offered at the next start
    write(part_of(sav), "finished later");
    fs::last_write_time(part_of(sav), fs::last_write_time(sav) + std::chrono::hours(1));
    assert(pending_parts(cloud / "user") == std::vector<std::string>{"cking.sav.wwhd-part"});
    Asked use_part{{}, {1}};
    s = startup(def, config, use_part.fn(), {});
    assert(use_part.titles.size() == 1 && read(sav) == "finished later" && !fs::exists(part_of(sav)));
    release();

    // nested folders are refused
    assert(!choose((cloud / "inner").string()) && !choose(cloud.parent_path().string()));
    assert(view().notice.find("inside") != std::string::npos);
    assert(choose(cloud.string()));  // the same folder is fine

    fs::remove_all(root);
    std::puts("save_folder_test: ok");
    return 0;
}
