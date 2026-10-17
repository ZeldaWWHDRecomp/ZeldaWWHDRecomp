// World editor, phase E0: the command layer (docs/mod-editor.md).
//
// Every editor action is one command: a name, JSON parameters, a JSON result. The Editor area of the
// settings overlay calls only Session::run, and so will a later tool interface: there is no other way
// to change anything. Commands that change a mod's package edit the open mod's manifest (a working copy
// in memory) and are checked by the same validator the mod manager uses before the change is kept; a
// refused change leaves everything as it was. Each kept change is one undo step. Saving writes the
// manifest back into the mod's own package folder (and nothing else), after a confirmation.
//
// Commands that act on the running game (warp, fly camera, test mode, test states) go through Host,
// which the game implements (editor_game.cpp) and the unit tests fake (runtime/tools/editor_test.cpp).
// They are requests: the game carries them out at its next safe point and read_state shows the outcome.
//
// This file has no game dependencies: plain C++ on the mod JSON helpers and the manifest parsers.
#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "../mods/mod_json.h"
#include "../mods/story_world_manifest.h"

namespace story_mail { class Book; }

namespace editor {
using mods::json::Value;
using story_notebook::WorldPoint;

// Where Link (or the camera) is: stage name, room, position, heading (the game's 16-bit angle).
struct Place {
    bool valid = false;
    std::string stage;
    int room = -1;
    WorldPoint position;
    int angle = 0;
};

// A warp destination.
inline constexpr int kFirstSpawn = -2;  // the room's first spawn point, from the game's own room data
inline constexpr int kAtPosition = -1;  // stand at `position` (the game's restart point)
struct Location {
    std::string id;     // stable: "island:11", "room:M_NewD2:3", "actor:<mod>:<id>", "step:<mod>:<quest>:<n>"
    std::string label;  // for people
    std::string group;  // "Islands", "Rooms" or "Quest"
    std::string stage;
    int room = 0;
    int point = kFirstSpawn;  // >= 0: that spawn point of the stage
    WorldPoint position;
    int angle = 0;
};

// A record of the story area (story_mail_book.h): what a mod's progress is made of.
struct Record {
    uint8_t kind = 0;
    uint16_t key = 0;
    bool operator==(const Record& o) const { return kind == o.kind && key == o.key; }
};

// What the command layer needs from the running game. Every call comes from the thread that runs the
// session; the game implementation queues the work for its own thread and returns at once.
class Host {
public:
    virtual ~Host() = default;
    // The mod's placed actors changed: `actors` all of them as parsed from the new manifest, `changed` the
    // ids whose declaration differs. The game removes those and creates them again in their room.
    // `note` may say what needs a room reload or a restart.
    virtual bool apply_actors(const std::string& mod, const std::vector<story_notebook::ManifestActor>& actors,
                              const std::vector<std::string>& changed, std::string& note, std::string& why) = 0;
    virtual bool reload_room(std::string& why) = 0;
    // Stages and the rooms each has, from the game's own files (sorted). Empty without game files.
    virtual std::vector<std::pair<std::string, std::vector<int>>> stage_rooms() = 0;
    virtual bool warp(const Location& to, std::string& why) = 0;
    virtual Place link() = 0;
    // A cutscene, event, dialogue, menu or stage change is running: no warp, no fly camera.
    virtual bool busy(std::string& why) = 0;
    virtual bool fly(bool on, bool place_link, std::string& why) = 0;
    virtual Value fly_state() = 0;  // {"on", "position", "target", "speed", "room"}
    virtual bool set_test_mode(bool on, std::string& why) = 0;
    virtual bool test_mode() = 0;
    // Test mode only: erase `own` from the loaded Quest Log's story area, never a record in `shared`.
    virtual bool reset_records(const std::string& mod, const std::vector<Record>& own, const std::vector<Record>& shared,
                               unsigned& erased, std::string& why) = 0;
    // Test mode only: the editor's own state slots (1..kTestSlots); a test state keeps the mod's records.
    virtual bool test_state(bool save, int slot, const std::string& mod, const std::vector<Record>& own, std::string& why) = 0;
};
inline constexpr int kTestSlots = 3;

// The command list (for the UI's help and a tool interface).
struct Param {
    std::string name, type, help;  // type: string, integer, number, boolean, point, object, array, any
    bool required = false;
};
struct CommandInfo {
    std::string name, help;
    std::vector<Param> params;
    bool changes_package = false;  // an undo step
    bool destructive = false;      // needs "confirm": true
};
const std::vector<CommandInfo>& command_list();
const CommandInfo* find_command(const std::string& name);

struct LogEntry {
    uint64_t seq = 0;
    std::string command;
    Value params, result;
};

class Session {
public:
    explicit Session(Host* host = nullptr) : host_(host) {}
    // The active mods' packages, at start-up (each with world content or quests). The first one that
    // places actors becomes the open mod (else the first one).
    void add_package(const std::string& mod, const std::string& name, const std::string& dir, const Value& manifest);
    // {"ok": true, "result": ...} or {"ok": false, "error": "..."}; never throws. `log`: false for the UI's
    // own reads every frame (read_state, list_locations...), which stay out of the command log.
    Value run(const std::string& command, const Value& params = Value(), bool log = true);
    // {"command": "...", "params": {...}} as text, the result as text
    std::string run_json(const std::string& request);

    const std::deque<LogEntry>& log() const { return log_; }
    std::string open_mod() const { return open_; }
    const Value* manifest(const std::string& mod) const;
    bool dirty(const std::string& mod) const;
    std::vector<std::string> mods() const;
    size_t undo_depth() const { return undo_.size(); }
    size_t redo_depth() const { return redo_.size(); }
    // Text of the last undo / redo step, for buttons ("" when none).
    std::string undo_label() const { return undo_.empty() ? std::string() : undo_.back().label; }
    std::string redo_label() const { return redo_.empty() ? std::string() : redo_.back().label; }
    // The last warp's starting point ("back to last position").
    const Place& last_place() const { return last_place_; }
    static constexpr size_t kLogSize = 64, kUndoSize = 128;

private:
    struct Package {
        std::string mod, name, dir;
        Value manifest, saved;
    };
    struct Step {
        std::string mod, label;
        Value before, after;
    };
    Host* host_;
    std::vector<Package> packages_;
    std::string open_;
    std::deque<LogEntry> log_;
    uint64_t seq_ = 0;
    std::vector<Step> undo_, redo_;
    Place last_place_;
    mutable std::vector<std::pair<std::string, std::vector<int>>> stage_rooms_;  // the game's files, read once

    Package* package(const Value& params, std::string& why);
    const Package* find(const std::string& mod) const;
    Value dispatch(const std::string& command, const Value& params, std::string& why, bool& ok);
    // Applies `edit` to a copy of the mod's manifest; kept only when the copy validates.
    bool change(Package& p, const std::string& label, const std::function<bool(Value&, std::string&)>& edit, Value& result,
                std::string& why);
    bool restore(const std::string& mod, const Value& manifest, Value& result, std::string& why);
    void apply_live(const Package& p, const Value& before, Value& result);
    std::vector<std::pair<std::string, Value>> all_manifests() const;
    std::vector<Location> locations() const;
    bool warp_to(const Location& to, std::string& why);
};

// ---- pure helpers (unit-tested) ----
// The manifest of `mod` as the mod manager checks it (models, actors, scenes, quests), together with the
// other active packages (actor and room limits, flag collisions).
bool validate_package(const std::string& mod, const Value& manifest, const std::vector<std::pair<std::string, Value>>& all,
                      std::string& why);
// The Great Sea is 7 x 7 squares of 100000 units, room 1 in the north-west; 0 off the sea.
int sea_room(float x, float z);
WorldPoint sea_room_centre(int room);
const char* island_name(int room);  // "Windfall Island"; nullptr outside 1..49
// Warp destinations: the 49 sea squares, the stages and rooms in `stage_rooms` (the game's files), and
// for each package its placed actors and quest step places.
std::vector<Location> build_locations(const std::vector<std::pair<std::string, std::vector<int>>>& stage_rooms,
                                      const std::vector<std::pair<std::string, Value>>& packages);
// The story records that hold a mod's progress (its actor and scene flags, quests and letters).
std::vector<Record> mod_records(const std::string& mod, const Value& manifest);
// Erases `own` from a story area, except records that another mod's names also map to (`shared`).
unsigned reset_records(story_mail::Book& book, const std::vector<Record>& own, const std::vector<Record>& shared, unsigned& skipped);
// The manifest as package text: two-space indent, numbers in their shortest exact form.
std::string pretty(const Value& v);
// A point object {"x","y","z"}.
Value point_value(const WorldPoint& p);

}  // namespace editor
