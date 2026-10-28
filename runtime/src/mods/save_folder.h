// The save folder a player chooses (Settings > Saves > Save folder, desktop), for example inside
// OneDrive, Dropbox, iCloud Drive or Google Drive, so the cloud app keeps the save the same on all
// of their computers (issue #190).
//
// The choice is per machine: save-folder.json in the config folder, never in the synced folder.
// Without one nothing changes: the save stays in config::save_dir ("save"), and --save still wins.
// A change applies at the next start, before the game reads any save (startup()):
//   - the new folder has no save yet: the current save folder is copied there (never moved);
//   - it has one: the player picks which save to keep (both dates shown), after a backup.
// In a chosen folder, at every start:
//   - extra cking*.sav copies, the way cloud apps keep both sides of a conflict (OneDrive
//     "cking-PCNAME.sav", Dropbox "cking (conflicted copy ...).sav", iCloud "cking 2.sav"), offer
//     a choice after a backup of the whole folder;
//   - a lock file names the computer playing; another computer's recent lock gives a warning;
//   - cking.sav is read once: a file the cloud app can't provide (an online-only placeholder that
//     fails to download) stops the start with a message, so the game never makes a new, empty save;
//     the same for a chosen folder that is gone, or a save that was there at an earlier start;
//   - with no way to ask, nothing is removed and nothing is made anew;
//   - the game's save files are written to a temporary file and renamed over the old one when
//     closed (hle/fs.cpp), so the cloud app never uploads half a save. The story block (story_store.h)
//     is added after that rename, through its own temporary file, as everywhere else.
// Backups go to the local save-backups folder (mods/save_backup.h), never into the synced folder.
#pragma once
#include "../platform/host.h"
#include "mod_json.h"
#include "save_backup.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace save_folder {
namespace fs = std::filesystem;

// ---------------------------------------------------------------- the per-machine choice
struct Choice {
    std::string folder;     // the chosen save folder; empty: the default one
    std::string copy_from;  // a change waiting for the next start: the folder the save comes from
    bool had_save = false;  // the folder had a save at a start: a missing one is asked about, never made anew
};
inline fs::path choice_file(const fs::path& config_dir) { return config_dir / "save-folder.json"; }

inline std::optional<Choice> read_choice(const fs::path& file) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return std::nullopt;
    try {
        std::ifstream in(file, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const mods::json::Value v = mods::json::parse(text);
        Choice c;
        c.folder = v.get("folder").string();
        c.copy_from = v.get("copy_from").string();
        c.had_save = v.get("had_save").boolean;
        if (c.folder.empty() && c.copy_from.empty()) return std::nullopt;
        return c;
    } catch (const std::exception&) {
        return std::nullopt;  // a broken file: as if no folder was chosen
    }
}
// Written through a temporary file; an empty choice removes the file (the default folder).
inline bool write_choice(const fs::path& file, const Choice& c) {
    std::error_code ec;
    if (c.folder.empty() && c.copy_from.empty()) {
        fs::remove(file, ec);
        return !ec;
    }
    fs::create_directories(file.parent_path(), ec);
    mods::json::Value v;
    v["folder"] = c.folder;
    if (!c.copy_from.empty()) v["copy_from"] = c.copy_from;
    if (c.had_save) v["had_save"] = true;
    const fs::path temp = file.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << mods::json::dump(v) << "\n";
        out.flush();
        if (!out) return false;
    }
    return host::replace_file(temp.string(), file.string());
}

// ---------------------------------------------------------------- what a save folder holds
inline std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}
inline bool digits(const std::string& s, size_t from, size_t to) {
    if (from >= to) return false;
    for (size_t i = from; i < to; i++)
        if (!std::isdigit((unsigned char)s[i])) return false;
    return true;
}
// The files the game itself writes in user/: cking.sav, cking_pic<N>_<M>.sav (the pictographs),
// cking_pic_order<N>.sav and cking_playlog.sav.
inline bool game_file(const std::string& name) {
    const std::string n = lower(name);
    if (n == "cking.sav" || n == "cking_playlog.sav") return true;
    if (n.size() < 4 || n.compare(n.size() - 4, 4, ".sav") != 0) return false;
    const size_t end = n.size() - 4;
    if (n.rfind("cking_pic_order", 0) == 0) return digits(n, 15, end);
    if (n.rfind("cking_pic", 0) == 0) {
        const size_t us = n.find('_', 9);
        return us != std::string::npos && us < end && digits(n, 9, us) && digits(n, us + 1, end);
    }
    return false;
}
// A copy of the main save the game doesn't know: "cking" + something other than "_" + ".sav".
inline bool main_save_copy(const std::string& name) {
    const std::string n = lower(name);
    return n.size() > 9 && n.rfind("cking", 0) == 0 && n[5] != '_' && n.compare(n.size() - 4, 4, ".sav") == 0 &&
           n != "cking.sav";
}
// The main save's conflict copies in <save>/user, sorted by name.
inline std::vector<std::string> conflict_copies(const fs::path& save_dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(save_dir / "user", ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string name = entry.path().filename().string();
        if (main_save_copy(name)) out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    return out;
}
// A name for another copy of the main save that isn't taken: <stem>.sav, <stem>-2.sav, ...
inline std::string free_copy_name(const fs::path& user_dir, const std::string& stem) {
    std::error_code ec;
    std::string name = stem + ".sav";
    for (int i = 2; fs::exists(user_dir / name, ec); i++) name = stem + "-" + std::to_string(i) + ".sav";
    return name;
}
inline bool has_save(const fs::path& save_dir) {
    std::error_code ec;
    return fs::is_regular_file(save_dir / "user" / "cking.sav", ec);
}

// A file's last change at the file system's full precision, to order a temporary file and its save
// (a write and the save's rename often land in the same second); the oldest time if unknown.
inline fs::file_time_type changed(const fs::path& file) {
    std::error_code ec;
    const auto t = fs::last_write_time(file, ec);
    return ec ? fs::file_time_type::min() : t;
}
// `a` was not changed before `b`: a temporary file that may hold the newest contents. The same time
// counts too (coarse file systems, cloud apps that set times), so such a file is offered, not removed.
inline bool not_older(const fs::path& a, const fs::path& b) { return !(changed(a) < changed(b)); }

// seconds since the epoch of a file's last change (0 if unknown), shown to the player
inline int64_t modified(const fs::path& file) {
#ifdef _WIN32
    struct _stat64 st;
    return _wstat64(file.wstring().c_str(), &st) == 0 ? int64_t(st.st_mtime) : 0;
#else
    struct stat st;
    return stat(file.c_str(), &st) == 0 ? int64_t(st.st_mtime) : 0;
#endif
}
inline std::string local_time(int64_t t) {
    if (t <= 0) return "unknown time";
    std::time_t tt = std::time_t(t);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%d %H:%M", &tm);
    return text;
}
inline int64_t now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
inline bool same_bytes(const fs::path& a, const fs::path& b) {
    std::ifstream x(a, std::ios::binary), y(b, std::ios::binary);
    if (!x || !y) return false;
    return std::string((std::istreambuf_iterator<char>(x)), std::istreambuf_iterator<char>()) ==
           std::string((std::istreambuf_iterator<char>(y)), std::istreambuf_iterator<char>());
}

// Reads the whole file once. Cloud apps download an online-only file when it is opened; when that
// fails the read fails, and the start stops instead of letting the game see no save.
enum class Readable { NoSave, Ok, Failed };
inline Readable read_check(const fs::path& file, std::string* error = nullptr) {
    std::error_code ec;
    if (!fs::exists(file, ec)) return Readable::NoSave;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        if (error) *error = "the file can't be opened";
        return Readable::Failed;
    }
    char buffer[65536];
    while (in.read(buffer, sizeof buffer) || in.gcount() > 0)
        if (in.bad()) break;
    if (in.bad()) {
        if (error) *error = "the file can't be read";
        return Readable::Failed;
    }
    return Readable::Ok;
}

// Copies a save folder's files into another one; existing files are replaced only with `replace`.
inline bool copy_save(const fs::path& from, const fs::path& to, bool replace, std::string& error) {
    std::error_code ec;
    fs::create_directories(to, ec);
    if (ec) return error = "can't create " + to.string() + ": " + ec.message(), false;
    auto options = fs::copy_options::recursive | (replace ? fs::copy_options::overwrite_existing : fs::copy_options::skip_existing);
    fs::copy(from, to, options, ec);
    if (ec) return error = "can't copy the save to " + to.string() + ": " + ec.message(), false;
    return true;
}

// ---------------------------------------------------------------- the lock: who is playing
struct Lock {
    std::string machine;
    int64_t time = 0;  // when it was last refreshed (seconds since the epoch)
};
inline constexpr int64_t kLockRefresh = 5 * 60;   // a running game rewrites its lock this often (the cloud app uploads it)
inline constexpr int64_t kLockStale = 15 * 60;    // an older lock is left over from a crash or a power cut
inline fs::path lock_file(const fs::path& save_dir) { return save_dir / "wwhd-playing.lock"; }

inline std::string machine_name() {
#ifdef _WIN32
    wchar_t name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameW(name, &size)) return fs::path(std::wstring(name, size)).string();
#else
    char name[256] = {};
    if (gethostname(name, sizeof name - 1) == 0 && name[0]) return name;
#endif
    return "this computer";
}
inline std::optional<Lock> read_lock(const fs::path& file) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return std::nullopt;
    try {
        std::ifstream in(file, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const mods::json::Value v = mods::json::parse(text);
        Lock l;
        l.machine = v.get("machine").string();
        l.time = int64_t(v.get("time").number);
        if (l.machine.empty()) return std::nullopt;
        return l;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}
inline bool write_lock(const fs::path& file, const Lock& l) {
    mods::json::Value v;
    v["machine"] = l.machine;
    v["time"] = double(l.time);
    const fs::path temp = file.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << mods::json::dump(v) << "\n";
        out.flush();
        if (!out) return false;
    }
    return host::replace_file(temp.string(), file.string());
}
// Another computer's lock, refreshed recently: that computer is probably playing right now.
inline bool held_elsewhere(const std::optional<Lock>& l, const std::string& me, int64_t at) {
    return l && l->machine != me && at - l->time < kLockStale && l->time - at < kLockStale;
}

// ---------------------------------------------------------------- start-up
// A question for the player: returns the chosen button's index, or -1 when this host can't ask
// (each question then takes its safe answer, written in the log and in the Saves tab).
using Ask = std::function<int(const std::string& title, const std::string& text, const std::vector<std::string>& buttons)>;

struct State {
    std::mutex mu;
    bool active = false;     // the game uses a chosen folder (safe writes, the lock)
    fs::path folder;         // the save folder in use
    fs::path default_folder; // where the save goes without a choice
    fs::path backups;        // the local save-backups folder
    fs::path config_dir;
    std::string machine;
    std::string notice;      // shown once in the Saves tab
    std::atomic<bool> stop{false};
};
inline State& state() { static State s; return s; }

// A backup before a step that replaces or removes save files. False: the step must not happen.
inline bool backup(const fs::path& folder, const fs::path& backups, const std::string& reason, std::function<void(const std::string&)> log,
                   std::string& error) {
    mods::saves::Store store(folder, backups);
    if (log) store.log = std::move(log);
    const auto result = store.create(reason);
    if (!result.ok) error = result.error;
    return result.ok;
}

// ---------------------------------------------------------------- safe writes (hle/fs.cpp)
inline fs::path part_of(const fs::path& save) { return fs::path(save.string() + ".wwhd-part"); }
// An older temporary file set aside during play (prepare_part); startup() backs it up, then removes it.
inline fs::path older_of(const fs::path& save) { return fs::path(save.string() + ".wwhd-older"); }
// Before the game writes `save`: the temporary file it writes instead. For updates and appends it
// starts as a copy of the save. A part not older than the save is a write whose rename failed
// (commit_part): it stays, as the newest contents. An older one (a crash, or a part a cloud app
// brought from another computer) is never deleted here: it is set aside for the next start's backup.
// False: the game's open fails, and nothing is written.
inline bool prepare_part(const fs::path& save, bool keep_contents, std::string& error) {
    const fs::path part = part_of(save);
    std::error_code ec;
    const bool has_save = fs::exists(save, ec), has_part = fs::exists(part, ec);
    if (has_part && (!has_save || not_older(part, save))) return true;
    if (has_part && !host::replace_file(part.string(), older_of(save).string()))
        return error = "can't set aside the old " + part.string(), false;
    if (keep_contents && has_save && !fs::copy_file(save, part, fs::copy_options::overwrite_existing, ec))
        return error = "can't copy " + save.string() + ": " + ec.message(), false;
    return true;
}
// After the game closed it: the temporary file becomes the save. A cloud app can hold the save open
// right after a sync, and Windows refuses the rename then, so it is retried for about a second. If
// it still fails the part stays (the next write builds on it, and startup() offers it).
inline bool commit_part(const fs::path& save, int tries = 10, int wait_ms = 100) {
    for (int i = 0; i < tries; i++) {
        if (host::replace_file(part_of(save).string(), save.string())) return true;
        if (i + 1 < tries) std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
    }
    return false;
}
inline bool ends_with(const std::string& s, const std::string& end) {
    return s.size() > end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
}
// A folder's pending writes (<file>.wwhd-part) not older than their file.
inline std::vector<std::string> pending_parts(const fs::path& user_dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(user_dir, ec)) {
        const std::string name = e.path().filename().string();
        if (!ends_with(name, ".wwhd-part")) continue;
        const fs::path file = user_dir / name.substr(0, name.size() - 10);
        if (!fs::exists(file, ec) || not_older(e.path(), file)) out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    return out;
}
// Leftovers removed after a backup: parts older than their file, and parts set aside (.wwhd-older).
inline std::vector<std::string> stale_parts(const fs::path& user_dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(user_dir, ec)) {
        const std::string name = e.path().filename().string();
        if (ends_with(name, ".wwhd-older")) out.push_back(name);
        else if (ends_with(name, ".wwhd-part")) {
            const fs::path file = user_dir / name.substr(0, name.size() - 10);
            if (fs::exists(file, ec) && !not_older(e.path(), file)) out.push_back(name);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

struct Startup {
    fs::path save_dir;   // the folder the game uses
    bool quit = false;   // the player chose to quit
};

// main(), before the game reads any save. default_dir: config::save_dir without a choice.
inline Startup startup(const fs::path& default_dir, const fs::path& config_dir, const Ask& ask, std::function<void(const std::string&)> log) {
    auto& s = state();
    std::lock_guard guard(s.mu);
    Startup out;
    const fs::path def = fs::absolute(default_dir).lexically_normal();
    out.save_dir = default_dir;
    s.default_folder = def;
    s.backups = def.parent_path() / "save-backups";
    s.config_dir = config_dir;
    s.machine = machine_name();
    s.folder = def;
    s.active = false;
    s.notice.clear();
    auto say = [&](const std::string& line) { if (log) log("[save-folder] " + line); };
    auto question = [&](const std::string& title, const std::string& text, const std::vector<std::string>& buttons) {
        return ask ? ask(title, text, buttons) : -1;
    };
    const fs::path file = choice_file(config_dir);
    auto choice = read_choice(file);
    if (!choice) return out;
    const fs::path target = choice->folder.empty() ? def : fs::absolute(fs::path(choice->folder)).lexically_normal();

    // a change made in the Saves tab: bring the save along
    if (!choice->copy_from.empty()) {
        const fs::path source = fs::absolute(fs::path(choice->copy_from)).lexically_normal();
        std::string error;
        bool ok = true;
        if (source != target && has_save(source)) {
            if (!has_save(target)) {
                ok = copy_save(source, target, false, error);
                if (ok) say("copied the save from " + source.string() + " to " + target.string());
            } else if (!same_bytes(source / "user/cking.sav", target / "user/cking.sav")) {
                const int64_t ts = modified(source / "user/cking.sav"), tt = modified(target / "user/cking.sav");
                const std::string text = "The new save folder already has a save.\n\n"
                                         "In the new folder (" + target.string() + "): saved " + local_time(tt) + "\n"
                                         "In the previous folder (" + source.string() + "): saved " + local_time(ts) + "\n\n"
                                         "Which save do you want to keep? The other one is kept in save-backups.";
                const int pick = question("Wind Waker HD: save folder", text, {"Keep the new folder's save", "Use the previous folder's save"});
                if (pick == 1) {
                    std::string backup_error;
                    if (!backup(target, s.backups, "save-folder", log, backup_error)) {
                        // nothing is replaced without a backup; the question comes again at the next start
                        s.notice = "The save in " + target.string() + " was not replaced: it could not be backed up first (" + backup_error +
                                   "). Both saves are unchanged.";
                        say(s.notice);
                        question("Wind Waker HD: save folder", s.notice, {"OK"});
                        out.save_dir = target;
                        s.folder = target;
                        s.active = target != def;
                        return out;
                    }
                    ok = copy_save(source, target, true, error);
                    if (ok) say("replaced the save in " + target.string() + " with the one from " + source.string());
                } else {
                    say("kept the save already in " + target.string());
                }
            }
        }
        if (!ok) {
            // the previous folder stays in use; the change waits for the next start
            say(error);
            s.notice = "The save folder change didn't work: " + error + ". The previous save folder is still in use.";
            question("Wind Waker HD: save folder", s.notice, {"OK"});
            out.save_dir = source;
            s.folder = source;
            s.active = source != def;
            return out;
        }
        choice->copy_from.clear();
        write_choice(file, *choice);
        std::error_code ec;
        fs::create_directories(target / "user", ec);  // only now, when the player just chose it
    }
    out.save_dir = target;
    s.folder = target;
    if (target == def) return out;  // the default folder: nothing else changes
    s.active = true;
    std::error_code ec;
    const fs::path user = target / "user";

    // a chosen folder that is gone (a cloud app signed out, a drive unplugged, a renamed folder) is
    // never made again here: the game would start a new, empty save that a cloud app could later
    // prefer over the real one
    while (!fs::is_directory(target, ec)) {
        const std::string text = "Your save folder can't be found:\n" + target.string() +
                                 "\n\nIf it is in a cloud folder or on a removable drive, check that the cloud app is running and "
                                 "signed in, or that the drive is connected.";
        const int pick = question("Wind Waker HD: save folder", text, {"Try again", "Use the default save folder this time", "Quit"});
        if (pick == 0) continue;
        if (pick == 1) {
            say(target.string() + " is missing: using the default save folder for this session");
            s.notice = "Your save folder " + target.string() + " couldn't be found, so the default save folder is used for this session.";
            out.save_dir = def;
            s.folder = def;
            s.active = false;
            return out;
        }
        say(target.string() + " is missing: quitting");  // also when this host can't ask
        out.quit = true;
        return out;
    }
    // the same for a save that was there and is gone
    // (cking.sav, its unfinished write or a cloud copy of it all count as the save)
    while (choice->had_save && !fs::exists(user / "cking.sav", ec) && !fs::exists(part_of(user / "cking.sav"), ec) &&
           conflict_copies(target).empty()) {
        const std::string text = "Your save is missing from " + user.string() +
                                 ".\n\nIf it is in a cloud folder, the cloud app may still be downloading it, or it may have been "
                                 "moved. Starting without it begins a new game in this folder.";
        const int pick = question("Wind Waker HD: save folder", text, {"Try again", "Start without a save", "Quit"});
        if (pick == 0) continue;
        if (pick == 1) {
            say("cking.sav is missing: starting without a save");
            choice->had_save = false;
            write_choice(file, *choice);
            break;
        }
        say("cking.sav is missing: quitting");
        out.quit = true;
        return out;
    }
    fs::create_directories(user, ec);

    // a save the cloud app can't provide
    for (;;) {
        std::string error;
        if (read_check(user / "cking.sav", &error) != Readable::Failed) break;
        const std::string text = "Your save in " + target.string() + " can't be read (" + error + ").\n\n"
                                 "If it is in a cloud folder, check that the cloud app is running and signed in, "
                                 "and that the file is available on this computer.";
        const int pick = question("Wind Waker HD: save folder", text, {"Try again", "Quit"});
        if (pick != 0) {
            say("cking.sav unreadable (" + error + "): quitting");
            out.quit = true;
            return out;
        }
    }

    // both sides of a cloud conflict, writes whose rename failed (commit_part) and leftover parts
    const auto copies = conflict_copies(target);
    const auto parts = pending_parts(user);
    const auto stale = stale_parts(user);
    const bool main_part = std::find(parts.begin(), parts.end(), "cking.sav.wwhd-part") != parts.end();
    if (!copies.empty() || !parts.empty() || !stale.empty()) {
        std::string backup_error;
        if (!backup(target, s.backups, "cloud-conflict", log, backup_error)) {
            // nothing is replaced or removed without a backup
            s.notice = "Your save folder has more than one copy of your save, but it could not be backed up (" + backup_error +
                       "), so nothing was changed. cking.sav is in use; the other copies are still in " + user.string() + ".";
            say(s.notice);
            question("Wind Waker HD: two saves", s.notice, {"OK"});
        } else {
            for (const auto& p : stale)  // older than their file, and in the backup
                if (fs::remove(user / p, ec)) say("removed the leftover " + p + " (kept in save-backups)");
            // the other files' pending writes are not older than their files: they become the files
            for (const auto& p : parts) {
                if (p == "cking.sav.wwhd-part") continue;
                const std::string file = p.substr(0, p.size() - 10);
                if (host::replace_file((user / p).string(), (user / file).string())) say("finished an earlier write of " + file);
            }
            // the copies of the main save: cking.sav (if there is one), the unfinished write, then
            // the cloud copies, newest first; the buttons offer four, the rest wait for the next start
            std::vector<std::string> names;
            if (fs::exists(user / "cking.sav", ec)) names.push_back("cking.sav");
            if (main_part) names.push_back("cking.sav.wwhd-part");
            std::vector<std::string> newest = copies;
            std::stable_sort(newest.begin(), newest.end(),
                             [&](const std::string& a, const std::string& b) { return changed(user / fs::path(a)) > changed(user / fs::path(b)); });
            for (const auto& c : newest) names.push_back(c);
            if (names.size() > 4) names.resize(4);
            if (names.size() == 1 && names[0] != "cking.sav") {
                // no cking.sav: the only copy becomes it, nothing to choose
                if (host::replace_file((user / fs::path(names[0])).string(), (user / "cking.sav").string()))
                    say(names[0] + " became cking.sav (the only copy of the save)");
            } else if (names.size() > 1) {
                std::string text = "Your save folder has more than one copy of your save:\n\n";
                std::vector<std::string> buttons;
                for (const auto& n : names) {
                    const char* what = n == "cking.sav.wwhd-part" ? " (a save that could not be finished)" : "";
                    text += n + what + ": saved " + local_time(modified(user / fs::path(n))) + "\n";
                    buttons.push_back("Use " + n);
                }
                text += "\nWhich one do you want to play with? All of them are kept in save-backups.";
                const int pick = question("Wind Waker HD: two saves", text, buttons);
                if (pick < 0 || pick >= int(names.size())) {
                    // no way to ask: nothing is removed, cking.sav (or the newest copy) is played and
                    // the choice comes again at the next start. The unfinished write becomes a copy
                    // of its own, since the game's next save would otherwise write into it.
                    if (names[0] == "cking.sav.wwhd-part") {
                        if (host::replace_file((user / names[0]).string(), (user / "cking.sav").string()))
                            say("no cking.sav: the unfinished write became cking.sav");
                    } else {
                        if (names[0] != "cking.sav") {
                            fs::copy_file(user / fs::path(names[0]), user / "cking.sav", ec);
                            say("no cking.sav: playing a copy of " + names[0]);
                        }
                        if (main_part) {
                            const std::string copy = free_copy_name(user, "cking-unfinished");
                            if (host::replace_file((user / "cking.sav.wwhd-part").string(), (user / copy).string()))
                                say("kept the unfinished write as " + copy);
                        }
                    }
                    s.notice = "Your save folder has more than one copy of your save. Nothing was removed; you'll be asked again at the next start.";
                    say(s.notice);
                } else {
                    const std::string chosen = names[size_t(pick)];
                    bool used = chosen == "cking.sav";
                    if (!used) {
                        const fs::path temp = user / "cking.sav.wwhd-choice";
                        fs::copy_file(user / fs::path(chosen), temp, fs::copy_options::overwrite_existing, ec);
                        used = !ec && host::replace_file(temp.string(), (user / "cking.sav").string());
                        if (!used) fs::remove(temp, ec);
                    }
                    if (used) {
                        for (const auto& n : names)  // the ones offered, all in the backup
                            if (n != "cking.sav") fs::remove(user / fs::path(n), ec);
                        s.notice = "Your save folder had more than one copy of your save; " + chosen + " is in use. All copies are in save-backups.";
                    } else {
                        s.notice = chosen + " could not be used, so nothing was changed: cking.sav is in use and the other copies are still in " +
                                   user.string() + ".";
                        question("Wind Waker HD: two saves", s.notice, {"OK"});
                    }
                    say(s.notice);
                }
            }
        }
    }
    if (!choice->had_save && fs::exists(user / "cking.sav", ec)) {
        choice->had_save = true;
        write_choice(file, *choice);
    }

    // another computer playing
    const auto lock = read_lock(lock_file(target));
    if (held_elsewhere(lock, s.machine, now())) {
        const std::string text = "Wind Waker HD seems to be running on " + lock->machine + " with this save folder (last seen " +
                                 local_time(lock->time) + ").\n\nPlaying on two computers at once can lose progress: "
                                 "each one overwrites the other's save. Close the game there first.";
        const int pick = question("Wind Waker HD: save folder in use", text, {"Play anyway", "Quit"});
        if (pick == 1) {
            say("in use on " + lock->machine + ": quitting");
            out.quit = true;
            return out;
        }
        say("in use on " + lock->machine + ": playing anyway");
    }
    write_lock(lock_file(target), {s.machine, now()});
    s.stop = false;
    std::thread([] {
        auto& st = state();
        for (int64_t waited = 0; !st.stop; waited++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (waited % kLockRefresh != kLockRefresh - 1) continue;
            std::lock_guard g(st.mu);
            if (st.active && !st.stop) write_lock(lock_file(st.folder), {st.machine, now()});
        }
    }).detach();
    say("using " + target.string());
    return out;
}

// The game ends: the lock goes (only ours).
inline void release() {
    auto& s = state();
    s.stop = true;
    std::lock_guard guard(s.mu);
    if (!s.active) return;
    const auto lock = read_lock(lock_file(s.folder));
    std::error_code ec;
    if (lock && lock->machine == s.machine) fs::remove(lock_file(s.folder), ec);
}

// hle/fs.cpp: write save files through a temporary file in a chosen folder
inline bool safe_writes() { return state().active; }
inline void write_not_finished(const fs::path& save) {
    auto& s = state();
    std::lock_guard guard(s.mu);
    s.notice = "Your last save could not be finished in " + save.parent_path().string() +
               " (another program, such as the cloud app, had it open). It is kept next to it and offered at the next start.";
}

inline void write_failed(const fs::path& save) {
    auto& s = state();
    std::lock_guard guard(s.mu);
    s.notice = "Your last save could not be written to " + save.parent_path().string() +
               " (the disk may be full). The previous save is unchanged; free some space and save again.";
}

// ---------------------------------------------------------------- the Saves tab
struct View {
    std::string folder;      // in use now
    std::string chosen;      // for the next start (empty: the default folder)
    bool pending = false;    // a change waits for the next start
    bool custom = false;     // a chosen folder is in use
    std::string notice;
};
inline View view() {
    auto& s = state();
    std::lock_guard guard(s.mu);
    View v;
    v.folder = s.folder.empty() ? std::string("the folder given on the command line") : s.folder.string();
    v.custom = s.active;
    v.notice = s.notice;
    if (!s.config_dir.empty())
        if (auto c = read_choice(choice_file(s.config_dir))) {
            v.chosen = c->folder;
            v.pending = !c->copy_from.empty();
        }
    return v;
}
// `a` is `b` or a folder inside it, after following symbolic links (case-insensitive on Windows and
// macOS, whose usual file systems ignore case)
inline bool inside(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    fs::path x = fs::weakly_canonical(a, ec);
    if (ec) x = a;
    fs::path y = fs::weakly_canonical(b, ec);
    if (ec) y = b;
#if defined(_WIN32) || defined(__APPLE__)
    x = fs::path(lower(x.lexically_normal().string()));
    y = fs::path(lower(y.lexically_normal().string()));
#else
    x = x.lexically_normal();
    y = y.lexically_normal();
#endif
    const fs::path rel = x.lexically_relative(y);
    return !rel.empty() && *rel.begin() != "..";
}
// the same folder, through another case (Windows, macOS) or a symbolic link
inline bool same_folder(const fs::path& a, const fs::path& b) { return inside(a, b) && inside(b, a); }
// A folder that already holds other things (for example the cloud folder itself) gets the save in a
// "Wind Waker HD" folder inside it, so backups copy only the save. Empty folders and save folders
// (with a user folder) are used as they are.
inline fs::path save_folder_in(const fs::path& folder) {
    std::error_code ec;
    if (!fs::is_directory(folder, ec) || fs::is_directory(folder / "user", ec)) return folder;
    for (const auto& e : fs::directory_iterator(folder, ec)) {
        const std::string name = e.path().filename().string();
        if (name != "wwhd-playing.lock" && name != ".DS_Store" && lower(name) != "desktop.ini") return folder / "Wind Waker HD";
    }
    return folder;
}
// Settings > Saves: a new folder (empty: back to the default one) for the next start. A failure is
// shown as the Saves tab's notice.
inline bool choose(const std::string& folder) {
    auto& s = state();
    std::lock_guard guard(s.mu);
    if (s.config_dir.empty()) return s.notice = "The save folder can't be changed in this session (--save or a test run).", false;
    const fs::path current = s.folder.empty() ? s.default_folder : s.folder;
    fs::path picked = folder.empty() ? s.default_folder : fs::absolute(fs::path(folder)).lexically_normal();
    if (same_folder(picked, current)) picked = current;  // the same folder, spelled another way
    else if (same_folder(picked, s.default_folder)) picked = s.default_folder;
    const fs::path next = folder.empty() || picked == current ? picked : save_folder_in(picked);
    if (next != current && (inside(next, current) || inside(current, next)))
        return s.notice = "The new save folder can't be inside the current one, or the other way round: choose a separate folder.", false;
    Choice c;
    if (next != s.default_folder) c.folder = next.string();
    if (next != current) c.copy_from = current.string();
    else if (auto old = read_choice(choice_file(s.config_dir))) c.had_save = old->had_save;
    if (!write_choice(choice_file(s.config_dir), c)) return s.notice = "The new save folder could not be remembered.", false;
    s.notice = next != picked ? "That folder already holds other files, so your save goes in " + next.string() + "." : std::string();
    return true;
}
}  // namespace save_folder
