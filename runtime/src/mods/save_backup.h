// Save backups for experimental mods (Mods tab > Save backups).
//
// A backup is a copy of the whole save folder the port uses (config::save_dir: "save" in the working
// directory, on Android the app's external files folder) in "save-backups" next to it:
//   save-backups/<YYYY-MM-DD_HH-MM-SS>-<reason>/backup.json   when, why, which mod, size
//   save-backups/<YYYY-MM-DD_HH-MM-SS>-<reason>/save/...      the copied files
// A backup is copied into a hidden temporary folder first and renamed into place when complete;
// an existing backup is never overwritten (a second one in the same second gets "-2", ...).
// Automatic backups (before an experimental mod, before a restore) keep the newest kKeepAutomatic, and
// so do, counted separately, the backups made for a chosen save folder (save_folder.h);
// manual ones ("Back up now") are never deleted. Folders without our backup.json are never touched.
//
// Restoring never replaces files the running game may have open: restore() only stages the choice
// (restore-pending.json) and apply_pending() performs it at the next start, before the game reads
// any save, after backing up the current save ("before restore") so a restore can be undone.
// Nothing here runs per frame; list() reads the folder only after a change.
#pragma once
#include "../exception_report.h"
#include "mod_json.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace mods::saves {
namespace fs = std::filesystem;

inline constexpr size_t kKeepAutomatic = 10;
inline constexpr uint64_t kMaxBytes = 1ull << 30;   // a Wind Waker HD save folder is well below 1 MiB
inline constexpr size_t kMaxFiles = 4096;
inline constexpr uint64_t kSpareBytes = 16ull << 20; // free space left over after a copy

struct Info {
    std::string name;      // folder name in save-backups
    std::string reason;    // "manual", "before-mod", "before-restore", "save-folder", "cloud-conflict"
    std::string mod, mod_name;
    bool automatic = true;
    int64_t created = 0;   // seconds since the epoch
    uint64_t bytes = 0;
    size_t files = 0;
    std::string when() const {  // local time, for display
        std::time_t t = std::time_t(created);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char text[32];
        std::strftime(text, sizeof text, "%Y-%m-%d %H:%M:%S", &tm);
        return text;
    }
    std::string description() const {
        if (reason == "manual") return "Manual backup";
        if (reason == "before-restore") return "Before a restore";
        if (reason == "save-folder") return "Before a save folder change";
        if (reason == "cloud-conflict") return "Before choosing between cloud copies";
        return "Before enabling " + (mod_name.empty() ? mod : mod_name);
    }
};
struct Result {
    bool ok = false;
    bool created = false;  // false with ok: nothing to back up (no save data yet)
    std::string name, error, message;
};

// Bytes as "12.3 KB" for the list.
inline std::string size_text(uint64_t bytes) {
    char text[32];
    if (bytes < 1024) snprintf(text, sizeof text, "%llu bytes", (unsigned long long)bytes);
    else if (bytes < 1024 * 1024) snprintf(text, sizeof text, "%.1f KB", bytes / 1024.0);
    else snprintf(text, sizeof text, "%.1f MB", bytes / (1024.0 * 1024.0));
    return text;
}

class Store {
public:
    // Seams for tests (time, disk space, a copy that fails like a full disk); defaults are the real ones.
    std::function<std::time_t()> now = [] { return std::time(nullptr); };
    std::function<uint64_t(const fs::path&)> free_space = [](const fs::path& p) {
        std::error_code ec;
        auto info = fs::space(p, ec);
        return ec ? UINT64_MAX : uint64_t(info.available);
    };
    std::function<void(const fs::path&, const fs::path&)> copy_file = [](const fs::path& from, const fs::path& to) {
        fs::copy_file(from, to, fs::copy_options::none);
    };
    std::function<void(const std::string&)> log = [](const std::string& line) { fprintf(stderr, "%s\n", line.c_str()); };
    // Used by remove(): the rename that takes a backup out of the list, then the removal of its files.
    std::function<void(const fs::path&, const fs::path&)> move = [](const fs::path& from, const fs::path& to) { fs::rename(from, to); };
    std::function<void(const fs::path&)> remove_tree = [](const fs::path& path) { fs::remove_all(path); };

    Store() = default;
    explicit Store(const fs::path& save_dir, const fs::path& backup_root = {}) { configure(save_dir, backup_root); }
    void configure(const fs::path& save_dir, const fs::path& backup_root = {}) {
        save_ = fs::absolute(save_dir).lexically_normal();
        if (!save_.has_filename()) save_ = save_.parent_path();
        root_ = backup_root.empty() ? save_.parent_path() / "save-backups" : fs::absolute(backup_root).lexically_normal();
        cache_valid_ = false;
        pending_cache_.reset();
    }
    bool configured() const { return !save_.empty(); }
    const fs::path& save_dir() const { return save_; }
    const fs::path& root() const { return root_; }

    // Copies the save folder. reason: "manual", "before-mod" (mod = its ID), "before-restore", or
    // "save-folder" / "cloud-conflict" (save_folder.h).
    // Nothing to copy (no save folder or no files yet) succeeds without creating a backup.
    Result create(const std::string& reason, const std::string& mod = {}, const std::string& mod_name = {}) {
        Result r;
        fs::path temporary;
        try {
            need(configured(), "Save backups are unavailable: the save folder is unknown");
            need(reason == "manual" || reason == "before-restore" || reason == "save-folder" || reason == "cloud-conflict" ||
                     (reason == "before-mod" && valid_id(mod)),
                 "Invalid backup reason");
            const auto source = resolved_save();
            if (!fs::exists(source) || count(source).first == 0) {
                r.ok = true;
                r.message = "No save data yet; nothing to back up.";
                log("[save-backup] " + r.message);
                return r;
            }
            fs::create_directories(root_);
            need(!fs::is_symlink(root_), "The save-backups folder is a link; refusing to write there");
            cleanup_temporary();
            Snapshot before, after;
            bool consistent = false;
            for (int attempt = 0; attempt < 3 && !consistent; attempt++) {
                if (!temporary.empty()) remove_quietly(temporary);
                before = snapshot(source);
                need(before.files.size() <= kMaxFiles && before.bytes <= kMaxBytes, "The save folder is unexpectedly large; not backed up");
                need(free_space(root_) >= before.bytes + kSpareBytes, "Not enough free disk space for a save backup");
                temporary = unique(root_ / (".tmp-" + std::to_string(now()) + "-" + std::to_string(serial()++)));
                fs::create_directory(temporary);
                copy_tree(source, temporary / "save", before);
                after = snapshot(source);
                consistent = after.files == before.files;
            }
            need(consistent, "The game is writing its save right now; try again in a moment");
            json::Value meta;
            meta["format_version"] = 1;
            meta["reason"] = reason;
            meta["automatic"] = reason != "manual";
            meta["created"] = double(now());
            meta["bytes"] = double(before.bytes);
            meta["files"] = double(before.files.size());
            if (!mod.empty()) meta["mod"] = mod;
            if (!mod_name.empty()) meta["mod_name"] = mod_name.substr(0, 256);
            write_text(temporary / "backup.json", json::dump(meta) + "\n");
            // a short folder name (issue #144: save paths stay well below Windows' MAX_PATH); the reason
            // and the mod are in backup.json. Releases up to v0.2.12 wrote <time>-before-<mod id>.
            auto suffix = reason == "before-mod" ? "mod-" + mod.substr(0, 7) : reason == "before-restore" ? std::string("restore") : reason;
            while (suffix.ends_with('.')) suffix.pop_back();  // Windows folder names cannot end with a dot
            auto target = unique(root_ / (stamp(now()) + "-" + suffix));
            fs::rename(temporary, target);  // the backup appears complete or not at all
            temporary.clear();
            r.ok = r.created = true;
            r.name = target.filename().string();
            r.message = "Saves backed up to save-backups/" + r.name;
            log("[save-backup] created " + r.name + " (" + std::to_string(before.files.size()) + " files, " +
                size_text(before.bytes) + (mod.empty() ? "" : ", for " + mod) + ")");
            cache_valid_ = false;
            prune();
        } catch (const std::exception& e) {
            if (!temporary.empty()) remove_quietly(temporary);
            r = {};
            r.error = std::string("Save backup failed: ") + e.what();
            log("[save-backup] " + r.error);
            cache_valid_ = false;
        }
        return r;
    }

    // Newest first. Cached until something changes.
    const std::vector<Info>& list() {
        if (cache_valid_) return cache_;
        cache_.clear();
        std::error_code ec;
        if (configured() && fs::is_directory(root_, ec))
            for (const auto& entry : fs::directory_iterator(root_, ec)) {
                auto info = read_info(entry.path());
                if (info) cache_.push_back(std::move(*info));
            }
        std::sort(cache_.begin(), cache_.end(), [](const Info& a, const Info& b) {
            return a.created != b.created ? a.created > b.created : a.name > b.name;
        });
        cache_valid_ = true;
        return cache_;
    }
    void invalidate() { cache_valid_ = false; }

    // Stages a restore for the next start. The backup must exist and be complete.
    bool restore(const std::string& name, std::string& error) {
        try {
            need(configured(), "Save backups are unavailable");
            need(read_info(root_ / name).has_value(), "That backup is missing or incomplete");
            json::Value pending;
            pending["format_version"] = 1;
            pending["backup"] = name;
            auto temporary = root_ / ".restore-pending.json.tmp";
            write_text(temporary, json::dump(pending) + "\n");
            pending_cache_.reset();
            fs::rename(temporary, root_ / "restore-pending.json");
            pending_cache_.reset();
            log("[save-backup] restore of " + name + " staged for the next start");
            error.clear();
            return true;
        } catch (const std::exception& e) {
            error = std::string("Could not stage the restore: ") + e.what();
            log("[save-backup] " + error);
            return false;
        }
    }
    // The backup staged for the next start, or empty. Cached: the Mods tab asks every frame.
    std::string pending() const {
        if (!configured()) return {};
        if (pending_cache_) return *pending_cache_;
        std::string name;
        try {
            auto file = root_ / "restore-pending.json";
            if (fs::is_regular_file(file)) name = json::parse(read_text(file)).get("backup").string();
            if (!valid_name(name)) name.clear();
        } catch (const std::exception&) { name.clear(); }
        pending_cache_ = name;
        return name;
    }
    bool cancel_restore(std::string& error) {
        std::error_code ec;
        fs::remove(root_ / "restore-pending.json", ec);
        pending_cache_.reset();
        if (ec) { error = "Could not cancel the restore: " + ec.message(); return false; }
        log("[save-backup] staged restore cancelled");
        error.clear();
        return true;
    }

    // At startup, before the game reads saves. No staged restore: ok, nothing done.
    // The current save is backed up first; any failure leaves the save folder as it was.
    Result apply_pending() {
        Result r;
        if (!configured() || !fs::exists(root_ / "restore-pending.json")) { r.ok = true; return r; }
        auto name = pending();
        std::error_code ignored;
        // A failure is reported once, not retried at every start.
        struct Unstage {
            fs::path file;
            std::optional<std::string>& cache;
            ~Unstage() { std::error_code ec; fs::remove(file, ec); cache.reset(); }
        } unstage{root_ / "restore-pending.json", pending_cache_};
        fs::path staged, replaced;
        bool moved_away = false;
        fs::path target;
        try {
            need(!name.empty(), "The staged restore is unreadable");
            auto info = read_info(root_ / name);
            need(info.has_value(), "Backup " + name + " is missing or incomplete");
            auto before = create("before-restore");  // while still staged, so pruning keeps the chosen backup
            need(before.ok, before.error);
            target = resolved_save();
            const auto parent = target.parent_path();
            fs::create_directories(parent);
            staged = unique(parent / (".save-restore-" + std::to_string(now())));
            fs::create_directory(staged);
            auto source = root_ / name / "save";
            if (fs::exists(source)) copy_tree(source, staged, snapshot(source));
            if (fs::exists(target)) {
                replaced = unique(parent / (".save-replaced-" + std::to_string(now())));
                fs::rename(target, replaced);
                moved_away = true;
            }
            fs::rename(staged, target);
            staged.clear();
            if (moved_away) remove_quietly(replaced);
            r.ok = r.created = true;
            r.name = name;
            r.message = "Restored saves from " + info->when() + " (" + info->description() + ")." +
                        (before.created ? " Your previous saves were backed up first (" + before.name + ")." : "");
            log("[save-backup] restored " + name + (before.created ? "; previous saves in " + before.name : ""));
        } catch (const std::exception& e) {
            if (moved_away && !fs::exists(target)) fs::rename(replaced, target, ignored);
            if (!staged.empty()) remove_quietly(staged);
            r = {};
            r.error = std::string("Restore failed; your saves were not changed: ") + e.what();
            log("[save-backup] " + r.error);
        }
        cache_valid_ = false;
        return r;
    }

    // Deletes one backup (manual or automatic) at the player's request. Only a complete backup this
    // store made (backup.json) directly inside save-backups, never a link, never the one staged for
    // restore. It is first renamed to a hidden name, so it leaves the list at once and an interrupted
    // removal never leaves a half-deleted backup that still lists; leftovers are cleared later.
    bool remove(const std::string& name, std::string& error) {
        try {
            need(configured(), "Save backups are unavailable");
            need(valid_name(name), "Not a save backup");
            const auto folder = root_ / name;
            need(!fs::is_symlink(root_) && !fs::is_symlink(folder), "Not a save backup");
            need(read_info(folder).has_value(), "Not a save backup (no backup.json)");
            need(fs::canonical(folder).parent_path() == fs::canonical(root_), "Not a save backup");
            need(name != pending(), "This backup is staged for restore; cancel the restore first");
            auto hidden = unique(root_ / (".deleted-" + std::to_string(now()) + "-" + std::to_string(serial()++)));
            move(folder, hidden);
            cache_valid_ = false;
            log("[save-backup] deleted " + name);
            try { remove_tree(hidden); }
            catch (const std::exception& e) { log("[save-backup] " + name + ": files are removed later (" + e.what() + ")"); }
            error.clear();
            return true;
        } catch (const std::exception& e) {
            error = std::string("Could not delete the backup: ") + e.what();
            log("[save-backup] " + error);
            cache_valid_ = false;
            return false;
        }
    }

    // Keeps the newest kKeepAutomatic automatic backups (and always the one staged for restore). The
    // backups of a save folder's other copies (save_folder.h) are counted on their own, so a run of
    // mod backups never removes the only other side of a cloud conflict.
    void prune() {
        auto keep = pending();
        size_t automatic = 0, folder = 0;
        for (const auto& info : std::vector<Info>(list())) {
            if (!info.automatic) continue;
            const bool folder_copy = info.reason == "save-folder" || info.reason == "cloud-conflict";
            if (++(folder_copy ? folder : automatic) <= kKeepAutomatic || info.name == keep) continue;
            std::error_code ec;
            fs::remove_all(root_ / info.name, ec);
            log("[save-backup] removed old automatic backup " + info.name + (ec ? " (failed: " + ec.message() + ")" : ""));
        }
        cache_valid_ = false;
    }

private:
    struct Snapshot {
        std::map<std::string, std::pair<uint64_t, int64_t>> files;  // relative path -> size, mtime
        uint64_t bytes = 0;
    };
    fs::path save_, root_;
    std::vector<Info> cache_;
    bool cache_valid_ = false;
    mutable std::optional<std::string> pending_cache_;

    static void need(bool ok, const std::string& why) { if (!ok) exception_report::raise(why); }
    static uint64_t& serial() { static uint64_t n = 0; return n; }
    static bool valid_id(const std::string& id) {
        return !id.empty() && id.size() <= 64 && id[0] != '.' && std::all_of(id.begin(), id.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        });
    }
    static bool valid_name(const std::string& name) { return valid_id(name) && name.size() <= 128; }
    static std::string stamp(std::time_t t) {
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char text[32];
        std::strftime(text, sizeof text, "%Y-%m-%d_%H-%M-%S", &tm);
        return text;
    }
    static fs::path unique(const fs::path& base) {
        auto candidate = base;
        for (int n = 2; fs::exists(candidate) || fs::is_symlink(candidate); n++)
            candidate = base.parent_path() / (base.filename().string() + "-" + std::to_string(n));
        return candidate;
    }
    static std::string read_text(const fs::path& file) {
        need(fs::file_size(file) <= 64 * 1024, "Backup metadata is too large");
        std::ifstream in(file, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), {}};
    }
    static void write_text(const fs::path& file, const std::string& text) {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << text;
        out.close();
        need(bool(out), "Cannot write " + file.filename().string());
    }
    static void remove_quietly(const fs::path& path) { std::error_code ec; fs::remove_all(path, ec); }
    // A save folder that is itself a link (e.g. into a synced folder) is backed up and restored at
    // its target, so the link stays in place.
    fs::path resolved_save() const {
        std::error_code ec;
        if (fs::is_symlink(save_, ec)) return fs::canonical(save_);
        return save_;
    }
    void cleanup_temporary() {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(root_, ec))
            if (entry.path().filename().string().starts_with(".tmp-") || entry.path().filename().string().starts_with(".deleted-"))
                remove_quietly(entry.path());
    }
    static std::pair<size_t, uint64_t> count(const fs::path& dir) {
        size_t files = 0;
        uint64_t bytes = 0;
        for (const auto& entry : fs::recursive_directory_iterator(dir))
            if (entry.is_regular_file()) { files++; bytes += entry.file_size(); }
        return {files, bytes};
    }
    static Snapshot snapshot(const fs::path& dir) {
        Snapshot s;
        for (const auto& entry : fs::recursive_directory_iterator(dir)) {
            need(!entry.is_symlink(), "The save folder contains a link; not backed up");
            if (entry.is_directory()) continue;
            need(entry.is_regular_file(), "The save folder contains a special file; not backed up");
            auto size = entry.file_size();
            auto time = entry.last_write_time().time_since_epoch().count();
            s.files[entry.path().lexically_relative(dir).generic_string()] = {size, int64_t(time)};
            s.bytes += size;
            need(s.files.size() <= kMaxFiles, "The save folder has too many files; not backed up");
        }
        return s;
    }
    void copy_tree(const fs::path& from, const fs::path& to, const Snapshot& files) {
        fs::create_directories(to);
        for (const auto& entry : fs::recursive_directory_iterator(from))
            if (entry.is_directory()) fs::create_directories(to / entry.path().lexically_relative(from));
        for (const auto& [relative, info] : files.files) {
            auto destination = to / fs::path(relative);
            fs::create_directories(destination.parent_path());
            copy_file(from / fs::path(relative), destination);
        }
    }
    std::optional<Info> read_info(const fs::path& folder) const {
        try {
            auto name = folder.filename().string();
            if (!valid_name(name) || fs::is_symlink(folder) || !fs::is_directory(folder)) return std::nullopt;
            auto file = folder / "backup.json";
            if (!fs::is_regular_file(file) || fs::is_symlink(file)) return std::nullopt;
            auto v = json::parse(read_text(file));
            if (v.get("format_version").number != 1) return std::nullopt;
            Info info;
            info.name = name;
            info.reason = v.get("reason").string();
            if (info.reason != "manual" && info.reason != "before-mod" && info.reason != "before-restore" && info.reason != "save-folder" &&
                info.reason != "cloud-conflict")
                return std::nullopt;
            info.automatic = info.reason != "manual";  // a hand-edited flag never makes a manual backup prunable
            info.mod = v.get("mod").string();
            info.mod_name = v.get("mod_name").string();
            info.created = int64_t(v.get("created").number);
            info.bytes = uint64_t(std::max(0.0, v.get("bytes").number));
            info.files = size_t(std::max(0.0, v.get("files").number));
            return info;
        } catch (const std::exception&) { return std::nullopt; }
    }
};

// The game's store, configured once at startup (main.cpp) from the save folder in use. Hold mutex()
// for every use after startup (the Mods tab and the mod manager share it).
inline std::mutex& mutex() { static std::mutex m; return m; }
inline Store& store() { static Store s; return s; }
// The startup restore's outcome, shown once in the Mods tab.
inline std::string& startup_notice() { static std::string text; return text; }

// main(): before the game reads any save. Applies a staged restore.
// backup_root: save_folder.h keeps backups of a chosen (cloud) save folder in the local save-backups.
inline void startup(const fs::path& save_dir, std::function<void(const std::string&)> log = {}, bool apply = true,
                    const fs::path& backup_root = {}) {
    std::lock_guard guard(mutex());
    auto& s = store();
    if (log) s.log = std::move(log);
    try { s.configure(save_dir, backup_root); } catch (const std::exception& e) { s.log(std::string("[save-backup] unavailable: ") + e.what()); return; }
    if (!apply) return;
    auto result = s.apply_pending();
    if (!result.ok) startup_notice() = result.error;
    else if (result.created) startup_notice() = result.message;
}
}  // namespace mods::saves
