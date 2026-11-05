// SPDX-License-Identifier: MPL-2.0
// Achievements: what is earned, and the check that earns it (achievements.cpp drives it once a second of game time).
// Pure: achievements_test.
//
//   Store    earned for good, twice: for all save games (key -> the time it was earned, whichever file earned it) and for
//            each save game ("<file number>:<name>" -> key -> time). Kept as JSON in the port's own data folder
//            (achievements.json), never in the game's save file; a save state or a reload never takes one back.
//   Checker  a goal is earned once it holds in two reads in a row (a stage being loaded changes the working copy and its
//            stage number one after the other); a goal on a change ("once") at the first read it holds in. Two reads in a
//            row that meet nothing at all are the start of a new game in that slot: it forgets what the slot's earlier
//            save game had (not the record for all). Nothing is earned while something pauses them (a cheat that keeps
//            running, or one used in this run: the caller names them), unless "Earn with cheats" is on.
// From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (MPL-2.0), Achievements.java check(), newFile(), paused().
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "achievements_goals.h"
#include "mod_json.h"

namespace achievements {

struct Options {
    bool with_cheats = false;  // "Earn with cheats"
};

class Store {
public:
    int64_t earned_all(const std::string& key) const { return get(all_, key); }
    int64_t earned_file(const std::string& file, const std::string& key) const {
        auto f = files_.find(file);
        return f == files_.end() ? 0 : get(f->second, key);
    }
    unsigned count(const std::string* file) const {
        unsigned n = 0;
        for (const Goal& g : kAll) n += (file ? earned_file(*file, g.key) : earned_all(g.key)) != 0;
        return n;
    }
    unsigned points(const std::string* file) const {
        unsigned n = 0;
        for (const Goal& g : kAll)
            if ((file ? earned_file(*file, g.key) : earned_all(g.key)) != 0) n += g.points;
        return n;
    }
    void earn(const std::string& file, const std::string& key, int64_t when) {
        if (!all_.count(key)) all_[key] = when, changed_ = true;
        auto& f = files_[file];
        if (!f.count(key)) f[key] = when, changed_ = true;
    }
    void forget_file(const std::string& file) {
        if (files_.erase(file)) changed_ = true;
    }
    void reset() {
        if (!all_.empty() || !files_.empty()) changed_ = true;
        all_.clear(), files_.clear();
    }
    // written since the last take_changed()
    bool take_changed() {
        const bool c = changed_;
        changed_ = false;
        return c;
    }

    std::string text() const {
        mods::json::Value v;
        v["format"] = 1;
        mods::json::Value& all = v["all"];
        all.type = mods::json::Value::Object;
        for (const auto& [k, t] : all_) all[k] = double(t);
        mods::json::Value& files = v["files"];
        files.type = mods::json::Value::Object;
        for (const auto& [f, keys] : files_) {
            mods::json::Value& one = files[f];
            one.type = mods::json::Value::Object;
            for (const auto& [k, t] : keys) one[k] = double(t);
        }
        return mods::json::dump(v);
    }
    // false (and nothing changed) for a file that isn't one of ours
    bool parse(const std::string& s) {
        mods::json::Value v;
        try {
            v = mods::json::parse(s);
        } catch (...) {
            return false;
        }
        if (v.type != mods::json::Value::Object || v.get("format").number != 1) return false;
        std::map<std::string, int64_t> all;
        std::map<std::string, std::map<std::string, int64_t>> files;
        for (const auto& [k, t] : v.get("all").object)
            if (t.type == mods::json::Value::Number && t.number > 0) all[k] = int64_t(t.number);
        for (const auto& [f, keys] : v.get("files").object)
            for (const auto& [k, t] : keys.object)
                if (t.type == mods::json::Value::Number && t.number > 0) files[f][k] = int64_t(t.number);
        all_ = std::move(all), files_ = std::move(files);
        return true;
    }

private:
    std::map<std::string, int64_t> all_;
    std::map<std::string, std::map<std::string, int64_t>> files_;
    bool changed_ = false;
    static int64_t get(const std::map<std::string, int64_t>& m, const std::string& k) {
        auto it = m.find(k);
        return it == m.end() ? 0 : it->second;
    }
};

// what one check earned
struct Earned {
    std::vector<std::string> keys;        // new for the save game in play
    std::vector<std::string> new_for_all; // ... of them, new for all save games
    bool paused = false;
};

class Checker {
public:
    // d: the save data now; stage: the stage in play; file: "<number>:<name>"; now_ms: the time to note;
    // pausing: what pauses them now (empty: nothing)
    Earned check(const unsigned char* d, const char* stage, bool hero, const std::string& file, int64_t now_ms, int64_t when,
                 Store& store, const Options& opt, const std::vector<std::string>& pausing) {
        Earned out;
        if (!pausing.empty() && !opt.with_cheats) {
            out.paused = true;
            have_prev_ = false, pending_.assign(kGoals, false);
            return out;
        }
        if (file != file_) {  // another save game: the reads before were of the other one
            file_ = file, have_prev_ = false, quiet_ = 0;
            pending_.assign(kGoals, false);
        }
        if (pending_.size() != kGoals) pending_.assign(kGoals, false);
        Read r;
        r.d = d, r.hero = hero, r.now_ms = now_ms, r.trees_from = &trees_from_[file];
        std::snprintf(r.stage, sizeof r.stage, "%s", stage ? stage : "");
        Read p;
        if (have_prev_) {
            p.d = prev_, p.hero = prev_hero_, p.now_ms = now_ms, p.trees_from = r.trees_from;
            std::snprintf(p.stage, sizeof p.stage, "%s", prev_stage_);
            r.prev = &p;
        }
        bool any = false;
        std::vector<bool> met(kGoals, false);
        for (unsigned i = 0; i < kGoals; i++) {
            const Goal& g = kAll[i];
            met[i] = g.met(r);
            if (std::string(g.key) != "hero_mode") any = any || met[i];  // (Hero Mode holds from a new file's start)
            const bool all = store.earned_all(g.key) != 0, here = store.earned_file(file, g.key) != 0;
            if (all && here) continue;
            if (met[i] && (g.once || pending_[i])) {
                store.earn(file, g.key, when);
                if (!here) out.keys.push_back(g.key);
                if (!all) out.new_for_all.push_back(g.key);
                met[i] = false;
            }
        }
        quiet_ = any ? 0 : quiet_ + 1;
        if (quiet_ == 2) store.forget_file(file);  // a new game in this slot
        pending_ = std::move(met);
        std::memcpy(prev_, d, sizeof prev_);
        std::snprintf(prev_stage_, sizeof prev_stage_, "%s", r.stage);
        prev_hero_ = hero, have_prev_ = true;
        return out;
    }
    // a save state was loaded or the game was reset: the next read is a first read
    void restart() { have_prev_ = false, pending_.assign(kGoals, false), file_.clear(); }

private:
    std::string file_;
    std::vector<bool> pending_;
    unsigned char prev_[GP_SAVE_LEN] = {};
    char prev_stage_[9] = {};
    bool prev_hero_ = false, have_prev_ = false;
    unsigned quiet_ = 0;
    std::map<std::string, int64_t> trees_from_;
};

}  // namespace achievements
