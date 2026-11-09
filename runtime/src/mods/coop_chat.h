// SPDX-License-Identifier: MPL-2.0
// Online co-op: text chat through a server (docs/online-coop.md, "Chat"; the server's side: docs/online-server.md).
//
// Two channels, each a tab of the chat box: Party (the party members) and Global (everyone on the server; only where
// the server allows it). Party is the active tab at first; lines of the active tab show in the box and fade after a
// few seconds, lines of the other tab count as unread on that tab until the player switches to it. A line typed goes
// to the active tab's channel; quick phrases too. Settings > Online turns each channel off (its tab and its lines go
// away, nothing is sent on it). Muting a player (here, locally) hides their lines for good; the list is kept in the
// user folder (online-chat-mutes.txt). A server without chat (or one that never said) shows no chat at all.
//
// The pure part here (the box: tabs, unread counts, history, fading, mutes, the mute file) is checked by
// coop_chat_test without the game; coop_chat.cpp draws it and takes the input.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <sstream>
#include <string>
#include <vector>

namespace mods::coop::chat {

enum Channel : int { kParty = 0, kGlobal = 1, kChannels = 2 };
inline const char* channel_name(int c) { return c == kGlobal ? "Global" : "Party"; }

struct Rules {  // the server's (CHAT_RULES)
    bool enabled = false, global = false;
    int max_length = 120;
    bool operator==(const Rules&) const = default;
};
struct Prefs {  // the player's (Settings > Online)
    bool party = true, global = true;
    bool operator==(const Prefs&) const = default;
};

struct Line {
    int channel = kParty;
    uint64_t key = 0;        // the sender's key hash (0: a note of this game's, not a player's)
    std::string name, text;
    bool party = false;      // the sender is in this player's party
    bool yours = false;      // this player's own line, as the server passed it on
    uint32_t at = 0;         // when it came (ms)
};

constexpr size_t kHistory = 50;         // lines kept per channel
constexpr uint32_t kShowMs = 8000;      // a new line shows this long in the box ...
constexpr uint32_t kFadeMs = 1500;      // ... then fades out over this
constexpr size_t kBoxLines = 6;         // at most this many lines in the box (the newest)
constexpr size_t kMaxMutes = 1024, kMaxName = 32;

// the quick phrases (controller and touch: chosen without typing)
inline constexpr const char* kPhrases[] = {"Follow me", "Wait for me", "Thanks!", "Over here", "Let's go", "Need help"};
inline constexpr int kPhraseCount = int(sizeof kPhrases / sizeof kPhrases[0]);

class Box {
public:
    void set_rules(const Rules& r) { rules_ = r, fix_active(); }
    void set_prefs(const Prefs& p) {
        prefs_ = p;
        for (int c = 0; c < kChannels; c++)
            if (!has_tab(c)) unread_[c] = 0, hist_[c].clear();  // (a channel turned off: its tab and its lines go)
        fix_active();
    }
    const Rules& rules() const { return rules_; }
    const Prefs& prefs() const { return prefs_; }

    // the chat shows at all: the server has it and a channel is on
    bool shown() const { return !tabs().empty(); }
    bool has_tab(int c) const {
        if (!rules_.enabled) return false;
        return c == kParty ? prefs_.party : c == kGlobal ? rules_.global && prefs_.global : false;
    }
    std::vector<int> tabs() const {
        std::vector<int> t;
        for (int c = 0; c < kChannels; c++)
            if (has_tab(c)) t.push_back(c);
        return t;
    }
    int active() const { return active_; }  // (-1: no tab)
    void select(int c) {
        if (!has_tab(c)) return;
        active_ = c, unread_[c] = 0;
    }
    void next_tab() {
        const std::vector<int> t = tabs();
        if (t.empty()) return;
        const auto it = std::find(t.begin(), t.end(), active_);
        select(it == t.end() || it + 1 == t.end() ? t.front() : *(it + 1));
    }
    int unread(int c) const { return c >= 0 && c < kChannels ? unread_[c] : 0; }

    // a line from the server: false when it isn't kept (its tab is off, or the sender muted here)
    bool receive(Line l, uint32_t now) {
        if (l.channel < 0 || l.channel >= kChannels || !has_tab(l.channel)) return false;
        if (!l.yours && muted(l.key)) return false;
        l.at = now ? now : 1;
        const int c = l.channel;
        const bool unread = c != active_ && !l.yours;
        auto& h = hist_[c];
        if (h.size() >= kHistory) h.pop_front();
        h.push_back(std::move(l));
        if (unread) unread_[c] = std::min(unread_[c] + 1, 999);
        return true;
    }
    // a note of this game's in a channel (a line the server did not pass on, and why)
    void note(int channel, const std::string& text, uint32_t now) {
        Line l;
        l.channel = has_tab(channel) ? channel : active_;
        l.text = text;
        if (l.channel < 0) return;
        l.at = now ? now : 1;
        auto& h = hist_[l.channel];
        if (h.size() >= kHistory) h.pop_front();
        h.push_back(std::move(l));
    }
    const std::deque<Line>& history(int c) const {
        static const std::deque<Line> none;
        return c >= 0 && c < kChannels ? hist_[c] : none;
    }
    // the box while closed: the active tab's newest lines still showing, each with its opacity (1 .. 0)
    std::vector<std::pair<const Line*, float>> fading(uint32_t now) const {
        std::vector<std::pair<const Line*, float>> out;
        if (active_ < 0) return out;
        for (const Line& l : hist_[active_]) {
            const uint32_t age = now - l.at;
            if (age >= kShowMs + kFadeMs) continue;
            out.push_back({&l, age <= kShowMs ? 1.0f : 1.0f - float(age - kShowMs) / float(kFadeMs)});
        }
        if (out.size() > kBoxLines) out.erase(out.begin(), out.end() - kBoxLines);
        return out;
    }
    // the newest line of any tab came in the last `ms` (the closed box shows its tab bar meanwhile)
    bool recent(uint32_t now, uint32_t ms) const {
        for (int c = 0; c < kChannels; c++)
            if (!hist_[c].empty() && now - hist_[c].back().at < ms) return true;
        return false;
    }

    // ---- local mutes: by key hash, kept between sessions
    struct Mute {
        uint64_t key = 0;
        std::string name;
    };
    bool muted(uint64_t key) const {
        for (const Mute& m : mutes_)
            if (m.key == key) return key != 0;
        return false;
    }
    // mute (their lines go from the history too) or unmute; false: no key or the list is full
    bool mute(uint64_t key, const std::string& name, bool on) {
        if (!key) return false;
        const auto it = std::find_if(mutes_.begin(), mutes_.end(), [&](const Mute& m) { return m.key == key; });
        if (!on) {
            if (it == mutes_.end()) return false;
            mutes_.erase(it);
            return true;
        }
        if (it != mutes_.end()) return true;
        if (mutes_.size() >= kMaxMutes) return false;
        mutes_.push_back({key, clean_name(name)});
        for (auto& h : hist_)
            h.erase(std::remove_if(h.begin(), h.end(), [&](const Line& l) { return l.key == key && !l.yours; }), h.end());
        return true;
    }
    const std::vector<Mute>& mutes() const { return mutes_; }
    // the senders of the lines kept (newest first, each once, not this player): to mute from the list
    std::vector<Mute> senders() const {
        std::vector<std::pair<uint32_t, Mute>> all;
        for (const auto& h : hist_)
            for (const Line& l : h)
                if (l.key && !l.yours) all.push_back({l.at, {l.key, l.name}});
        std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        std::vector<Mute> out;
        for (const auto& [at, m] : all)
            if (std::none_of(out.begin(), out.end(), [&](const Mute& o) { return o.key == m.key; })) out.push_back(m);
        return out;
    }
    // the file: one line each, "mute <16 hex digits> <name>"; bad lines are left out
    std::string mutes_text() const {
        std::string out;
        for (const Mute& m : mutes_) {
            char b[40];
            std::snprintf(b, sizeof b, "mute %016llx ", (unsigned long long)m.key);
            out += b + m.name + "\n";
        }
        return out;
    }
    void parse_mutes(const std::string& text) {
        mutes_.clear();
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (line.compare(0, 5, "mute ") != 0 || line.size() < 21) continue;
            const std::string hex = line.substr(5, 16);
            if (hex.find_first_not_of("0123456789abcdef") != std::string::npos) continue;
            mute(std::strtoull(hex.c_str(), nullptr, 16), line.size() > 22 ? line.substr(22) : "", true);
        }
    }
    void clear_lines() {  // (the session ended: a new server's chat starts empty)
        for (auto& h : hist_) h.clear();
        unread_[0] = unread_[1] = 0;
    }

private:
    void fix_active() {
        if (has_tab(active_)) return;
        active_ = has_tab(kParty) ? kParty : has_tab(kGlobal) ? kGlobal : -1;  // (Party first)
    }
    static std::string clean_name(const std::string& s) {
        std::string o;
        for (char c : s)
            if ((unsigned char)c >= 0x20 && c != 0x7F && o.size() < kMaxName) o += c;
        return o;
    }
    Rules rules_;
    Prefs prefs_;
    int active_ = -1;
    int unread_[kChannels] = {};
    std::deque<Line> hist_[kChannels];
    std::vector<Mute> mutes_;
};

// ---- the port (coop_chat.cpp)
// any thread: from the server (coop_net_server.cpp)
void server_rules(const Rules& r);  // after each welcome; a session's end: Rules{} (no chat)
void server_line(const Line& l);
void server_result(int channel, const std::string& why);  // a line of this player's not passed on
void session_ended();
// any thread: opens the chat (keyboard: the line to type; controller and touch: the chat menu with the quick phrases)
void open_typing();
void open_menu();
bool available();  // the chat shows (a server with chat, a channel on)
// mute a player's chat here (by key; name for the list)
bool mute(uint64_t key, const std::string& name, bool on);
bool muted(uint64_t key);
std::vector<Box::Mute> mutes();
std::vector<Box::Mute> senders();
void register_overlay();
// the game's step: the settings followed, the test aids
void step();

}  // namespace mods::coop::chat
