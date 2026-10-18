// World editor: the quest editor's pure helpers (docs/mod-editor.md, "Quests"). No game dependencies.
//
// A quest of the manifest ("quests": SDK v2 steps, SDK v3 actor conditions and actions) seen as a graph:
// a Start node, one node per step and an End node. The edges are what moves the quest from one step to
// the next in the current format:
//   - the quest's letter, read: Start -> the first step;
//   - a placed actor whose "when" names the quest at step A (also in "quests") and whose "then" sets
//     step B: A -> B, or finishes the quest ("quest_done"): A -> End (an actor without such a condition:
//     from Start, "any time"); each of an actor's "talks" the same way, with its own conditions;
//   - one of the mod's items whose use sets a step: Start -> that step;
//   - a scene whose end actions set a step (or finish the quest): from its trigger's step (or Start);
//   - a step's resident talk ("npc"): that step -> End (the talk finishes the quest).
// Nothing here changes the format: the editor only reads and writes the fields that exist.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "commands.h"

namespace editor {

// The quest as nodes and edges (JSON, for the editor's graph view and tools):
// {"quest", "title", "nodes": [{"id": "start" | "step:N" | "end", "kind", "n", "text", "where", "place",
//   "talk", "badges": [{"kind", "text"}], "problems": [...], "final", "reachable"}],
//  "edges": [{"from", "to", "via": {"kind", "id"}, "label", "conditions": [...], "rewards": [...],
//   "editable", "branch"}]}
// false when the manifest has no such quest.
bool quest_graph(const std::string& mod, const Value& manifest, const std::string& quest, Value& out);

// What "start at this step" sets up: the shortest declared way from Start to the step, and what the
// earlier steps hand out on it (the mod's own flags and items only, never a game bit).
struct StepStart {
    unsigned step = 0;
    std::vector<std::string> path;                      // node ids, "start" first
    std::vector<std::pair<std::string, bool>> flags;    // mod flags (story records of the mod)
    std::vector<std::pair<std::string, int>> items;     // the mod's items and how many Link has then
    std::string letter;                                 // the quest's letter, read on the way
    bool carried = false;                               // Link carries the letter it encloses
    std::string location;                               // a list_locations id to warp to ("" none)
    bool reachable = true;                              // false: no declared way leads there
};
// `warp`: "next" (in front of the actor that moves the quest on, else the step's place), "place" (the
// step's place on the sea chart) or "none".
bool step_start(const std::string& mod, const Value& manifest, const std::string& quest, unsigned step, const std::string& warp, StepStart& out,
                std::string& why);
// The story records start-at-step writes: the quest's step (and start order), the letter, the carried
// letter, the tracked quest, and the flags.
std::vector<RecordValue> step_records(const std::string& mod, const std::string& quest, const StepStart& s);

// The quest editor's checks beyond the validator's (each a warning unless said): steps nothing leads
// to, steps nothing moves on from, steps without a place, steps set twice, backwards steps, items a
// condition needs that nothing gives, flag actions on a prop's or enemy's flag. field = "step:N".
std::vector<Problem> quest_problems(const std::string& mod, const Value& manifest);

// The mod's own flags: the actors' flags, switch flags, flag actions, props' broken and enemies'
// defeated flags; each with its story record and who uses it. settable: a plain flag record (the
// editor may set it in test mode); broken and defeated flags only show.
struct ModFlag {
    std::string id, kind;  // kind: actor, switch, action, prop, enemy
    Record record;
    std::vector<std::string> used_by;
    bool settable = true;
};
std::vector<ModFlag> mod_flags(const std::string& mod, const Value& manifest);

// The quest's step record key and the key of an item (as story_notebook and story_items use them).
uint16_t quest_key(const std::string& mod, const std::string& quest);
uint16_t mod_item_key(const std::string& mod, const std::string& item);
// "Lighthouse quest: step 3 of 6" for a saved step value (0 not started, 255 done).
std::string quest_progress_text(const Value& quest, int saved);

// Everything of the package whose ids or texts contain `text` (case-insensitive): actors, quests and
// their steps, scenes, items and flags. [{"kind", "id", "quest"?, "step"?, "field", "text"}]
Value search_package(const std::string& mod, const Value& manifest, const std::string& text);

// Gives a quest's steps new numbers (old -> new) and changes every reference to them: the actors'
// conditions and actions and the items' use actions. Refused when a scene names one of the changed
// steps (the scene is changed first) or the numbers would collide.
bool renumber_steps(Value& manifest, const std::string& quest, const std::vector<std::pair<unsigned, unsigned>>& map, std::string& why);
// Removes a step and every reference to it (actor conditions on it, actions that set it); refused while a
// scene names it.
bool remove_step(Value& manifest, const std::string& quest, unsigned step, std::vector<std::string>& changed, std::string& why);

// Transitions: actor `via` moves quest `quest` from `from` (0: any time) to step `to`. Sets the actor's
// condition to the quest at `from` (keeping its item and time conditions, or setting them from
// `item` / `time` when given: "" keeps, "-" removes) and adds the quest_step action.
bool connect_steps(Value& manifest, const std::string& quest, unsigned from, unsigned to, const std::string& via, const std::string& item,
                   const std::string& time, std::string& why);
// Removes that actor's action that sets `to` (its condition stays; from 0: also any time).
bool disconnect_steps(Value& manifest, const std::string& quest, unsigned from, unsigned to, const std::string& via, std::string& why);

}  // namespace editor
