// World editor: the scene editor's pure helpers (docs/mod-editor.md, "Scenes"). No game dependencies.
//
// A scene of the manifest ("scenes": SDK v3 steps, optional "native" turn_camera version) as the editor
// sees it: steps addressed by their place in the list (1 = first) or their id, a timeline on the
// default path (each choice's default option), the camera at a time on that path, new steps with
// sensible defaults, retiming, a range of steps as a scene of its own for previews, and the native
// version made from the steps. Nothing here changes the format: only the fields that exist are written,
// and every change is still checked by the validator before the session keeps it.
#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "commands.h"

namespace editor {

// Durations are the scene's 60 Hz timeline units ("frames"); "seconds" counts as seconds * 60.
inline constexpr unsigned kSceneFramesMax = 18000;

// A step named by its place (1-based integer) or its id: its index in the list (0-based).
bool scene_step_index(const Value& scene, const Value& ref, size_t& index, std::string& why);
// The step's duration in frames (camera_path: the sum of its keys'; untimed steps: 0).
unsigned scene_step_frames(const Value& step);
bool scene_step_timed(const std::string& type);
// The steps that may follow step `index` (next, the choice's options, or the following step; none
// after an end). -1 entries: a name that names no later step.
std::vector<long> scene_successors(const Value& scene, size_t index);

// The scene on a time axis:
// {"duration": the default path's length, "longest": the longest path, "path": [step numbers on the
//  default path], "steps": [{"step", "id"?, "type", "start", "frames", "timed", "default_path",
//  "branch"?: "the choice step's number:option", "keys"?: [time of each camera key]}]}
// start = -1 for a step nothing leads to.
Value scene_timeline(const Value& scene);

// The camera at `time` frames on the default path: the last camera, camera_path or pan before it.
// `step` gets the camera step's index. False (with why) before the first fixed camera, or for a camera
// that follows an actor (only the game knows where the actor is).
bool scene_camera_at(const Value& scene, double time, WorldPoint& eye, WorldPoint& target, size_t& step, std::string& why);

// A new step of `type` (camera, say, move, face, wait, sound, fade, end, camera_path, camera_follow,
// animation, walk, choice, music, particle) with defaults that pass the checks where the package
// allows: `view` the fly camera's view (or Link's) for cameras and positions; `manifest` for the actor a
// step names (the first that fits). A choice comes as three steps: the choice and its two branches.
struct SceneView {
    bool valid = false;
    WorldPoint eye, target;
};
bool new_scene_steps(const std::string& type, const Value& manifest, const Value& scene, const SceneView& view, std::vector<Value>& out,
                     std::string& why);
// Inserts `steps` before index `at` (0-based; the list's size: at the end). A choice's options name
// the step after its branches: that step gets an id when it has none.
bool insert_scene_steps(Value& scene, size_t at, std::vector<Value> steps, std::string& why);
// Removes a step; refused while another step names it (next, an option).
bool remove_scene_step(Value& scene, size_t index, std::string& why);
// Moves a step to index `to` (0-based).
bool move_scene_step(Value& scene, size_t index, size_t to, std::string& why);
// A free step id from `base` ("choice", "choice-2", ...).
std::string free_step_id(const Value& scene, const std::string& base);

// Retiming (the timeline's drag): a timed step's duration (frames; "seconds" goes), a camera_path's
// key (`key` 1-based: the time from that key to the next) or, with key 0, the whole path scaled.
bool retime_scene_step(Value& step, unsigned key, unsigned frames, std::string& why);

// Camera keys of a camera or camera_path step. add: the view as a new key at `at` (1-based place; 0
// the end), `frames` the time from the key before it to the new one (a camera step becomes a
// camera_path from its view to the new one). set: key `key` takes the view (and frames when > 0).
// remove: the key goes (a two-key path becomes a camera step).
bool add_camera_key(Value& step, const WorldPoint& eye, const WorldPoint& target, unsigned at, unsigned frames, std::string& why);
bool set_camera_key(Value& step, unsigned key, const WorldPoint* eye, const WorldPoint* target, unsigned frames, std::string& why);
bool remove_camera_key(Value& step, unsigned key, std::string& why);

// Steps `from`..`to` (0-based, inclusive) as a scene of its own (id `id`) for a preview: links that
// leave the range go to an end of the preview's own, steps the range's first step does not lead to
// are left out, and an end comes last.
bool scene_range(const Value& scene, size_t from, size_t to, const std::string& id, Value& out, std::string& why);

// The native turn_camera version made from the scene's steps (two placed uo2 residents' face steps,
// fixed cameras and waits): {"type": "turn_camera", "actors", "wait_ticks", "camera", "timekeeper_ticks",
// "watchdog_ticks"}. False with why when the steps don't fit the native version's shape.
bool native_from_steps(const Value& manifest, const Value& scene, Value& native, std::string& why);
// Whether the native version would play (the game's own check of the steps against the native block),
// and why not.
bool native_eligible(const Value& manifest, const Value& scene, std::string& why);

// The scene's trigger from simple parameters: type none (removes it), talk (actor), quest_step
// (quest, step), room or area (stage, room, position, radius) or salvage (chart).
bool scene_trigger(const std::string& type, const Value& params, const Place& place, Value& trigger, std::string& why);

}  // namespace editor
