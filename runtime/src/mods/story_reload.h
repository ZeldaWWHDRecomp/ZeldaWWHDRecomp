// The world editor's live story update (docs/mod-editor.md, "Quests, letters, charts and items while the
// game runs"): what a change of a mod's story tables means for a running game, worked out from the
// manifest before and after the change. Plain JSON on the mod manifest; no game dependencies (unit-tested
// in runtime/tools/story_reload_test.cpp).
//
// The rules the game follows (editor_game.cpp applies them):
//   - a quest's progress is its saved step number (story_notebook_list.h); a live update never writes it.
//     A step keeps its number when edited; inserting, moving or removing steps renumbers them in the
//     editor, and the loaded Quest Log then is at whatever step has its number now, exactly as after a
//     restart. step_moves() says which saved numbers point to another step now, for the status bar;
//   - an item's icon is built into the game's icon layout at start-up, its held model and "hold_up" into
//     its item get: removed items and the held model and "hold_up" wait for a restart (restart_items());
//     new items and a changed icon too, unless the game has live icon places (LiveIcons: the editor
//     launch mode's spare places, item_icons.h); its name, description, stack and use change at once.
#pragma once
#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "mod_json.h"

namespace story_reload {
using mods::json::Value;

// The manifest sections the game's story tables are made of.
inline constexpr const char* kSections[] = {"quests", "letters", "charts", "items"};
inline bool story_changed(const Value& before, const Value& after) {
    for (const char* k : kSections)
        if (!(before.get(k) == after.get(k))) return true;
    return false;
}

// A step's identity apart from its number: everything it declares but "n".
inline std::string step_identity(const Value& step) {
    Value v = step;
    v.object.erase("n");
    return mods::json::dump(v);
}
inline unsigned step_number(const Value& step) {
    const Value& n = step.get("n");
    return n.type == Value::Number && n.number >= 1 && n.number <= 254 ? unsigned(n.number) : 0;
}

// A saved step number of a quest that now names another step (or none).
struct StepMove {
    std::string quest;
    unsigned saved = 0;  // the number the Quest Log keeps
    unsigned now = 0;    // where that step went (0: the step was removed)
};
// For each quest in both manifests, each old step number that now names another step: `now` is the
// number of the step with the same declaration (all but "n", the only one), or 0 when the step is gone
// (its number is free, or another step moved there). A step edited in place (same number, other
// fields, and no other step took the number) is not listed.
inline std::vector<StepMove> step_moves(const Value& before, const Value& after) {
    std::vector<StepMove> out;
    for (const auto& q : before.get("quests").array) {
        const std::string id = q.get("id").string();
        const Value* nq = nullptr;
        for (const auto& c : after.get("quests").array)
            if (c.get("id").string() == id) nq = &c;
        if (!nq) continue;  // a removed quest: nothing shows it, its records stay
        std::map<std::string, std::vector<unsigned>> by_identity;  // new steps by declaration
        std::map<unsigned, std::string> new_at;                    // new step number -> declaration
        for (const auto& s : nq->get("steps").array)
            if (unsigned n = step_number(s)) by_identity[step_identity(s)].push_back(n), new_at[n] = step_identity(s);
        std::vector<std::string> old_identities;
        for (const auto& s : q.get("steps").array) old_identities.push_back(step_identity(s));
        for (const auto& s : q.get("steps").array) {
            const unsigned n = step_number(s);
            if (!n) continue;
            const std::string me = step_identity(s);
            const auto same = new_at.find(n);
            if (same != new_at.end() && same->second == me) continue;  // unchanged
            const auto it = by_identity.find(me);
            if (it != by_identity.end() && it->second.size() == 1) {
                out.push_back({id, n, it->second[0]});  // the same step under another number
                continue;
            }
            // edited in place: the number holds a step that is none of the old ones
            if (same != new_at.end() && std::find(old_identities.begin(), old_identities.end(), same->second) == old_identities.end()) continue;
            out.push_back({id, n, 0});  // removed (its number is free, or another step's now)
        }
    }
    return out;
}

// What the running game can take of items beyond their texts. In the editor launch mode the game starts with
// spare places in its item icon layout (item_icons.h): a new item takes a free one, and an item's icon is
// drawn again in its place, while the game runs. Without it (the default) both wait for a restart.
struct LiveIcons {
    std::function<bool(const std::string& id)> placed;  // the item has an icon place now (null: no live icons)
    unsigned spare = 0;                                 // free spare places for the new items without one
};
// The items whose change waits for a restart, compared with the manifest the game started with: removed
// items, a changed held model, held frames or "hold_up", and (without a live icon place, `live`) new items
// and a changed icon. Each entry is "<id> (<why>)".
inline std::vector<std::string> restart_items(const Value& started, const Value& after, const LiveIcons& live = {}) {
    std::vector<std::string> out;
    auto find = [](const Value& m, const std::string& id) -> const Value* {
        for (const auto& i : m.get("items").array)
            if (i.get("id").string() == id) return &i;
        return nullptr;
    };
    unsigned spare = live.placed ? live.spare : 0;
    auto placed = [&](const std::string& id) { return live.placed && live.placed(id); };
    for (const auto& i : after.get("items").array) {
        const std::string id = i.get("id").string();
        const Value* was = find(started, id);
        if (!was) {
            if (placed(id)) continue;  // added while the game runs, in a spare place already
            if (spare) {
                spare--;
                continue;
            }
            out.push_back(id + (live.placed ? " (a new item: the spare icon places are all in use)" : " (a new item)"));
            continue;
        }
        if (!(was->get("icon") == i.get("icon")) && !placed(id)) {
            out.push_back(id + " (its icon)");
            continue;
        }
        for (const char* k : {"held_model", "held_frames", "hold_up"})
            if (!(was->get(k) == i.get(k))) {
                out.push_back(id + " (its " + std::string(k == std::string("hold_up") ? "hold_up" : "held model") + ")");
                break;
            }
    }
    for (const auto& i : started.get("items").array)
        if (!find(after, i.get("id").string())) out.push_back(i.get("id").string() + " (removed)");
    return out;
}

// The items that change at once: in both (or new, in a spare icon place), with the same held model and
// "hold_up" (and icon, without live icon places).
inline std::vector<std::string> live_items(const Value& started, const Value& after, const LiveIcons& live = {}) {
    std::vector<std::string> out;
    const auto later = restart_items(started, after, live);
    for (const auto& i : after.get("items").array) {
        const std::string id = i.get("id").string();
        bool waits = false;
        for (const auto& l : later)
            if (l.rfind(id + " (", 0) == 0) waits = true;
        if (!waits) out.push_back(id);
    }
    return out;
}
// The id of a restart_items entry ("<id> (<why>)") and its reason
inline std::string entry_id(const std::string& entry) { return entry.substr(0, entry.find(" (")); }
inline std::string entry_why(const std::string& entry) {
    const size_t a = entry.find(" (");
    return a == std::string::npos || entry.size() < a + 3 ? std::string() : entry.substr(a + 2, entry.size() - a - 3);
}

// The letter ids a manifest declares: each quest's letter and the letters on their own.
inline std::vector<std::string> letter_ids(const Value& m) {
    std::vector<std::string> out;
    for (const auto& q : m.get("quests").array)
        if (q.get("letter").type == Value::Object && q.get("letter").get("id").type == Value::String) out.push_back(q.get("letter").get("id").text);
    for (const auto& l : m.get("letters").array)
        if (l.get("id").type == Value::String) out.push_back(l.get("id").text);
    return out;
}
// The ids in `before` that `after` no longer has.
inline std::vector<std::string> gone(const std::vector<std::string>& before, const std::vector<std::string>& after) {
    std::vector<std::string> out;
    for (const auto& id : before)
        if (std::find(after.begin(), after.end(), id) == after.end()) out.push_back(id);
    return out;
}

}  // namespace story_reload
