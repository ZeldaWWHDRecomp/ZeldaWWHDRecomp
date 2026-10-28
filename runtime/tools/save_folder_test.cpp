// A save folder chosen in the Saves tab (mods/save_folder.h, issue #190): the per-machine choice, the
// copy at the next start, cloud conflict copies, the lock, and the safe answers without a host.
#include "mods/save_folder.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <memory>
#include <thread>

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

    // no way to ask: cking.sav is played, nothing is removed (and still backed up)
    write(cloud / "user/cking 2.sav", "other");
    s = startup(def, config, nullptr, {});
    assert(read(cloud / "user/cking.sav") == "laptop save" && backups_with(backups, "cloud-conflict") == 2);
    assert(read(cloud / "user/cking 2.sav") == "other");
    release();
    fs::remove(cloud / "user/cking 2.sav");

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

    // the temporary file: a leftover older than the save is set aside; a newer one (a failed rename) stays
    const fs::path sav = cloud / "user/cking.sav";
    write(part_of(sav), "stale part");
    fs::last_write_time(part_of(sav), fs::last_write_time(sav) - std::chrono::hours(1));
    std::string error;
    assert(prepare_part(sav, true, error) && read(part_of(sav)) == "laptop save");  // rebuilt from the save
    assert(read(older_of(sav)) == "stale part");                                     // not deleted
    // written in the same second as the save, or with the same time: still the newest contents
    write(part_of(sav), "same time");
    fs::last_write_time(part_of(sav), fs::last_write_time(sav));
    assert(prepare_part(sav, true, error) && read(part_of(sav)) == "same time");
    assert(pending_parts(cloud / "user") == std::vector<std::string>{"cking.sav.wwhd-part"});
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
    assert(!fs::exists(older_of(sav)));  // the set-aside part went after the backup
    release();

    // no way to ask: a newer unfinished write is kept as a copy of its own (the game's next save
    // would write into the part), so it is offered again at the next start
    write(part_of(sav), "newer, no one to ask");
    fs::last_write_time(part_of(sav), fs::last_write_time(sav) + std::chrono::hours(1));
    s = startup(def, config, nullptr, {});
    assert(read(cloud / "user/cking-unfinished.sav") == "newer, no one to ask" && !fs::exists(part_of(sav)));
    assert(read(sav) == "finished later");
    release();
    Asked keep_save{{}, {0}};  // "Use cking.sav": now the copy goes (it is in the backup)
    s = startup(def, config, keep_save.fn(), {});
    assert(keep_save.titles.size() == 1 && !fs::exists(cloud / "user/cking-unfinished.sav"));
    release();

    // the only copy, with no cking.sav, becomes cking.sav without a question
    fs::rename(sav, part_of(sav));
    Asked silent;
    s = startup(def, config, silent.fn(), {});
    assert(silent.titles.empty() && read(sav) == "finished later" && !fs::exists(part_of(sav)));
    release();

    // more than four copies: four are offered, only those go, the rest wait for the next start
    for (int i = 1; i <= 5; i++) {
        const fs::path copy = cloud / ("user/cking-PC" + std::to_string(i) + ".sav");
        write(copy, "pc " + std::to_string(i));
        fs::last_write_time(copy, fs::last_write_time(sav) + std::chrono::minutes(i));  // PC5 newest
    }
    Asked pick_newest{{}, {1}};  // after cking.sav: the newest copy
    s = startup(def, config, pick_newest.fn(), {});
    assert(read(sav) == "pc 5" && !fs::exists(cloud / "user/cking-PC5.sav") && !fs::exists(cloud / "user/cking-PC3.sav"));
    assert(fs::exists(cloud / "user/cking-PC2.sav") && fs::exists(cloud / "user/cking-PC1.sav"));
    release();
    fs::remove(cloud / "user/cking-PC2.sav");
    fs::remove(cloud / "user/cking-PC1.sav");

    // a chosen folder that is gone is never made again: try again, the default folder, or quit
    fs::rename(cloud, root / "OneDrive" / "moved");
    Asked gone{{}, {2}};
    s = startup(def, config, gone.fn(), {});
    assert(s.quit && gone.titles.size() == 1 && !fs::exists(cloud));
    s = startup(def, config, nullptr, {});
    assert(s.quit && !fs::exists(cloud));  // no way to ask: quit
    Asked use_default{{}, {1}};
    s = startup(def, config, use_default.fn(), {});
    assert(!s.quit && s.save_dir == def && !safe_writes() && !fs::exists(cloud));
    fs::rename(root / "OneDrive" / "moved", cloud);

    // a save that was there and is gone: asked too (the choice remembers it had one)
    assert(read_choice(choice_file(config))->had_save);
    fs::rename(sav, root / "away.sav");
    Asked missing{{}, {2}};
    s = startup(def, config, missing.fn(), {});
    assert(s.quit && missing.titles.size() == 1 && !fs::exists(sav));
    Asked fresh{{}, {1}};  // "Start without a save"
    s = startup(def, config, fresh.fn(), {});
    assert(!s.quit && !fs::exists(sav) && !read_choice(choice_file(config))->had_save);
    release();
    fs::rename(root / "away.sav", sav);

    // the replace path: a backup that fails replaces nothing
    assert(choose(""));
    write(def / "user/cking.sav", "local again");
    s = startup(def, config, none.fn(), {});
    assert(choose(cloud.string()));
    fs::rename(backups, root / "data" / "backups-moved");
    write(backups, "a file where the backups folder should be");
    Asked replace{{}, {1, 0}};  // "Use the previous folder's save", then "OK"
    s = startup(def, config, replace.fn(), {});
    assert(replace.titles.size() == 2 && read(sav) == "pc 5" && read(def / "user/cking.sav") == "local again");
    assert(fs::path(read_choice(choice_file(config))->copy_from) == def);  // asked again at the next start
    release();
    fs::remove(backups);
    fs::rename(root / "data" / "backups-moved", backups);
    Asked keep_cloud{{}, {0}};
    s = startup(def, config, keep_cloud.fn(), {});
    release();

    // a copy that fails at open fails the open; a rename that fails keeps the part
    const fs::path not_a_file = cloud / "user/folder.sav";
    fs::create_directories(not_a_file);
    assert(!prepare_part(not_a_file, true, error));
    fs::remove(not_a_file);
    assert(!commit_part(cloud / "user/no-part.sav", 2, 0));
#ifdef _WIN32
    {  // a cloud app holding the save for a moment: the rename is retried until it works
        write(part_of(sav), "after a wait");
        auto held = std::make_unique<std::ifstream>(sav, std::ios::binary);
        assert(!commit_part(sav, 1, 0) && fs::exists(part_of(sav)));
        std::thread let_go([&] { std::this_thread::sleep_for(std::chrono::milliseconds(150)); held.reset(); });
        assert(commit_part(sav, 20, 50) && read(sav) == "after a wait");
        let_go.join();
    }
#endif

    // nested folders are refused, also through another case or a symbolic link
    assert(!choose((cloud / "inner").string()));
    assert(view().notice.find("inside") != std::string::npos);
    assert(choose(cloud.string()));                // the same folder is fine
    assert(choose(cloud.parent_path().string()));  // its parent means the save folder inside it
    assert(fs::path(read_choice(choice_file(config))->folder) == fs::absolute(cloud).lexically_normal());
#if defined(_WIN32) || defined(__APPLE__)
    std::string upper = cloud.string();
    for (char& ch : upper) ch = char(std::toupper((unsigned char)ch));
    assert(!choose((fs::path(upper) / "inner").string()));
    assert(choose(upper));  // the same folder spelled in another case: no change
    assert(fs::path(read_choice(choice_file(config))->folder) == fs::absolute(cloud).lexically_normal());
    assert(read_choice(choice_file(config))->copy_from.empty());
#endif
    std::error_code link_error;
    fs::create_directory_symlink(cloud, root / "link", link_error);
    if (!link_error) assert(!choose((root / "link" / "inner").string()));  // (Windows needs developer mode)

    // a folder that holds other things (the cloud folder itself) gets a folder for the save
    write(root / "Dropbox" / "photo.jpg", "not a save");
    assert(choose((root / "Dropbox").string()));
    assert(fs::path(read_choice(choice_file(config))->folder).filename() == "Wind Waker HD");
    assert(view().notice.find("Wind Waker HD") != std::string::npos);
    assert(choose(cloud.string()));

    fs::remove_all(root);
    std::puts("save_folder_test: ok");
    return 0;
}
