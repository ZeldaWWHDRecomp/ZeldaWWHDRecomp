// World editor: a scene's "demo" step (docs/mod-sdk-v3.md, "Demo steps") as lanes and keys. Pure helpers
// on the step's JSON, no game dependencies: the timeline's lanes, the poses at a frame (the editor's
// preview without starting the demo), and key and event edits. Every change is checked by the
// manifest validator before the session keeps it (demo_check gives the same reason for the panels).
//
// Lanes (keyed tracks): "camera.eye", "camera.target", "camera.fovy", "camera.roll" and, for each actor,
// "<id>.position", "<id>.rotation", "<id>.scale".
// Event lists (frame-ordered): per actor "clips", "modes", "data"; for the step "messages", "sounds",
// "particles", "attach", "detach", "time", "weather".
#pragma once
#include <string>
#include <vector>

#include "commands.h"

namespace editor {

inline constexpr double kDemoFps = 30;  // demo frames are the game's 30 Hz cutscene frames

// {"frames", "origin": "link"|{x,y,z}|null, "lanes": [{"lane", "actor"?, "kind": "point"|"number",
//  "curve", "keys": [frame...]}], "events": [{"list", "actor"?, "items": [{"index", "frame", "label"}]}],
//  "actors": [{"id", "kind": "game"|"link"|"model"|"character", "name"}], "problem"?: why the step is refused}
//  "warnings"?: what the demo can't show (demo_warnings)
Value demo_lanes(const Value& manifest, const Value& step);
// What the step can't show although the game plays it: an actor whose position keys never come into
// the demo camera's view, Link without a position key (he stays where he stands). One line each.
std::vector<std::string> demo_warnings(const Value& step);

// The demo at `frame`: {"camera"?: {"eye", "target", "fovy", "roll"}, "actors": [{"id", "position"?,
// "rotation"?, "scale"?}]} in world coordinates (`origin` and `angle` place an "origin": "link" demo:
// Link's position and facing; a demo with a fixed origin uses its own). Curves as the game plays them:
// linear, smooth (passes through the keys), bspline (rounder, near the keys), step.
bool demo_pose(const Value& step, double frame, const WorldPoint& link, double link_angle, Value& out, std::string& why);

// Keys. set: the key at `frame` takes `value` ({x,y,z} or a number; added when none is there).
// move: the key at `frame` goes to `to`. remove: the key goes (the lane goes with its last key).
// curve: the lane's curve (linear, smooth, bspline, step).
bool demo_set_key(Value& step, const std::string& lane, unsigned frame, const Value& value, std::string& why);
bool demo_move_key(Value& step, const std::string& lane, unsigned frame, unsigned to, std::string& why);
bool demo_remove_key(Value& step, const std::string& lane, unsigned frame, std::string& why);
bool demo_set_curve(Value& step, const std::string& lane, const std::string& curve, std::string& why);

// Events of a list ("clips", "modes", "data" need `actor`): added in frame order; edit sets fields
// (null removes one) and keeps the order; remove by index (0-based, as demo_lanes lists them).
bool demo_add_event(Value& step, const std::string& list, const std::string& actor, const Value& event, size_t& index, std::string& why);
bool demo_edit_event(Value& step, const std::string& list, const std::string& actor, size_t index, const Value& fields, std::string& why);
bool demo_remove_event(Value& step, const std::string& list, const std::string& actor, size_t index, std::string& why);

// Actors: add one ({"id", and "actor" | "model" | "character", ...}); remove one (refused while a
// sound, particle or attachment names it).
bool demo_add_actor(Value& step, const Value& actor, std::string& why);
// Sets fields of a demo actor (spawn, params, model, ...; null removes one; not its id).
bool demo_edit_actor(Value& step, const std::string& id, const Value& fields, std::string& why);
bool demo_remove_actor(Value& step, const std::string& id, std::string& why);

// A world point as the step's own coordinates (the inverse of the pose's placing): for an "origin":
// "link" demo relative to `link`, for a fixed origin relative to it, otherwise unchanged.
WorldPoint demo_local(const Value& step, const WorldPoint& world, const WorldPoint& link);
// The other way: a point of the step's own coordinates in the world.
WorldPoint demo_world(const Value& step, const WorldPoint& local, const WorldPoint& link);

// The game's own check of the step (and that its characters are the mod's placed actors).
bool demo_check(const Value& manifest, const Value& step, std::string& why);

// A new demo step: `frames` long, the camera from `view` (world coordinates, no origin).
Value new_demo_step(const WorldPoint& eye, const WorldPoint& target, unsigned frames);

}  // namespace editor
