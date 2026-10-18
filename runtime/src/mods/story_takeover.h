// Resident takeovers: a story mod's quest step that has a game resident say the mod's lines (and hand
// out a reward) instead of the resident's own line, for one talk (docs/mod-sdk-v2.md, "Talking to
// residents"). Pure code, unit-tested in runtime/tools/story_notebook_test.cpp.
//
// The rules (user decisions 2026-10-13/14), enforced here and not by the mods:
// - a takeover is live for every ACTIVE quest at its current step while its conditions hold (the
//   letter carried, the step's prerequisites); tracking is not needed;
// - one quest per talk: when several want the resident, the tracked quest's comes first, else the one
//   started first; the others come with later talks;
// - the port never sets or clears a game flag for it. The resident's own line selection does not run
//   for that talk, so a first-talk introduction it would have given is not lost but comes later.
// Mods can't hook a resident's dialogue themselves; they declare the step (wwhd_quest_talk).
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "story_notebook_list.h"
#include "story_actors_core.h"
#include "story_npc_catalogue.h"

namespace story_notebook {

// Residents the port can hand to a quest: the NPC catalogue's entries (story_npc_catalogue.h).

// A step's prerequisites (manifest "available"), checked read-only from the save: event flags the game
// set (or didn't) and the time of day. While they don't hold, the notebook says "Not yet possible:
// <text>", the step has no ring on the chart and its resident talk is not live.
struct Availability {
    std::vector<uint16_t> events, not_events;  // the game's event flag numbers (high byte: byte, low byte: mask)
    int time = 0;                              // 0 any, 1 day (6:00-18:00), 2 night
    std::string text;
    std::string flag_mod;std::vector<std::string> flags,not_flags;
    bool any() const { return !events.empty() || !not_events.empty() || time || !flags.empty() || !not_flags.empty(); }
};
// event_flags: the save's event flag block (0x100 bytes); hour: the game's hour of the day
inline bool event_flag(const uint8_t* event_flags, uint16_t flag) { return (event_flags[flag >> 8] & (flag & 0xFF)) != 0; }
inline bool mod_flags_available(const Availability&a,const story_mail::Book&book){
    if((!a.flags.empty()||!a.not_flags.empty())&&(!book.usable()||a.flag_mod.empty()))return false;
    for(const auto&f:a.flags)if(!story_actors::flag(book,a.flag_mod,f))return false;
    for(const auto&f:a.not_flags)if(story_actors::flag(book,a.flag_mod,f))return false;
    return true;
}
inline bool available(const Availability& a, const uint8_t* event_flags, int hour, const story_mail::Book& book=story_mail::Book(nullptr)) {
    if(!mod_flags_available(a,book))return false;
    for (uint16_t f : a.events)
        if (!event_flags || !event_flag(event_flags, f)) return false;
    for (uint16_t f : a.not_events)
        if (!event_flags || event_flag(event_flags, f)) return false;
    const bool day = hour >= 6 && hour < 18;
    if (a.time == 1 && !day) return false;
    if (a.time == 2 && day) return false;
    return true;
}

inline constexpr size_t kMaxLinesBytes = 240;  // one message box: up to three lines
using story_mail::kRecordCarried;  // Link carries the quest's letter: key quest_key, value 1

struct Takeover {
    uint16_t quest = 0;  // quest_key
    uint8_t step = 0;    // the step it belongs to (1..254)
    Resident resident = kResidentNone;
    bool needs_letter = true;
    std::string lines;   // UTF-8, '\n' between lines
    uint8_t reward = 0;  // a rupee item (0x01..0x06) or 0
    Availability when;   // the step's prerequisites (checked by the caller with the save's state)
    bool follow = true;  // the chart's marks follow the resident while it is loaded (manifest "follow")
    std::string chart_mod, chart;  // "then": {"give": "chart:<id>"}: the mod chart the talk gives Link
    std::string item;              // "then": {"give": "item:<id>"}: one of the mod's items (story_items.h), the mod is chart_mod's
};

inline bool carries(const story_mail::Book& b, uint16_t quest) { return b.get(kRecordCarried, quest) == 1; }

// Does this quest want the resident right now: the step it belongs to is the quest's current one, and
// its conditions hold (the letter carried, the step's prerequisites)?
inline bool wants(const Takeover& t, const story_mail::Book& b, const uint8_t* event_flags, int hour) {
    const uint8_t step = saved_step(b, t.quest);
    return step && step != kStepDone && t.step == step && (!t.needs_letter || carries(b, t.quest)) &&
           (event_flags ? available(t.when,event_flags,hour,b) : mod_flags_available(t.when,b));
}
// The live takeover for this resident, or null. Every active quest may want it (tracking is not
// needed): none, the game's line; one, that quest's line; several, the tracked quest's first, else the
// one started first. One quest per talk.
inline const Takeover* live_takeover(const std::vector<Takeover>& all, const story_mail::Book& b, Resident who,
                                     const uint8_t* event_flags = nullptr, int hour = 12) {
    if (!b.usable()) return nullptr;
    const uint16_t tracked = tracked_key(b);
    const Takeover* best = nullptr;
    for (const Takeover& t : all) {
        if (t.resident != who || !wants(t, b, event_flags, hour)) continue;
        if (t.quest == tracked) return &t;
        if (!best || start_order(b, t.quest) < start_order(b, best->quest)) best = &t;
    }
    return best;
}
// The quests that want this resident right now, in the order they are offered: the tracked one first,
// then by start order; at most `max`.
inline std::vector<Takeover> wanted(const std::vector<Takeover>& all, const story_mail::Book& b, Resident who,
                                    const uint8_t* event_flags, int hour, size_t max) {
    std::vector<Takeover> out;
    for (const Takeover& t : all)
        if (t.resident == who && wants(t, b, event_flags, hour)) out.push_back(t);
    const uint16_t tracked = tracked_key(b);
    std::stable_sort(out.begin(), out.end(), [&](const Takeover& x, const Takeover& y) {
        if ((x.quest == tracked) != (y.quest == tracked)) return x.quest == tracked;
        return start_order(b, x.quest) < start_order(b, y.quest);
    });
    if (out.size() > max) out.resize(max);
    return out;
}
// A quest's title as a choice in the game's choice box: one line, at most `max` bytes (cut at a word,
// "..." added). UTF-8 safe.
inline std::string choice_label(const std::string& title, size_t max = 24) {
    if (title.size() <= max) return title;
    size_t cut = max - 3;
    while (cut > 0 && (title[cut] & 0xC0) == 0x80) cut--;  // not inside a character
    const size_t space = title.rfind(' ', cut);
    if (space != std::string::npos && space > max / 2) cut = space;
    return title.substr(0, cut) + "...";
}

// How many quests want this resident right now (more than one: the others wait for a later talk)
inline size_t wanting(const std::vector<Takeover>& all, const story_mail::Book& b, Resident who, const uint8_t* event_flags = nullptr,
                      int hour = 12) {
    size_t n = 0;
    for (const Takeover& t : all) n += t.resident == who && wants(t, b, event_flags, hour);
    return n;
}

// After the talk: the letter is handed over and the quest is done (it moves to the notebook's archive).
inline bool complete(story_mail::Book& b, const Takeover& t) {
    b.erase(kRecordCarried, t.quest);
    return finish(b, t.quest) == kStepDone;
}

}  // namespace story_notebook
