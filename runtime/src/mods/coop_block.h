// SPDX-License-Identifier: MPL-2.0
// Online co-op: blocked and muted players (GAPS C14, DESIGN.md 3.7).
//
// Block hides a player outside the party for good: no puppet, no sounds, effects or emotes, no dot on the radar, and
// the server is told (it stops relaying them to you and you to them). Mute keeps them visible but silent: no sounds,
// effects or emotes. Both go by the player's key (a hash of their public key, the same on every server and visit),
// are kept in the user folder (online-blocks.txt), sent to the server again at each fresh login, and can be undone
// on the Online page. Party members are never blocked or muted here (leave the party instead).
//
// The pure part here (the list, its file form) is checked by coop_block_test without the game.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace mods::coop::block {

enum Kind : uint8_t { kBlocked = 1, kMuted = 2 };
struct Entry {
    uint64_t key = 0;
    Kind kind = kBlocked;
    std::string name;  // as they were shown when blocked (for the list only)
};
constexpr size_t kMaxEntries = 4096, kMaxName = 32;

class List {
public:
    // block or mute (a block replaces a mute and the other way); false: no room or a party member's key 0
    bool set(uint64_t key, Kind kind, const std::string& name) {
        if (!key) return false;
        for (Entry& e : entries_)
            if (e.key == key) return e.kind = kind, e.name = clean(name.empty() ? e.name : name), true;
        if (entries_.size() >= kMaxEntries) return false;
        entries_.push_back({key, kind, clean(name)});
        return true;
    }
    bool clear(uint64_t key) {
        const auto it = std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.key == key; });
        const bool had = it != entries_.end();
        entries_.erase(it, entries_.end());
        return had;
    }
    bool blocked(uint64_t key) const { return key && find(key) && find(key)->kind == kBlocked; }
    // muted or blocked: nothing of theirs is heard
    bool silent(uint64_t key) const { return key && find(key) != nullptr; }
    const Entry* find(uint64_t key) const {
        for (const Entry& e : entries_) if (e.key == key) return &e;
        return nullptr;
    }
    const std::vector<Entry>& entries() const { return entries_; }

    // the file: one line each, "block|mute <16 hex digits> <name>"; bad lines are left out
    std::string text() const {
        std::string out;
        for (const Entry& e : entries_) {
            char b[64];
            snprintf(b, sizeof b, "%s %016llx ", e.kind == kBlocked ? "block" : "mute", (unsigned long long)e.key);
            out += b + e.name + "\n";
        }
        return out;
    }
    static List parse(const std::string& text) {
        List l;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            const size_t a = line.find(' ');
            if (a == std::string::npos) continue;
            const std::string kind = line.substr(0, a);
            const size_t b = line.find(' ', a + 1);
            const std::string hex = line.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
            if ((kind != "block" && kind != "mute") || hex.size() != 16 || hex.find_first_not_of("0123456789abcdef") != std::string::npos) continue;
            l.set(std::strtoull(hex.c_str(), nullptr, 16), kind == "block" ? kBlocked : kMuted, b == std::string::npos ? "" : line.substr(b + 1));
        }
        return l;
    }

private:
    static std::string clean(const std::string& s) {
        std::string o;
        for (char c : s) if ((unsigned char)c >= 0x20 && c != 0x7F && o.size() < kMaxName) o += c;
        return o;
    }
    std::vector<Entry> entries_;
};

// ---- the port (coop_block.cpp; any thread)
List current();
// block / mute / undo a player now shown (by player number); false: not one outside the party, or no key
bool set(int id, Kind kind, std::string& why);
bool undo(uint64_t key);
bool blocked_id(int id);   // that player is blocked: no puppet, nothing of theirs
bool silent_id(int id);    // muted or blocked: no sounds, effects or emotes
// a fresh login to a server: the lists sent to it again
void resend();

}  // namespace mods::coop::block
