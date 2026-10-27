// World editor: the command layer (docs/mod-editor.md).
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
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../mods/mod_json.h"
#include "../mods/story_world_collision_mesh.h"
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
// A record and the value to write (< 0: erase it).
struct RecordValue {
    Record record;
    int value = 0;
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
    // A handle drag in progress (Session::drag_preview): the same live update as apply_actors, but
    // quietly (no status per step) and only for what can change while the game runs; `later` gets the
    // changed ids that can't (restart-only kinds), which keep their old state. The default: apply_actors.
    virtual bool preview_actors(const std::string& mod, const std::vector<story_notebook::ManifestActor>& actors,
                                const std::vector<std::string>& changed, std::vector<std::string>& later, std::string& why) {
        (void)later;
        std::string note;
        return apply_actors(mod, actors, changed, note, why);
    }
    virtual bool reload_room(std::string& why) = 0;
    // Stages and the rooms each has, from the game's own files (sorted). Empty without game files.
    virtual std::vector<std::pair<std::string, std::vector<int>>> stage_rooms() = 0;
    // The installed packages' mod stages (mod_stages.h): the package, its id for the stage, the game name and the
    // rooms. Warp destinations ("Mod stages") and the warp command's "stage:<mod>/<id>".
    struct ModStage {
        std::string mod, id, name;
        std::vector<int> rooms;
    };
    virtual std::vector<ModStage> mod_stages() { return {}; }
    // The open mod's stages built again from `manifest` and its files in `dir` (only the stage `only` when given):
    // [{id, name, ok, error, rooms}]; `why` set when nothing could run. A stage Link is in loads again.
    virtual Value rebuild_stages(const std::string& mod, const Value& manifest, const std::string& dir, const std::string& only, std::string& why) {
        (void)mod, (void)manifest, (void)dir, (void)only;
        why = "mod stages can't be built here";
        return {};
    }
    // The player's stages a mod stage can take as its template: [{stage, rooms, sky, doors, music}].
    virtual Value stage_templates(std::string& why) {
        why = "the game's stages can't be listed here";
        return {};
    }
    // A kit room's materials: {materials: [{material, textures, triangles, uv_min, uv_max}], main}.
    virtual Value kit_materials(const std::string& stage, unsigned room, std::string& why) {
        (void)stage, (void)room;
        why = "the game's rooms can't be read here";
        return {};
    }
    // The Story Notebook's page opened (or closed) over the game, as the player opens it from the bag.
    virtual bool notebook_page(bool open, bool give, std::string& why) {
        (void)open, (void)give;
        why = "the notebook can't be opened here";
        return false;
    }
    // A game room ({stage, room}) or a game piece ({piece}) exported as <name>.glb into the player's exports folder
    // (<mod manager>/Exports; never a package): the exporter's summary with "file". Request: stage, room, piece,
    // name, collision, actors, max_texture (checked by the command).
    virtual Value export_glb(const Value& request, std::string& why) {
        (void)request;
        why = "the game's rooms can't be exported here";
        return {};
    }
    // Test mode: the save group's stage record erased from the loaded Quest Log (and the working one).
    virtual bool reset_stage_save(const std::string& mod, const std::string& group, std::string& why) {
        (void)mod, (void)group;
        return why = "mod stages can't be reset here", false;
    }
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

    virtual bool test_state_format(bool save,int slot,const std::string&mod,const std::vector<Record>&own,bool full,std::string&why){
        if(full)return why="full test states are unavailable",false;
        return test_state(save,slot,mod,own,why);
    }

    // Latest asynchronous test-load result, including request ID and refusal reason.
    virtual Value load_state_result() { return Value(); }

    // ---- building new content (each optional: the defaults say the game is not there) ----
    // The ground under `above` (the game's own collision check): its height. False: no ground below.
    virtual bool ground(const WorldPoint& above, float& height, std::string& why) { return why = "the game is not running", false; }
    // The height range of a room's own collision (world space): the game looks for a body of water only within it.
    virtual bool room_height(const std::string& stage, int room, float& low, float& high, std::string& why) {
        (void)stage, (void)room, (void)low, (void)high;
        return why = "the game is not running", false;
    }
    // The ground under each of `above` at once (heights[i] valid where found[i]); false: no game.
    virtual bool grounds(const std::vector<WorldPoint>& above, std::vector<float>& heights, std::vector<bool>& found, std::string& why) {
        heights.assign(above.size(), 0);
        found.assign(above.size(), false);
        for (size_t i = 0; i < above.size(); i++) {
            float h = 0;
            std::string w;
            if (ground(above[i], h, w)) heights[i] = h, found[i] = true;
            else if (w == "the game is not running") return why = w, false;
        }
        return true;
    }
    // The first ground the ray from `from` along `direction` (unit length) meets within `range` units.
    virtual bool ray(const WorldPoint& from, const WorldPoint& direction, float range, WorldPoint& hit, std::string& why) {
        return why = "the game is not running", false;
    }
    // The fly camera's place: its position, heading and the room under it (invalid while it is off).
    virtual Place camera() { return {}; }
    // A placed actor's route in the game: {"declared", "live", "status"} for a walking resident (its copy
    // follows its route; walking, skipped a blocked point, stopped, finished); null when the game has none.
    virtual Value route_state(const std::string& mod, const std::string& id) { (void)mod, (void)id; return Value(); }
    // The mod's room paths changed (`paths` all of them, parsed): the game writes their records again (a
    // kept path keeps its place, so an actor on it sees the change at once). False: after a restart.
    virtual bool apply_paths(const std::string& mod, const std::vector<story_room_path::Path>& paths, std::string& why) {
        (void)mod, (void)paths;
        return why = "the game is not running", false;
    }
    // The mod paths in the game: [{"mod", "id", "stage", "room", "number", "points", "closed", "valid"}]; null without a game.
    virtual Value path_state() { return Value(); }
    // Points the fly camera at `target` from a short distance (turns it on first if needed).
    virtual bool focus(const WorldPoint& target, std::string& why) { return why = "the game is not running", false; }
    // The same from `distance` units back (and half as high), looking along `yaw_degrees` (NaN: its own direction).
    virtual bool focus_view(const WorldPoint& target, float distance, float yaw_degrees, std::string& why) {
        (void)distance, (void)yaw_degrees;
        return focus(target, why);
    }
    // A model of `mod` was added or changed (its manifest entry; the file is in the package `dir`):
    // convert and prepare it for the game, so actors can use it without a restart where possible. With
    // "replace": true (replace_model, issue #287) the model's file changed under the same id: the actors drawing
    // it take the new version, its collision included, as soon as it is ready.
    virtual bool prepare_model(const std::string& mod, const std::string& dir, const Value& model, std::string& note, std::string& why) {
        return why = "the game is not running", false;
    }
    // Whether a model of `mod` is ready in the game or being prepared for it. A model the game started without (no
    // placement used it then) is neither: a placement changed to it, or added with it, has it prepared first (issue #281).
    virtual bool model_ready(const std::string& mod, const std::string& model) {
        (void)mod, (void)model;
        return true;
    }
    // Why a model of `mod` could not be prepared for the game on this computer (the converter's reason); empty when it
    // is prepared, still being prepared or never was asked for. A restart prepares it the same way, so it isn't
    // "after a restart" (issue #280).
    virtual std::string model_failure(const std::string& mod, const std::string& model) {
        (void)mod, (void)model;
        return {};
    }
    // Whether the collision of a model of `mod` is being prepared (prepare_model with "collision"): a world object on it
    // is placed and the game checks its place when the collision is ready, as for an island building being made.
    virtual bool collision_preparing(const std::string& mod, const std::string& model) {
        (void)mod, (void)model;
        return false;
    }
    // Snapshot query: collision geometry and placement/budget findings for one
    // parsed object, including motion, grapple points and island presentation. The host
    // synchronizes its geometry, wave envelope and protected world snapshot.
    virtual bool world_object_query(const std::string& mod, const story_notebook::ManifestActor& actor,
                                    bool wireframe, Value& result, std::string& why) {
        return why = "the game is not running", false;
    }
    // The same for an object whose collision is not in the game yet (the water tool's body before it is made):
    // `collision` is its collision, local to the actor (without it the actor's own shape is checked).
    virtual bool world_object_query_with(const std::string& mod, const story_notebook::ManifestActor& actor,
                                         const story_world_collision::mesh::Mesh& collision, Value& result, std::string& why) {
        (void)collision;
        return world_object_query(mod, actor, false, result, why);
    }
    // Shows `text` in the game's own message box (close: hides it again).
    virtual bool preview_text(const std::string& text, bool close, std::string& why) { return why = "the game is not running", false; }
    // Play: the editor hides, Link plays from the camera's place or his own, in test mode; Stop goes back.
    virtual bool play(bool from_camera, bool restore, std::string& why) { return why = "the game is not running", false; }
    virtual bool stop(std::string& why) { return why = "the game is not running", false; }
    // Restart and return here: the game saves where Link is (a portable state), starts again with the same
    // arguments and, once the player picks the Quest Log, puts him back there (and back in Play or test
    // mode). For what the running game can't take (a removed item, a held model). The packages are saved
    // before (the command does that). False when the game can't start itself here.
    virtual bool restart_and_return(std::string& why) { return why = "the game is not running", false; }
    virtual bool playing() { return false; }

    // ---- quests and flags (each optional) ----
    // The values of story records in the loaded Quest Log (-1: no such record): read_state's quest steps and
    // flags, the quest editor's graph, status bar and Flags panel. Any thread, cheap: the game may answer
    // from its own copy of the story records (refreshed a few times a second). False: not known (no story
    // progress, or the game has not looked yet).
    virtual bool read_records(const std::vector<Record>& records, std::vector<int>& values, std::string& why) {
        return why = "the game is not running", false;
    }
    // Test mode only: writes records of `mod` (value < 0 erases one).
    virtual bool write_records(const std::string& mod, const std::vector<RecordValue>& values, std::string& why) {
        return why = "the game is not running", false;
    }
    // Test mode only: moves the game's calendar (the save file's day counter, as the clock passing midnight
    // does) on by `days`, for letters that wait some days ("after_days").
    virtual bool advance_days(int days, std::string& why) {
        (void)days;
        return why = "the game is not running", false;
    }
    // Start a quest at a step (test mode, turned on first as Play does): erases `erase` (the mod's own
    // records), writes `values`, gives or takes the mod's items to the `items` counts, warps to `to` when it
    // names a place, then plays from Link when `play`.
    virtual bool start_at_step(const std::string& mod, const std::vector<Record>& erase, const std::vector<RecordValue>& values,
                               const std::vector<std::pair<std::string, int>>& items, const Location& to, bool play, std::string& why) {
        return why = "the game is not running", false;
    }

    // Whether actors of a list_actor_types type appear while the game runs (`fallback`: the type's own
    // default, when the game doesn't know). Native props, switches, doors and enemies do in --editor.
    virtual bool live_kind(const std::string& type, bool fallback) { return fallback; }
    // where the player's game has an object archive (stage_files.h game_archive); "" none, "?" unknown (no game)
    virtual std::string game_archive(const std::string& archive) { return "?"; }
    // The demo step's pickers (command demo_choices): the stage actor names in the game's object name table;
    // the files of an object archive (the game's resource id table: "dk.bdl", "fly1.bck"); the bones of the
    // models in an archive (its BFRES in the player's game files); the object archives the player's game
    // has; more named actors (the NPC catalogue's residents, the game objects). And a sound effect played
    // with the game's own sound playback (the Play button of the sound picker).
    virtual bool stage_actor_names(std::vector<std::string>& out, std::string& why) { return out.clear(), why = "the game is not running", false; }
    virtual bool archive_files(const std::string& archive, std::vector<std::string>& out, std::string& why) { return out.clear(), why = "the game is not running", false; }
    virtual bool archive_joints(const std::string& archive, std::vector<std::string>& out, std::string& why) { return out.clear(), why = "the game is not running", false; }
    virtual std::vector<std::string> game_archives() { return {}; }
    struct NamedActor {
        std::string actor, label, archive, id, group;
    };
    virtual std::vector<NamedActor> more_actor_names() { return {}; }
    virtual bool play_sound(uint32_t id, std::string& why) { return why = "the game is not running", false; }
    // Game pieces (story_game_pieces.h): building piece `id` for `mod` from the player's room archive (the
    // converter runs on a thread of its own; placements appear once it is ready). The state is "" (not
    // prepared), "preparing", "ready" or "failed: <reason>".
    virtual bool prepare_piece(const std::string& mod, const std::string& id, std::string& note, std::string& why) {
        return why = "the game is not running", false;
    }
    virtual std::string piece_state(const std::string& mod, const std::string& id) { return ""; }

    // ---- the story tables (optional) ----
    // The mod's quests, quest talks, letters, charts or items changed (`manifest` the whole new manifest,
    // `started` the one the game started with, `dir` the package folder): the game takes the new tables
    // at its next calm moment (story_reload.h; no quest talk, letter, scene, menu or stage change running).
    // `note` says what waits for a restart. False (why): no story change shows before a restart.
    virtual bool apply_story(const std::string& mod, const std::string& name, const std::string& dir, const Value& started,
                             const Value& manifest, std::string& note, std::string& why) {
        (void)mod, (void)name, (void)dir, (void)started, (void)manifest, (void)note;
        return why = "the game is not running", false;
    }

    // ---- the game's own villagers (optional) ----
    // The game's residents the NPC catalogue knows that are loaded now, read-only: [{"id" (catalogue id),
    // "name", "description", "where", "stage", "room", "position", "talks"}].
    virtual bool villagers(Value& out, std::string& why) {
        (void)out;
        return why = "the game is not running", false;
    }

    // ---- new mods (optional) ----
    // A new, empty mod package (new_mod): the mod manager installs `manifest` as a new package in its
    // mods folder (refused when a package or folder of that id exists) and turns it on in the current
    // profile, so it also loads at the next start. `dir` gets the package folder. Nothing else changes;
    // on a refusal nothing is left behind.
    virtual bool create_package(const Value& manifest, std::string& dir, std::string& why) {
        (void)manifest, (void)dir;
        return why = "the game is not running", false;
    }

    // ---- scenes (each optional) ----
    // The fly camera looks from `eye` at `target` (scrubbing a scene's camera; turns it on if needed).
    virtual bool look(const WorldPoint& eye, const WorldPoint& target, std::string& why) {
        return why = "the game is not running", false;
    }
    // Plays `scene` of `mod` in the game from the editor's view, in test mode: the editor hides, the scene
    // plays, and when it ends (or stops) the editor's view comes back; with `restore` the world goes back to
    // how it was before. `declared`: the scene is the one the game started with (its native version may
    // play); otherwise the edited steps play (never the native version).
    virtual bool preview_scene(const std::string& mod, const story_notebook::ManifestScene& scene, bool declared, bool restore, std::string& why) {
        return why = "the game is not running", false;
    }
    // The running preview: skip (as Plus / Start does) or stop (ends it at once, back to the editor).
    virtual bool scene_control(bool stop, std::string& why) { return why = "the game is not running", false; }
    // {"playing": bool, "scene", "step", "native", "phase"} of the editor's preview.
    virtual Value scene_state() { return Value(); }
    // A handle drag's step: the actors in `poses` moved where they are in the game, in place (no new
    // actor); `moved` the ids that moved so. The default: none (the drag shows through preview_actors).
    virtual bool pose_actors(const std::string& mod, const Value& poses, std::vector<std::string>& moved, std::string& why) {
        (void)mod, (void)poses, (void)moved;
        return why = "the game is not running", false;
    }
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
    std::string source;  // "" the editor's own UI or a script; "MCP" a tool client (editor/mcp.h)
};

class Session {
public:
    explicit Session(Host* host = nullptr) : host_(host) {}
    // The active mods' packages, at start-up (each with world content or quests). The first one that
    // places actors becomes the open mod (else the first one).
    void add_package(const std::string& mod, const std::string& name, const std::string& dir, const Value& manifest);
    // {"ok": true, "result": ...} or {"ok": false, "error": "..."}; never throws. `log`: false for the UI's
    // own reads every frame (read_state, list_locations...), which stay out of the command log.
    Value run(const std::string& command, const Value& params = Value(), bool log = true, const std::string& source = {});
    // A log entry for something that is not a command (a tool client's screenshot, a refused request).
    void note(const std::string& command, const Value& params, const Value& result, const std::string& source);
    // {"command": "...", "params": {...}} as text, the result as text
    std::string run_json(const std::string& request);
    // A handle drag in progress: the game shows the actors in `poses` ([{"id", "position"?, "rotation"?}])
    // where the drag has them, through the host's live update (Host::preview_actors). Nothing changes in
    // the package: no command, no undo step, no log entry; letting go runs the one command that keeps the
    // change. A pose without position and rotation puts that actor back at the package's place (a drag
    // cancelled). The actors that exist in the game move in place (Host::pose_actors, `in_place` gets
    // them); the others are created again at the new place. `later`: the ids that can't move while the
    // game runs. False with `why`: nothing shown.
    bool drag_preview(const std::string& mod, const Value& poses, std::vector<std::string>& later, std::string& why,
                      std::vector<std::string>* in_place = nullptr);

    const std::deque<LogEntry>& log() const { return log_; }
    std::string open_mod() const { return open_; }
    const Value* manifest(const std::string& mod) const;
    const Value* started(const std::string& mod) const;  // the manifest the game started with
    std::string package_dir(const std::string& mod) const;  // the package folder ("" unknown)
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
    // Watched model files (watch_model, issue #287): each {mod, model, file} whose file changed since it was last
    // imported and has stayed the same for kWatchSettleMs, and is a whole GLB (its header's length is its size: a
    // file still being written is not); once per change. The caller runs replace_model for them. `now_ms`: a
    // steady clock in milliseconds.
    std::vector<Value> watch_due(int64_t now_ms);
    static constexpr int64_t kWatchSettleMs = 1000;

private:
    struct Package {
        std::string mod, name, dir;
        Value manifest, saved;
        Value started;  // the manifest the game started with (its scenes are the ones the game declared)
        Value layout, layout_saved;  // the editor's own sidecar (editor/quest-layout.json): graph node places
    };
    struct Step {
        std::string mod, label;
        Value before, after;
        bool layout = false;  // a layout step: before / after are layouts
    };
    Host* host_;
    std::vector<Package> packages_;
    std::string open_;
    std::deque<LogEntry> log_;
    uint64_t seq_ = 0;
    std::vector<Step> undo_, redo_;
    Place last_place_;
    mutable std::vector<std::pair<std::string, std::vector<int>>> stage_rooms_;  // the game's files, read once
    // own models' files (issue #287), by "mod/model": the file it was last imported or replaced from, the watched ones
    struct Watch {
        std::string file;
        int64_t mtime = -1, seen_mtime = -1, since = 0;  // the imported version, the last one seen, when that was first seen
        uint64_t size = 0, seen_size = 0;
    };
    std::map<std::string, std::string> model_sources_;
    std::map<std::string, Watch> watches_;
    std::set<std::string> removed_models_;  // removed in this session: a new import under the id is prepared afresh
    std::set<std::string> set_aside_;       // "mod/source": files remove_model put into models/.removed
    // remove_model's files (`removing`: the models `before` had and the manifest no longer has) into models/.removed
    // (hidden, never exported); an undo or redo moves those files back and away again
    void sync_model_files(const Package& p, const Value& before, bool removing = false);

    Package* package(const Value& params, std::string& why);
    const Package* find(const std::string& mod) const;
    Value dispatch(const std::string& command, const Value& params, std::string& why, bool& ok);
    // Applies `edit` to a copy of the mod's manifest; kept only when the copy validates.
    bool change(Package& p, const std::string& label, const std::function<bool(Value&, std::string&)>& edit, Value& result,
                std::string& why);
    bool restore(const std::string& mod, const Value& manifest, Value& result, std::string& why);
    void apply_live(const Package& p, const Value& before, Value& result);
    void apply_story(const Package& p, Value& result);  // the story tables in the game (Host::apply_story)
    void apply_paths(const Package& p, Value& result);  // the mod's room paths in the game (Host::apply_paths)
    std::vector<std::pair<std::string, Value>> all_manifests() const;
    std::vector<Location> locations() const;
    bool warp_to(const Location& to, std::string& why);
    // the building commands (commands_build.cpp): add, remove, place, models, dialogue, export, play
    Value dispatch_build(const std::string& command, const Value& params, std::string& why, bool& ok, bool& handled);
    bool where(const Value& params, Place& place, std::string& why);  // at: link, camera or position (snapped)
    Value progress(const std::string& mod, const Value& manifest);    // read_state: quest steps and flags
    // quests, flags, search, copy and paste (commands_quest.cpp)
    Value dispatch_quest(const std::string& command, const Value& params, std::string& why, bool& ok, bool& handled);
    bool change_layout(Package& p, const std::string& label, const Value& layout, Value& result);
    Value clipboard_;  // copy / paste: {"kind": "actors" | "quest_steps" | "scenes" | "scene_steps", "items": [...]}
    // the game objects palette (commands_game_objects.cpp)
    Value dispatch_game_objects(const std::string& command, const Value& params, std::string& why, bool& ok, bool& handled);
    // scenes: steps, camera keys, timeline, preview (commands_scene.cpp)
    Value dispatch_scene(const std::string& command, const Value& params, std::string& why, bool& ok, bool& handled);
    // letters on their own, charts and items (commands_story.cpp)
    Value dispatch_story(const std::string& command, const Value& params, std::string& why, bool& ok, bool& handled);
    // an actor's route: points, settings, ground check, live state (commands_route.cpp)
    Value dispatch_route(const std::string& command, const Value& params, std::string& why, bool& ok, bool& handled);
    // the mod's room paths: lines and areas its actors name (commands_paths.cpp)
    Value dispatch_paths(const std::string& command, const Value& params, std::string& why, bool& ok, bool& handled);
    // the water tool: ponds, rivers, waterfalls (commands_water.cpp)
    Value dispatch_water(const std::string& command, const Value& params, std::string& why, bool& ok, bool& handled);
    // the mod's stages: list, add, set, check, rebuild (commands_stages.cpp)
    Value dispatch_stages(const std::string& command, const Value& params, std::string& why, bool& ok, bool& handled);
};

// One problem of a package, with what it is about (for the editor's problem list: a click selects it).
struct Problem {
    std::string message;
    std::string kind;   // "actor", "model", "scene", "quest", "item", "chart", "letter", "file" or "package"
    std::string id;     // the actor's (model's...) id; empty for the whole package
    std::string field;  // the field when known ("position", "model", ...)
    std::string severity;  // "" an error (the mod manager refuses the package), "warning" or "info" (the editor's own checks)
};

// ---- pure helpers (unit-tested) ----
// A placed resident's own walking pace (game units per second; 0: it does not walk) and why an actor
// can't have a route (empty: it can) (commands_route.cpp).
double resident_pace(const Value& actor);
std::string route_refusal(const Value& actor);
// What a placed actor uses one of the mod's room paths for (the Inspector's Path section; empty: it uses
// none) (commands_paths.cpp).
std::string room_path_use(const Value& actor);
// Every problem of the package: each actor, model, scene and quest checked on its own (as the mod
// manager checks the whole), the model files, then the whole package. Empty: it is valid.
std::vector<Problem> package_problems(const std::string& mod, const Value& manifest, const std::vector<std::pair<std::string, Value>>& all,
                                      const std::string& dir);
// One placement finding in words: its message, the game's own name and where it is ("a cutscene area of the island
// [TagEv] at x -671, y 115, z -198464"), so the maker can tell which of the island's places is meant.
std::string conflict_words(const Value& conflict);
// World objects and island buildings placed where they can't stand ("house-a can't stand here: in the way: ..."),
// checked against the running game's placement index (commands.cpp); empty without a game.
std::vector<Problem> placement_problems(Host* host, const std::string& mod, const Value& manifest);
// The types `add_actor` places, with what each needs.
struct ActorType {
    std::string type, label, group, help;
    bool needs_model = false;  // a mod model (static or skinned)
    bool skinned = false;      // a skinned model only
    bool live = true;          // appears without a restart (where the game has the kind already)
};
const std::vector<ActorType>& actor_types();
// The declared choices of a type (list_actor_types "variants"): the add_actor option that takes them
// ("resident") and each choice's value, the editor's name for it and plain help. Empty: no choices.
struct TypeVariant {
    std::string option, value, label, help;
};
std::vector<TypeVariant> type_variants(const std::string& type);
// A new placed actor of `type` (actor_types) with id `id` at `place`, valid on its own: the flags it
// needs get ids of its own. `model` for model types; `options` may name "resident", "switch", "lines".
bool new_actor(const std::string& type, const std::string& id, const Place& place, const std::string& model, const Value& manifest,
               const Value& options, Value& out, std::string& why);
// Game objects (commands_game_objects.cpp): one catalogue entry as list_game_objects shows it; a new
// placed game object of `type` ("game:palm", options.variant) at `place`; the manifest's
// "requires": {"game_objects": N} raised to what `type` needs.
Value game_object_info(const story_game_objects::Entry& e);
bool new_game_object(const std::string& type, const std::string& id, const Place& place, const Value& options, Value& out, std::string& why);
void require_game_objects(Value& manifest, const std::string& type);
// Game pieces (commands_game_pieces.cpp): one catalogue piece as list_game_objects shows it (type
// "piece:<id>", categories Buildings, Island parts and Islands); a new placed piece at `place` (yaw only, scale 1;
// an island ground at the sea's level); the manifest's
// "requires" raised to what it needs.
Value game_piece_info(const story_game_pieces::Piece& p);
bool new_game_piece(const std::string& type, const std::string& id, const Place& place, Value& out, std::string& why);
// An island ground on the sea: it always stands at the sea's level (y 0), wherever it is moved to.
bool sea_level_piece(const Value& actor);
void require_game_piece(Value& manifest, const std::string& type);
// A free actor id from `base` ("sign", "sign-2", ...).
std::string free_id(const Value& list, const std::string& base);
// A copy of the actor `a` under id `id`, with its own flags (each flag id gets the new id).
Value duplicate_actor(const Value& a, const std::string& id);
// A .glb file checked as the game would take it: {"kind", "size", "vertices", "triangles", "texture",
// "bones", "clips", "minimum", "maximum" (its bounds, game units)} or false with a message that says which limit (size, triangles, texture, bones,
// clips, material) it breaks.
// Whether a model's GLB has collision of its own: a mesh, or the node drawing it, named "<name>_col".
bool model_has_collision_mesh(const std::string& dir, const Value& manifest, const std::string& model);
bool check_model_file(const std::string& path, Value& info, std::string& why);
bool check_model_bytes(const std::vector<uint8_t>& bytes, Value& info, std::string& why);
// A .glb to import: made to fit the game's rules where it can be (model_import.h), then checked as check_model_file
// does. `bytes` is the file the package keeps; info["import_fixes"] lists what was changed (absent: nothing).
inline constexpr size_t kMaxImportBytes = 64 * 1024 * 1024;
bool import_model_file(const std::string& path, std::vector<uint8_t>& bytes, Value& info, std::string& why);
bool check_kit_model_file(const std::string& path, const Value& declaration, Value& info, std::string& why);
// A kit model's file as an import keeps it (issue #281): read from `path`, with the textures of its game materials removed
// (Blender keeps them for its preview; the game draws its own) and checked as check_kit_model_file checks it. Game
// materials: the declaration's "materials" and "material", an export_glb material's "wwhd" extras, and with `listed` (a kit
// room's list_kit_materials result; null when it can't be read) the room's material names (a Blender ".001" copy counts)
// and an image named after one of their textures. info["import_fixes"] says how many were removed and
// info["preview_textures_removed"] names their materials (absent: none).
bool prepare_kit_model_file(const std::string& path, const Value& declaration, const Value& listed, std::vector<uint8_t>& bytes, Value& info, std::string& why);
// A kit model's visible materials against a kit room's (list_kit_materials' result) and the declaration's "materials" and
// "material", as kit_model.py names them: {materials: [{name, kind: game|own|unknown, game?, default?}], game: [names],
// own: [names], unknown: [names]}. "unknown": no texture and no material of the room (its build would refuse it).
Value kit_material_report(const std::vector<uint8_t>& glb, const Value& listed, const Value& declaration);
// The report in a sentence for the import's note ("Game materials of sea room 11: ... Own textures: ...").
std::string kit_report_note(const Value& report, const Value& kit);
// A model's place relative to its own origin (issue #280). Bounds are in game units, the GLB's metres x 100, as
// check_model_file's "minimum" and "maximum" give them.
// How far the geometry lies from the origin across the ground (x, z): 0 when the origin is under or inside it.
float origin_gap(const WorldPoint& minimum, const WorldPoint& maximum);
// Modelled away from its origin, as a model made on an island export is (the export keeps the room's coordinates):
// the gap is over 10 m and over a quarter of the model's width.
bool off_origin(const WorldPoint& minimum, const WorldPoint& maximum);
// The world box of the model drawn by an actor with this "position", "rotation" (degrees) and "scale"; the game
// turns a model as mDoMtx_ZXYrotS does (Y, then X, then Z).
void placed_bounds(const WorldPoint& minimum, const WorldPoint& maximum, const Value& actor, WorldPoint& low, WorldPoint& high);
// The place an off-origin model goes when it is added at Link or the camera: the origin of the room's coordinates,
// as the island export has it (a sea square's centre at sea level; a stage's own origin).
WorldPoint room_origin(const std::string& stage, int room);
// Writes the package (manifest plus the package's own files, never game files) into `dest`, a new or
// empty folder. `written`: the files.
bool export_package(const std::string& dir, const Value& manifest, const std::string& dest, std::vector<std::string>& written, std::string& why);
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
                                      const std::vector<std::pair<std::string, Value>>& packages,
                                      const std::vector<Host::ModStage>& mod_stages = {});
// A warp destination's stage as the game names it. A package's stage ("stage:<mod>/<id>", or "stage:<id>"
// when one installed package has that stage) becomes its game name, and a mod stage's room must be one of its
// rooms. False with `why` (said to the person) when no installed stage matches: the game never gets a stage
// name it has no archive for.
bool resolve_warp_stage(Location& to, const std::vector<Host::ModStage>& mod_stages, std::string& why);
// The story records that hold a mod's progress (its actor and scene flags, quests, letters, and the charts and
// items Link got from it).
std::vector<Record> mod_records(const std::string& mod, const Value& manifest);
// Erases `own` from a story area, except records that another mod's names also map to (`shared`).
unsigned reset_records(story_mail::Book& book, const std::vector<Record>& own, const std::vector<Record>& shared, unsigned& skipped);
// Where the package names one of its charts, items or letters (`kind`: "chart", "item" or "letter"),
// each said for people ("actor keeper > then > 1", "scene treasure > trigger (salvage)"). The entry
// itself is left out. Empty: nothing names it. (A letter on its own is named by nothing in the
// manifest; only the mod's code may ask for it.)
std::vector<std::string> story_references(const Value& manifest, const std::string& kind, const std::string& id);
// A PNG an item can use as its icon: the file's signature and size (1..2048 pixels a side, up to 16 MiB),
// as the game reads it. `width` / `height` get its size.
bool check_icon_file(const std::string& path, unsigned& width, unsigned& height, std::string& why);
// The villager of `villagers` (list_villagers) that a ray from `origin` along `direction` (unit length)
// meets first within `range`: the one whose body (a 70-unit cylinder, 160 high, at its position) it
// passes through nearest. -1: none.
int pick_villager(const Value& villagers, const WorldPoint& origin, const WorldPoint& direction, float range);
// A mod id as the mod manager takes it: 1..64 of a-z, 0-9, '_', '-' and '.', not starting with '.'.
bool package_id_valid(const std::string& id);
// The manifest of a new, empty mod (new_mod's parameters: id, name, author?, description?): a content
// package with an empty actor list and the Story Notebook dependency the story content needs, in the
// current manifest format. False with `why` for an invalid id, a missing name or text that is too long.
bool new_package_manifest(const Value& params, Value& out, std::string& why);
// Writes `manifest` as a new package folder `parent`/<id> with its manifest.json. Refused when that
// folder exists (it is created first, so two callers can't both get it); on a failure nothing is left.
// `dir` gets the folder.
bool write_package_folder(const std::string& parent, const Value& manifest, std::string& dir, std::string& why);
// The manifest as package text: two-space indent, numbers in their shortest exact form.
std::string pretty(const Value& v);
// A point object {"x","y","z"}.
Value point_value(const WorldPoint& p);

}  // namespace editor
