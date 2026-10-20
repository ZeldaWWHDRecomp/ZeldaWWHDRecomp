// World editor command layer (commands.h). No game dependencies.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include "commands.h"
#include "../mods/mod_stages_manifest.h"


#include "../mods/story_items_core.h"
#include "../mods/story_mail_book.h"
#include "../mods/story_manifest.h"
#include "../mods/story_reload.h"

namespace editor {
namespace {
namespace fs = std::filesystem;
using story_notebook::ManifestActor;

// ---------------------------------------------------------------- the command list
const std::vector<CommandInfo> kCommands = {
    {"help", "The command list with each command's parameters, or one command.", {{"command", "string", "a command name"}}},
    {"list_mods", "The active mods the editor can open (each with placed actors, models, scenes or quests).", {}},
    {"open_mod", "Opens a mod: later commands without \"mod\" act on it.", {{"mod", "string", "the mod's id", true}}},
    {"new_mod", "Creates a new, empty mod (a package of its own in the mod manager's mods folder), turns it on in the current mod profile and opens it. Refused when a mod with that id exists; other mods are not touched. Not an undo step: remove a mod you don't want in Settings > Mods (turn it off, then Remove).",
     {{"id", "string", "the new mod's id: up to 64 of a-z, 0-9, '-', '_' and '.' (not starting with '.')", true}, {"name", "string", "the name players see (up to 128 characters)", true},
      {"author", "string", "who made it"}, {"description", "string", "one or two sentences about it"}, {"stages", "boolean", "a mod of stages only (no story content: it needs no Story Notebook)"},
      {"confirm", "boolean", "must be true: a new package folder is written and the mod is turned on", true}},
     false, true},
    {"list_actors", "The open mod's placed actors (game actors and mod models).", {{"mod", "string", "another mod than the open one"}}},
    {"get_actor", "One placed actor as the manifest declares it.", {{"id", "string", "the actor's id", true}, {"mod", "string", ""}}},
    {"set_actor", "Sets one property of a placed actor (null removes it): position, rotation, scale, model, lines, collision, behaviour, buoyancy, drift, anchor, grapple_points, distant, shoreline, chart_marker, arrival, attention, animation, flag, stage, room, when, then, talks, look_at, path, parameters, defeated_flag, broken_flag, drops, native_drops, combat, carry, switch_flag, switch, texture, variant (a game object's). when: {quest, step, quests: [{quest, step}] (all must hold), item, time, flag, not_flag (a flag id)}. talks: up to 8 [{when?, lines, then?}], the first that matches is said (one without when last; not with lines or then). A chest's parameters: {item: rupee_green (1), rupee_blue (5), rupee_yellow (10), rupee_red (20), rupee_purple (50), rupee_orange (100, the default), rupee_silver (200), heart_piece, heart_container, or item:<id> (one of the mod's items; with hold_up Link holds its model up)}.",
     {{"id", "string", "the actor's id", true}, {"property", "string", "the manifest field", true}, {"value", "any", "the new value; null removes the field", true}, {"mod", "string", ""}},
     true},
    {"move_actor", "Moves a placed actor.", {{"id", "string", "", true}, {"position", "point", "{x, y, z} in world units", true}, {"mod", "string", ""}}, true},
    {"rotate_actor", "Turns a placed actor.", {{"id", "string", "", true}, {"rotation", "point", "{x, y, z} in degrees", true}, {"mod", "string", ""}}, true},
    {"scale_actor", "Scales a placed actor.", {{"id", "string", "", true}, {"scale", "point", "{x, y, z}, each above 0 and up to 10", true}, {"mod", "string", ""}}, true},
    {"set_dialogue", "Sets what a placed actor says (up to three lines).", {{"id", "string", "", true}, {"lines", "any", "a string or a list of up to three strings", true}, {"mod", "string", ""}}, true},
    {"list_models", "The open mod's models and the actors that use them.", {{"mod", "string", ""}}},
    {"list_scenes", "The open mod's scenes.", {{"mod", "string", ""}}},
    {"get_scene", "One scene as the manifest declares it.", {{"id", "string", "the scene's id", true}, {"mod", "string", ""}}},
    {"set_scene", "Sets one property of a scene (trigger, steps or native; null removes trigger or native).", {{"id", "string", "", true}, {"property", "string", "trigger, steps or native", true}, {"value", "any", "", true}, {"mod", "string", ""}}, true},
    {"list_quests", "The open mod's quests and their steps.", {{"mod", "string", ""}}},
    {"set_quest_step", "Sets one property of a quest step (text, where, place, npc, available, sail_line, find...). place: {x, z} at sea, or inside {stage, room, sea: {x, z}, point} (or x, y, z instead of point). available may use flag / not_flag (a flag id or a list) and needs a text.",
     {{"quest", "string", "the quest's id", true}, {"step", "integer", "the step's n", true}, {"property", "string", "", true}, {"value", "any", "null removes the field", true}, {"mod", "string", ""}},
     true},
    {"undo", "Takes back the last change.", {}},
    {"redo", "Does the last undone change again.", {}},
    {"history", "The undo and redo steps.", {}},
    {"validate", "Checks the open mod as the mod manager does.", {{"mod", "string", ""}}},
    {"save_package", "Writes the open mod's manifest into its package folder (replaces manifest.json).", {{"mod", "string", ""}, {"confirm", "boolean", "must be true: the file is replaced", true}}, false, true},
    {"revert_package", "Forgets the unsaved changes (an undo step).", {{"mod", "string", ""}, {"confirm", "boolean", "must be true", true}}, true, true},
    {"reload_room", "Leaves and enters the current room again (Link stays where he is): every actor of the room is created again.", {}},
    {"list_locations", "Warp destinations: the islands, the game's stages and rooms, the installed packages' stages, and the open mod's actors and quest places.", {{"group", "string", "Islands, Rooms, Mod stages or Quest"}}},
    {"warp", "Warps Link with the game's own stage change. Not during a cutscene, dialogue or menu.",
     {{"stage", "string", "the stage name (sea, M_NewD2, ...), or a package's stage: stage:<mod>/<id>", true}, {"room", "integer", "0..63", true}, {"point", "integer", "a spawn point; without it: the room's first spawn point, or \"position\""},
      {"position", "point", "stand here instead of at a spawn point"}, {"angle", "integer", "heading with position, the game's 16-bit angle"}}},
    {"warp_to", "Warps to an entry of list_locations.", {{"location", "string", "the location's id", true}}},
    {"warp_back", "Back to where Link was before the last warp.", {}},
    {"read_state", "Link's stage, room and position, the fly camera, test mode and the open mod's state.", {}},
    {"fly_camera", "The free camera: Link stays where he is and gets no input; fly with the mouse, WASD, Q/E, Shift (faster), or the controller's sticks and shoulder buttons.",
     {{"on", "boolean", "", true}, {"place_link", "boolean", "when turning it off: Link moves to the camera's position"}}},
    {"test_mode", "Test mode: in-game saves go to a separate test save, and the test commands are allowed.", {{"on", "boolean", "", true}}},
    {"reset_quest", "Test mode: erases the open mod's story progress from the loaded Quest Log: its quests, flags, letters, the charts and items Link got from it (never another mod's, never the game's own).", {{"mod", "string", ""}, {"confirm", "boolean", "must be true", true}}, false, true},
    {"save_state", "Test mode: saves a test state (the game's progress, Link's place and the open mod's flags).", {{"slot", "integer", "1..3", true}, {"kind", "string", "portable (default) or full"}}},
    {"load_state", "Test mode: loads a test state (the game's current progress is replaced).",
     {{"slot", "integer", "1..3", true}, {"kind", "string", "portable (default) or full"}, {"confirm", "boolean", "must be true: the current progress is replaced", true}}, false, true},
    {"command_log", "The last commands with their parameters and results.", {{"count", "integer", "how many (default 20)"}}},
    // building new content
    {"list_actor_types", "The kinds of actor add_actor places, and which appear without a restart; a kind's variants are the choices of its option (who a resident is).", {}},
    {"add_actor", "Places a new actor of the open mod at Link, at the fly camera or at a position (snapped to the ground).",
     {{"type", "string", "a list_actor_types type: sign, resident, chest, pot, barrel, crate, switch, door, chuchu, bokoblin, moblin, keese, miniblin, peahat, darknut, rat_hole, bokobaba, bubble, octorok, stalfos, redead, mothula, morth, magtail, poe, armos, armos_knight, kargaroc, wizzrobe, warship, gyorg, floormaster, big_octo, wall_cannon, model, model_enemy, model_carry; or a game object \"game:<id>\" or an island building \"piece:<id>\" (list_game_objects)", true},
      {"id", "string", "the new id (default: from the type)"}, {"at", "string", "link (default), camera or position"},
      {"position", "point", "with at = position"}, {"stage", "string", "with at = position (default: Link's)"}, {"room", "integer", "with at = position (default: Link's or the sea square's)"},
      {"angle", "number", "heading in degrees (default: facing like Link or the camera)"}, {"snap", "boolean", "onto the ground below (default true)"},
      {"model", "string", "the model for model types"}, {"options", "object", "resident (who: a list_actor_types variant such as uo2, um1, sa2 or the seated ym1), variant (an enemy's: chuchu green, red, blue, yellow or black; bokoblin stick, shield or machete; keese normal or fire; peahat peahat or seahat; bubble red or blue), switch (a door's switch id), path (a wall cannon's firing area: one of the mod's area paths; without it a new area in front is added), lines; variant also for a game object (list_game_objects)"}, {"mod", "string", ""}},
     true},
    {"list_game_objects", "The game's own objects and island buildings a mod places by reference (add_actor type \"game:<id>\" or \"piece:<id>\"; a building is prepared from the player's game files when first placed: state preparing, ready or failed), by category, with their variants, turn and scale limits and whether the player's game files have them.",
     {{"category", "string", "only this category"}, {"query", "string", "only those whose id, label or description contains this"}}},
    // ---- an actor's route (commands_route.cpp): a placed resident who walks (uo1, uo2, uo3, um1, um2) or a skinned model
    {"add_route_point", "Adds a point to an actor's route (its first point starts the route) at Link, the fly camera or a position, snapped to the ground. A resident starts at its position and walks to its points with its own walk (ub, uw and sa: a walk borrowed from another villager, fitted to them); a model's route lists every point, its position first.",
     {{"id", "string", "the actor", true}, {"at", "string", "link (default), camera or position"}, {"position", "point", "with at = position"},
      {"after", "integer", "insert after this point number (0: first; default: at the end)"}, {"snap", "boolean", "onto the ground (default true)"}, {"mod", "string", ""}},
     true},
    {"move_route_point", "Moves one point of an actor's route.",
     {{"id", "string", "", true}, {"point", "integer", "the point number (1 = the first after a resident's position)", true}, {"position", "point", "where to"},
      {"at", "string", "link or camera instead of position"}, {"snap", "boolean", "with at: onto the ground (default true)"}, {"mod", "string", ""}},
     true},
    {"remove_route_point", "Removes one point of an actor's route (the route goes when too few points are left).",
     {{"id", "string", "", true}, {"point", "integer", "the point number", true}, {"mod", "string", ""}}, true},
    {"reorder_route_point", "Moves a route point to another place in the order.",
     {{"id", "string", "", true}, {"point", "integer", "the point number", true}, {"to", "integer", "its new number", true}, {"mod", "string", ""}}, true},
    {"set_route", "Sets an actor's route settings: speed (game units per second; a resident: 0.5 to 2 times its own pace, null for its own), mode (once, loop or back_and_forth), pauses (short strolling pauses), and for a model its walk and idle clips.",
     {{"id", "string", "", true}, {"speed", "number|null", "units per second"}, {"mode", "string", "once, loop or back_and_forth"}, {"pauses", "boolean", "short strolling pauses on the way"},
      {"walk", "string|null", "a model's walk clip (default walk)"}, {"idle", "string|null", "a model's idle clip (default idle)"}, {"mod", "string", ""}},
     true},
    {"remove_route", "Removes an actor's route: it stands where it is placed.", {{"id", "string", "", true}, {"mod", "string", ""}}, true},
    // ---- the mod's stages (commands_stages.cpp, docs/mod-stages.md): new places built from the package's models
    {"import_ground", "Copies a room's .glb (from Blender, say) into the package (assets/<name>.glb, or \"as\") for add_stage, add_room or a rebuild; reports its parts, the parts without UVs and its material names.",
     {{"file", "string", "the .glb", true}, {"as", "string", "its path in the package"}, {"confirm", "boolean", "replace a different file of that name"}, {"mod", "string", ""}}},
    {"place_in_stage", "Places something of a stage where Link (or the fly camera, or a position) is: a spawn (\"point\" moves one), an exit box on the floor (\"to\": where it leads), a door facing his way (\"to_room\", \"lock\"), a return warp, a chest 1.5 m ahead facing him (\"item\"), an enemy (\"actor\", \"variant\"), a pot; or, standing in a game room, an entry (the way in, \"to_room\", \"to_point\").",
     {{"stage", "string", "", true}, {"what", "string", "spawn, exit, door, warp, entry, chest, enemy, pot or big_pot", true}, {"at", "string", "link (default), camera or position"},
      {"position", "point", "with at = position"}, {"room", "integer", "with at = position"}, {"id", "string", "its id"}, {"point", "integer", "a spawn's point"},
      {"to", "object", "an exit's or warp's place"}, {"to_room", "integer", "a door's other room, an entry's room"}, {"to_point", "integer", "an entry's spawn point"},
      {"lock", "string", "none, small_key, boss or shutter"}, {"switch", "integer", "a shutter's switch"}, {"item", "string", "a chest's: small_key, map, compass, boss_key, heart_container"},
      {"actor", "string", "an enemy: chuchu, bokoblin, keese, darknut"}, {"variant", "string", ""}, {"size", "point", "a box's size"},
      {"style", "string", "an entry's: walk (default) or updraft (the wind lifts Link first)"}, {"height", "number", "an updraft's lift in units (200..5000, default 1500)"}, {"after", "string", "a warp's: the chest (its id) that must be open first"}, {"angle", "number", ""}, {"mod", "string", ""}},
     true},
    {"list_stage_templates", "The stages of the player's game files a mod stage can take as its template: their rooms (kit rooms), whether they have a sky (an open-air stage), the kinds of door they have models of (knob, boss, small_key, shutter), and whether their music can be borrowed (music_like). Filter with sky and door.",
     {{"sky", "boolean", "only stages with (true) or without (false) a sky"}, {"door", "string", "only stages with this kind of door: knob, boss, small_key, shutter"}}},
    {"list_stages", "The mod's stages: id, template, kind, map, save group, rooms (ground, kit, floor, spawn points, exits, actors), doors, entries, whether the game has it built, and how to warp in.",
     {{"mod", "string", ""}}},
    {"add_stage", "Adds a stage with one room whose ground is a .glb in the package (named materials and UVs, see the docs). The spawn is the model's middle on its lowest point unless \"spawn\" is given. Then rebuild_stage and warp in.",
     {{"id", "string", "the stage's id", true}, {"ground", "string", "the room's .glb in the package (assets/hall.glb)", true}, {"template", "string", "the game stage it takes its light, camera, doors and kit room from (default Cave01)"},
      {"kind", "string", "cave or dungeon (default dungeon)"}, {"kit", "integer", "the template's room whose materials the ground uses (default 0)"},
      {"materials", "object", "the model's material names -> kit materials"}, {"material", "string", "the kit material for the rest"},
      {"spawn", "point", "where Link stands in the room"}, {"angle", "number", "the way he faces (degrees)"}, {"title", "string", "the dungeon map's title (gives the stage a map)"},
      {"music_like", "string", "a game stage whose music it plays"}, {"entrance", "object", "{stage, room, point}: where a save made in it continues without the package (default sea 34 point 1)"},
      {"entry", "object", "a way in: {stage, room, position} of a box in a game room (size 200, into room 0 point 0)"}, {"mod", "string", ""}},
     true},
    {"add_room", "Adds a room to a stage: ground a .glb in the package (or \"kit\" with \"kit\": a game room as it is); join it with a door or a join.",
     {{"stage", "string", "", true}, {"ground", "string", "a .glb in the package, or kit", true}, {"kit", "integer|object", "the template's room number, or {stage, room}"},
      {"floor", "integer", "its floor on the map (1, 2, -1, ...)"}, {"materials", "object", ""}, {"material", "string", ""}, {"spawn", "point", "where Link stands in it"},
      {"angle", "number", ""}, {"mod", "string", ""}},
     true},
    {"set_stage_field", "Sets one field of a stage by its path, e.g. map, entrance, music_like, doors, joins, entries, warps, rooms/1/exits, rooms/0/actors, rooms/0/spawns/0/position, rooms/2/materials; a list index one past the end adds, null removes. The value follows the manifest's format (docs/mod-stages.md).",
     {{"stage", "string", "", true}, {"field", "string", "the path in the stage", true}, {"value", "any", "the new value (null removes)"}, {"mod", "string", ""}},
     true},
    {"remove_stage", "Removes a stage from the mod.", {{"stage", "string", "", true}, {"confirm", "boolean", "must be true", true}, {"mod", "string", ""}}, true, true},
    {"check_stage", "Checks the mod's stages (or one): the manifest's rules, rooms that can't be reached in play with the keys the stage gives before each locked door, missing ground files, ground models without UVs or with one material for everything. Problems: {severity, stage, field, message}.",
     {{"stage", "string", ""}, {"mod", "string", ""}}},
    {"rebuild_stage", "Builds the mod's stages (or one) again from the open manifest and the package's files, in the player's stage cache. The stage Link is in loads again where he stands; others from their next way in. Reports each stage's problems from the builder.",
     {{"stage", "string", ""}, {"mod", "string", ""}}},
    {"reset_stage_save", "Test mode: forgets what the loaded Quest Log keeps of the mod's stages (opened chests and doors, keys, map, compass) for a fresh run.",
     {{"stage", "string", "only its save group"}, {"confirm", "boolean", "must be true", true}, {"mod", "string", ""}}, false, true},
    // ---- the mod's room paths (commands_paths.cpp): lines and areas its actors name by id ("path": "<id>")
    {"list_paths", "The mod's room paths: id, stage, room, closed, points, the actors that use each and its state in the game (its number and whether its record is written).",
     {{"mod", "string", ""}}},
    {"add_path", "Adds a room path in the room of Link, the fly camera or a position: an area (closed, the default: a square of \"size\" units around the place) or a line (closed false: from the place ahead along the heading), or the given points. A wall cannon's firing area is an area.",
     {{"id", "string", "the new id (default area or path)"}, {"closed", "boolean", "an area (default true) or a line"}, {"at", "string", "link (default), camera or position"},
      {"position", "point", "with at = position"}, {"stage", "string", "with at = position"}, {"room", "integer", "with at = position"}, {"size", "number", "the default shape's size (50..20000, default 800)"},
      {"points", "array", "the points instead of the default shape ({x, y, z}, 2 to 32)"}, {"snap", "boolean", "the place onto the ground (default true)"}, {"mod", "string", ""}},
     true},
    {"add_path_point", "Adds a point to a room path at Link, the fly camera or a position (snapped to the ground).",
     {{"id", "string", "the path", true}, {"at", "string", "link (default), camera or position"}, {"position", "point", "with at = position"},
      {"after", "integer", "insert after this point number (0: first; default: at the end)"}, {"snap", "boolean", "onto the ground (default true)"}, {"mod", "string", ""}},
     true},
    {"move_path_point", "Moves one point of a room path.",
     {{"id", "string", "", true}, {"point", "integer", "the point number (1 = the first)", true}, {"position", "point", "where to"},
      {"at", "string", "link or camera instead of position"}, {"snap", "boolean", "with at: onto the ground (default true)"}, {"mod", "string", ""}},
     true},
    {"remove_path_point", "Removes one point of a room path (an area keeps 3, a line 2).", {{"id", "string", "", true}, {"point", "integer", "the point number", true}, {"mod", "string", ""}}, true},
    {"set_path_point", "Marks a point of a room path: release (a Kargaroc lets its rider go there).",
     {{"id", "string", "", true}, {"point", "integer", "the point number", true}, {"release", "boolean", "", true}, {"mod", "string", ""}}, true},
    {"set_path", "Changes a room path: closed (an area) or open (a line), its next path, or its id (the actors and paths that name it follow).",
     {{"id", "string", "", true}, {"closed", "boolean", ""}, {"new_id", "string", "a new id"}, {"next", "string|null", "the path that follows it (a Wizzrobe summoner's summon points)"}, {"mod", "string", ""}}, true},
    {"remove_path", "Removes a room path; refused while an actor uses it (the reason names them).", {{"id", "string", "", true}, {"mod", "string", ""}}, true},
    {"check_path", "A warning check of a route (never a refusal): for a warship, segments over land; for a Floormaster, segments off the floor. Sampled along the route, closed as they move it.",
     {{"id", "string", "", true}, {"for", "string", "warship or floormaster (default: the actor that uses the path)"}, {"mod", "string", ""}}},
    {"path_inside", "Whether Link, the fly camera or a position is inside an area path, by the game's own area test (the one a wall cannon uses).",
     {{"id", "string", "", true}, {"at", "string", "link (default), camera or position"}, {"position", "point", "with at = position"},
      {"stage", "string", "with at = position (default: Link's)"}, {"room", "integer", "with at = position (default: Link's)"}, {"snap", "boolean", "default true"}, {"mod", "string", ""}}},
    {"check_route", "Checks the ground under each segment of an actor's route (the game's collision): no ground, a drop or a step too high (a wall) marks it.",
     {{"id", "string", "", true}, {"mod", "string", ""}}},
    {"route_status", "A walking resident's route in the game: whether its copy follows it, and walking, skipped a blocked point, stopped (blocked or off its ground) or finished.",
     {{"id", "string", "", true}, {"mod", "string", ""}}},
    {"remove_actor", "Removes a placed actor (an undo step).", {{"id", "string", "", true}, {"confirm", "boolean", "must be true", true}, {"mod", "string", ""}}, true, true},
    {"duplicate_actor", "Copies a placed actor next to it under a new id, with flags of its own.",
     {{"id", "string", "", true}, {"new_id", "string", "default: from the id"}, {"offset", "point", "default {x: 100, y: 0, z: 0}"}, {"mod", "string", ""}}, true},
    {"place_actor", "Moves a placed actor to Link, the fly camera, the ground below it or a position, snapped to the ground.",
     {{"id", "string", "", true}, {"at", "string", "link, camera, ground or position", true}, {"position", "point", "with at = position"},
      {"snap", "boolean", "onto the ground (default true)"}, {"mod", "string", ""}},
     true},
    {"place_world_object", "Places a static, floating or drifting mod object with collision in one undoable change.",
     {{"id", "string", "new actor id", true}, {"model", "string", "render model id", true}, {"collision", "object", "{mesh: model} or a simple shape", true},
      {"behaviour", "string", "static (default), floating or drifting"}, {"buoyancy", "object", "draft, damping seconds and max_tilt degrees"},
      {"drift", "object", "wind_speed, current {x,z}, max_speed in units per second"}, {"anchor", "object", "world point and tether radius; drifting only"}, {"grapple_points", "array|null", "up to eight local grapple points {id,position,yaw}; null clears"},
      {"distant", "object|null", "{model}: original low-detail model for adjacent sea squares"}, {"shoreline", "object|null", "{model, material?}: original coast foam model, or with material \"water\" a still-water surface"},
      {"chart_marker", "object|null", "{label,when_flag?}: mod island marker"}, {"arrival", "object|null", "{name,radius,flag}: mod-owned arrival notice"}, {"stage", "string", "stage name", true}, {"room", "integer", "room number", true},
      {"position", "point", "world position", true}, {"rotation", "point", "degrees"}, {"scale", "point", "positive scale"}, {"mod", "string", ""}}, true},
    {"set_world_object", "Changes world-object collision, behaviour and motion parameters atomically.",
     {{"id", "string", "placed actor id", true}, {"collision", "object", "mesh reference or simple shape; unchanged when omitted"}, {"behaviour", "string", "static, floating or drifting; unchanged when omitted"},
      {"buoyancy", "object|null", "draft, damping seconds and max_tilt degrees"}, {"drift", "object|null", "wind_speed, current {x,z}, max_speed in units per second"}, {"anchor", "object|null", "world point and tether radius; null removes it"}, {"grapple_points", "array|null", "local grapple points; omitted preserves, null clears"},
      {"distant", "object|null", "low-detail model; null removes it"}, {"shoreline", "object|null", "coast foam or water surface model; null removes it"},
      {"chart_marker", "object|null", "island marker; null removes it"}, {"arrival", "object|null", "arrival notice; null removes it"}, {"mod", "string", ""}}, true},
    {"preview_collision", "Returns world-space collision vertices and edges for a wireframe preview, with placement findings.",
     {{"id", "string", "placed world object", true}, {"position", "point", "a trial position (default: its own)"}, {"yaw", "number", "a trial turn in degrees"}, {"mod", "string", ""}}},
    {"placement_conflicts", "Returns placement conflicts and collision budget findings for a world object.",
     {{"id", "string", "placed world object", true}, {"position", "point", "a trial position (default: its own)"}, {"yaw", "number", "a trial turn in degrees"}, {"mod", "string", ""}}},
    {"prepare_collision", "Prepares a static model's collision for live world-object placement.",
     {{"model", "string", "declared static model id", true}, {"mod", "string", ""}}},
    {"check_water", "What a pond, river or waterfall would be (the water tool), and every problem with it (errors and warnings), without changing anything: its place, triangles, memory, a river's steps and levels.",
     {{"kind", "string", "pond, river or waterfall", true}, {"points", "array", "pond: the outline; river: the centre line, in the flow's order; waterfall: the top edge's 2 points (or \"river\" and \"step\"). Each {x, z} (or {x, y, z}) in world units"},
      {"level", "number", "pond: the water's height (world); default: depth above the lowest ground along the shore (swimmable)"}, {"depth", "number", "pond: floor below the level; river: water above the bed (20 to 480; default 200)"},
      {"rim", "number", "pond: banks above the water (0 to 300; default 0)"}, {"foam", "boolean", "pond: a foam ring at the shore"}, {"floor", "string", "the floor's (river bed's, rock's) colour \"#rrggbb\""},
      {"width", "number", "river: 100 to 2000 (default 400)"}, {"shallow", "any", "river: true / false per section (point i to i+1), or one for all: wading water"},
      {"top", "number", "waterfall: the top edge's height (default: the ground there)"}, {"bottom", "number", "waterfall: the ground below (default: the ground in front of the edge)"},
      {"side", "integer", "waterfall: 1 or -1, the side it falls to (default: the lower one)"}, {"river", "string", "waterfall: at this river's step (instead of points)"}, {"step", "integer", "waterfall: the river's step (1-based)"},
      {"id", "string", "a body of water being changed: its own models and place don't count against it"}, {"mod", "string", ""}}},
    {"add_pond", "Makes a pond (the water tool): its outline at the water's level, with a floor, banks and optional foam; the water surface and swimmable collision as two generated models; one undo step.",
     {{"id", "string", "the new actor's id (default pond, pond-2...)"}, {"points", "array", "the outline: 3 to 48 world points {x, z}", true}, {"level", "number", "the water's height (default: depth above the lowest ground along the shore, swimmable)"},
      {"depth", "number", "floor below the level (20 to 480, default 200)"}, {"rim", "number", "banks above the water (0 to 300)"}, {"foam", "boolean", "a foam ring at the shore"},
      {"floor", "string", "the floor's colour \"#rrggbb\""}, {"mod", "string", ""}}, true},
    {"add_river", "Makes a river (the water tool): a centre line (the flow goes from the first point to the last) with a width; it follows the ground and steps down where the ground drops; shallow sections are for wading; one undo step.",
     {{"id", "string", "the new actor's id (default river...)"}, {"points", "array", "the centre line: 2 to 32 world points {x, z}", true}, {"width", "number", "100 to 2000 (default 400)"},
      {"depth", "number", "water above the bed in deep sections (default 200)"}, {"shallow", "any", "true / false per section, or one for all"}, {"floor", "string", "the bed's colour \"#rrggbb\""}, {"mod", "string", ""}}, true},
    {"add_waterfall", "Makes a waterfall (the water tool): a falling sheet from a top edge (two points, or a river's step) down to the ground, with its splash ground below; one undo step.",
     {{"id", "string", "the new actor's id (default waterfall...)"}, {"points", "array", "the top edge: 2 world points {x, z}"}, {"river", "string", "at this river's step instead"},
      {"step", "integer", "the river's step (1-based, default 1)"}, {"top", "number", "the top edge's height"}, {"bottom", "number", "the ground below"}, {"side", "integer", "1 or -1: the side it falls to"},
      {"floor", "string", "the rock's colour \"#rrggbb\""}, {"mod", "string", ""}}, true},
    {"edit_water", "Changes a pond, river or waterfall made with the water tool: give the parameters that change (as for add_pond, add_river, add_waterfall); it is built again where it stands; one undo step.",
     {{"id", "string", "the body of water's actor", true}, {"points", "array", "the new outline, centre line or top edge"}, {"level", "number", "pond"}, {"depth", "number", ""}, {"rim", "number", "pond"},
      {"foam", "boolean", "pond"}, {"floor", "string", "\"#rrggbb\""}, {"width", "number", "river"}, {"shallow", "any", "river"}, {"top", "number", "waterfall"}, {"bottom", "number", "waterfall"},
      {"side", "integer", "waterfall"}, {"mod", "string", ""}}, true},
    {"get_water", "A body of water's parameters as the water tool keeps them (world points, level, depth...), and a river's steps and levels.",
     {{"id", "string", "the body of water's actor", true}, {"mod", "string", ""}}},
    {"pick", "The first ground a ray meets (the game's collision), for placing by clicking into the picture.",
     {{"origin", "point", "where the ray starts", true}, {"direction", "point", "its direction", true}, {"range", "number", "default 20000"}}},
    {"ground", "The ground height below a point (the game's collision).", {{"position", "point", "", true}}},
    {"set_then", "Sets what happens after an actor's dialogue: give / take an item, give a chart (once; it shows in the MAP screen's chart list as a letter's chart does), a quest step, finish a quest (quest_done), a flag, a scene. The same actions work in talks' then and a scene's end step.",
     {{"id", "string", "", true}, {"actions", "array", "[{action: give|take, id: item:<id>, count}, {action: give, id: chart:<id>}, {action: quest_step, id, step}, {action: quest_done, id}, {action: flag, id, value}, {action: scene, id}]", true}, {"mod", "string", ""}},
     true},
    {"set_drops", "Sets what an enemy or prop drops: up to eight of the mod's items.", {{"id", "string", "", true}, {"drops", "array", "[{item: item:<id>, quantity}]", true}, {"mod", "string", ""}}, true},
    {"preview_dialogue", "Shows an actor's dialogue (or given lines) in the game's own message box; close hides it.",
     {{"id", "string", "the actor"}, {"lines", "any", "lines instead of the actor's"}, {"close", "boolean", "hide the box"}, {"mod", "string", ""}}},
    {"check_model", "Checks a .glb file for the game: size, triangles, texture, bones, clips and material.", {{"file", "string", "the .glb file", true}}},
    {"import_model", "Copies a .glb file into the open mod's models folder, adds it to the package and prepares it for the game.",
     {{"file", "string", "the .glb file", true}, {"id", "string", "the model's id (default: the file name)"}, {"confirm", "boolean", "replace a different file of the same name"}, {"mod", "string", ""}},
     true},
    {"model_clips", "A model's animation clips (for a route's walk and idle clips).", {{"model", "string", "the model's id", true}, {"mod", "string", ""}}},
    {"check_package", "Every problem of the open mod's package, each with the actor, model, scene or quest it is about.", {{"mod", "string", ""}}},
    {"export_package", "Checks the open mod and writes its package (manifest and its own files) into a new or empty folder.",
     {{"path", "string", "the folder", true}, {"confirm", "boolean", "must be true", true}, {"mod", "string", ""}}, false, true},
    {"focus", "Points the fly camera at a placed actor (a world object: its whole extent), or at a place.",
     {{"id", "string", "the actor (or give position instead)"}, {"position", "point", "a place to look at instead of an actor"}, {"distance", "number", "how far back the camera goes (default: the object's size, at least 700)"},
      {"yaw", "number", "the direction it looks, degrees (0: towards +z; default: its own)"}, {"mod", "string", ""}}},
    {"play", "Play: the editor hides and Link plays (test mode on: saves go to the test save) from the camera's place or his own.",
     {{"from", "string", "camera (default) or link"}, {"restore", "boolean", "Stop returns to the world as it was before Play (default true)"}}},
    {"stop", "Stop: back to the editor's camera view (restoring the world from before Play when asked).", {}},
    // ---- quests, flags, search, copy and paste (commands_quest.cpp)
    {"get_quest", "One quest as the manifest declares it.", {{"quest", "string", "the quest's id", true}, {"mod", "string", ""}}},
    {"quest_graph", "A quest as a graph: Start, its steps and End as nodes; what moves the quest on (a letter, an actor, an item, a scene, a resident's talk) as edges; each node's problems, the editor's node places and the step the loaded game is at.",
     {{"quest", "string", "the quest's id", true}, {"mod", "string", ""}}},
    {"add_quest", "Adds a quest to the open mod (with its first step).",
     {{"id", "string", "the quest's id (default: from the title)"}, {"title", "string", "the title the notebook shows", true}, {"text", "string", "the first step's text"}, {"mod", "string", ""}},
     true},
    {"remove_quest", "Removes a quest and every actor condition and action that names it (an undo step).", {{"quest", "string", "", true}, {"confirm", "boolean", "must be true", true}, {"mod", "string", ""}}, true, true},
    {"set_quest", "Sets one property of a quest: title, journal, start, reward, letter (null removes it).",
     {{"quest", "string", "", true}, {"property", "string", "title, journal, start, reward or letter", true}, {"value", "any", "", true}, {"mod", "string", ""}}, true},
    {"add_quest_step", "Adds a step to a quest: after a step (later steps move up by one) or at the end.",
     {{"quest", "string", "", true}, {"after", "integer", "the step it follows (default: the last)"}, {"text", "string", "what to do next"}, {"where", "string", "the place's name"},
      {"place", "object", "{x, z} on the sea chart"}, {"mod", "string", ""}},
     true},
    {"edit_quest_step", "Sets several properties of a quest step at once: text, where, place, npc, available, sail_line, find (null removes one). place: {x, z} at sea, or inside {stage, room, sea: {x, z}, point} (or x, y, z instead of point); available may use flag / not_flag (a flag id or a list) and needs a text.",
     {{"quest", "string", "", true}, {"step", "integer", "the step's n", true}, {"fields", "object", "{property: value}", true}, {"mod", "string", ""}}, true},
    {"remove_quest_step", "Removes a quest step and the actor conditions and actions that name it.",
     {{"quest", "string", "", true}, {"step", "integer", "", true}, {"confirm", "boolean", "must be true", true}, {"mod", "string", ""}}, true, true},
    {"duplicate_quest_step", "Copies a quest step as a new last step.", {{"quest", "string", "", true}, {"step", "integer", "", true}, {"mod", "string", ""}}, true},
    {"move_quest_step", "Moves a step to another place in the order: the steps get new numbers and every actor and item that names them follows.",
     {{"quest", "string", "", true}, {"step", "integer", "", true}, {"to", "integer", "its new place, 1 = first", true}, {"mod", "string", ""}}, true},
    {"set_step_place", "Sets a step's place on the sea chart (and its name) from Link's place, the camera's or a warp destination.",
     {{"quest", "string", "", true}, {"step", "integer", "", true}, {"from", "string", "link, camera, location or none (removes it)", true},
      {"location", "string", "a list_locations id with from = location"}, {"mod", "string", ""}},
     true},
    {"connect_quest_steps", "Makes an actor move the quest on: while the quest is at step \"from\" (0: any time) talking to it, reading or opening it sets step \"to\". Also changes the edge's conditions (item, time).",
     {{"quest", "string", "", true}, {"from", "integer", "the step (0: any time)", true}, {"to", "integer", "the step it sets", true}, {"via", "string", "the actor's id", true},
      {"item", "string", "a mod item Link must carry (item:<id>; \"-\" removes)"}, {"time", "string", "day or night (\"-\" removes)"}, {"mod", "string", ""}},
     true},
    {"disconnect_quest_steps", "Removes an actor's action that sets a step.",
     {{"quest", "string", "", true}, {"from", "integer", "", true}, {"to", "integer", "", true}, {"via", "string", "the actor's id", true}, {"mod", "string", ""}}, true},
    {"set_quest_layout", "Places of a quest's graph nodes in the editor (its own file in the package, never the manifest; the game ignores it).",
     {{"quest", "string", "", true}, {"positions", "object", "{node id: {x, y}}, node ids start, end, step:N", true}, {"mod", "string", ""}}, true},
    {"start_at_step", "Test mode (turned on first): starts the quest at a step. Sets this mod's quest step and the mod flags and items the earlier steps hand out, warps there and plays from Link. Never sets the game's own event or save bits.",
     {{"quest", "string", "", true}, {"step", "integer", "", true}, {"warp", "string", "next (in front of the actor that moves it on, the default), place or none"},
      {"play", "boolean", "play from Link afterwards (default true)"}, {"mod", "string", ""}}},
    {"list_villagers", "The game's own villagers the NPC catalogue knows that are loaded now, each with its catalogue id (for a quest step's npc talk), name and place. Read-only.", {}},
    {"pick_villager", "The game villager a ray meets first (for clicking into the picture): its catalogue id, name and place.",
     {{"origin", "point", "where the ray starts", true}, {"direction", "point", "its direction", true}, {"range", "number", "default 20000"}}},
    {"add_villager_talk", "Gives one of the game's villagers a quest talk in the open mod (one undo step): a new quest (title) with this talk as its first step, a new last step of a quest, or a step's talk (step). The villager's own dialogue, quests and save flags are not changed.",
     {{"who", "string", "the villager's catalogue id (list_villagers)", true}, {"lines", "any", "what the villager says: a string or up to three strings", true},
      {"quest", "string", "an existing quest of the open mod (without it: a new quest)"}, {"step", "integer", "with quest: that step gets the talk (without it: a new last step)"},
      {"title", "string", "a new quest's title"}, {"id", "string", "a new quest's id (default: from the title)"}, {"text", "string", "a new step's text (default: Talk to <name>.)"},
      {"mod", "string", ""}},
     true},
    {"quest_state", "The step each of the open mod's quests is at in the loaded game, as the status bar says it (\"Lighthouse quest: step 3 of 6\").", {{"mod", "string", ""}}},
    {"list_flags", "The open mod's flags (actors, switches, flag actions, props, enemies), who uses each, and their values in the loaded game.", {{"mod", "string", ""}}},
    {"set_flag", "Test mode: sets one of the open mod's flags in the loaded game (never a game flag).",
     {{"id", "string", "the flag's id", true}, {"value", "boolean", "", true}, {"mod", "string", ""}}},
    {"advance_days", "Test mode: moves the game's calendar on by some days (as the clock passing midnight does), to test letters that wait (after_days).",
     {{"days", "integer", "1..255", true}}},
    {"search", "Everything of the open mod whose id or text contains the text: actors, quests and steps, scenes, items, flags.", {{"text", "string", "", true}, {"mod", "string", ""}}},
    {"copy", "Copies actors, quest steps, scenes or scene steps for paste.",
     {{"ids", "array", "actor ids"}, {"quest", "string", "with steps: the quest"}, {"steps", "array", "step numbers of that quest (or of the scene)"},
      {"scenes", "array", "scene ids"}, {"scene", "string", "with steps: the scene (steps by number or id)"}, {"mod", "string", ""}},
     },
    {"paste", "Pastes what copy took: actors (new ids and flags of their own) offset, at Link, at the camera or at a position (into the room there); quest steps as new last steps of a quest; scenes under new ids; scene steps before a scene's end (or before a step).",
     {{"at", "string", "actors: offset (default), link, camera or position (in the room Link is in)"}, {"offset", "point", "with at = offset (default {x: 100, y: 0, z: 0})"},
      {"position", "point", "with at = position: where the first one goes (the others keep their places around it)"},
      {"quest", "string", "steps: the quest (default: the one they came from)"}, {"scene", "string", "scene steps: the scene (default: the one they came from)"},
      {"before", "any", "scene steps: the step they go before (number or id; default: the scene's last step)"}, {"mod", "string", ""}},
     true},
    {"move_actors", "Moves several actors by an offset (one undo step).", {{"ids", "array", "", true}, {"offset", "point", "", true}, {"mod", "string", ""}}, true},
    {"remove_actors", "Removes several actors (one undo step).", {{"ids", "array", "", true}, {"confirm", "boolean", "must be true", true}, {"mod", "string", ""}}, true, true},
    // ---- scenes: steps, camera keys, timeline, preview (commands_scene.cpp)
    {"add_scene", "Adds a scene to the open mod (with its end step).", {{"id", "string", "the scene's id (default: scene, scene-2...)"}, {"mod", "string", ""}}, true},
    {"remove_scene", "Removes a scene (refused while an actor or another scene starts it).", {{"id", "string", "", true}, {"confirm", "boolean", "must be true", true}, {"mod", "string", ""}}, true, true},
    {"duplicate_scene", "Copies a scene under a new id (without its trigger).", {{"id", "string", "", true}, {"new_id", "string", "default: from the id"}, {"mod", "string", ""}}, true},
    {"set_scene_trigger", "Sets what starts a scene: none, talk (an actor), quest_step, room or area (where Link or the camera is, or a given place) or salvage (a chart).",
     {{"scene", "string", "", true}, {"type", "string", "none, talk, quest_step, room, area or salvage", true}, {"actor", "string", "talk: the actor"},
      {"quest", "string", "quest_step: the quest"}, {"step", "integer", "quest_step: the step"}, {"chart", "string", "salvage: the chart"},
      {"at", "string", "room / area: link (default) or camera"}, {"stage", "string", "room / area"}, {"room", "integer", "room / area"},
      {"position", "point", "area: its centre"}, {"radius", "number", "area: its radius (default 500)"}, {"mod", "string", ""}},
     true},
    {"add_scene_step", "Adds a step to a scene, with defaults that fit (a camera takes the fly camera's view; a choice comes with its two branches).",
     {{"scene", "string", "", true}, {"type", "string", "camera, say, move, face, wait, sound, fade, end, camera_path, camera_follow, animation, walk, choice, music, particle or demo", true},
      {"at", "integer", "the place it takes (1 = first; default: before the scene's last step)"}, {"fields", "object", "fields to set over the defaults (null removes one)"}, {"mod", "string", ""}},
     true},
    {"edit_scene_step", "Sets several fields of a scene step at once (null removes one).",
     {{"scene", "string", "", true}, {"step", "any", "the step's number (1 = first) or id", true}, {"fields", "object", "{field: value}", true}, {"mod", "string", ""}}, true},
    {"remove_scene_step", "Removes a scene step (refused while another step leads to it).", {{"scene", "string", "", true}, {"step", "any", "number or id", true}, {"mod", "string", ""}}, true},
    {"move_scene_step", "Moves a scene step to another place in the list.", {{"scene", "string", "", true}, {"step", "any", "number or id", true}, {"to", "integer", "its new place, 1 = first", true}, {"mod", "string", ""}}, true},
    {"duplicate_scene_step", "Copies a scene step right after it.", {{"scene", "string", "", true}, {"step", "any", "number or id", true}, {"mod", "string", ""}}, true},
    {"add_camera_key", "Takes a camera key from the fly camera's view (or an eye and target): without step a new camera step; on a camera step it becomes a camera path; on a camera path a new key.",
     {{"scene", "string", "", true}, {"step", "any", "a camera or camera_path step (number or id)"}, {"at", "integer", "the key's place in the path (1 = first; default: the end)"},
      {"frames", "integer", "frames from the key before it (60 a second; default 60)"}, {"eye", "point", "instead of the fly camera's view"}, {"target", "point", "with eye"}, {"mod", "string", ""}},
     true},
    {"set_camera_key", "Sets a camera key to the fly camera's view (or an eye and target) and its time to the next key.",
     {{"scene", "string", "", true}, {"step", "any", "number or id", true}, {"key", "integer", "1 = first", true}, {"from", "string", "camera (default) or none (keep the view)"},
      {"eye", "point", ""}, {"target", "point", ""}, {"frames", "integer", "frames to the next key"}, {"mod", "string", ""}},
     true},
    {"remove_camera_key", "Removes a key of a camera path (a path of two keys becomes a camera step).",
     {{"scene", "string", "", true}, {"step", "any", "number or id", true}, {"key", "integer", "1 = first", true}, {"mod", "string", ""}}, true},
    {"retime_scene_step", "Changes a step's duration on the timeline (frames, 60 a second): a timed step, one key of a camera path, or the whole path (scaled).",
     {{"scene", "string", "", true}, {"step", "any", "number or id", true}, {"frames", "integer", "the new duration", true}, {"key", "integer", "a camera path's key: the time from it to the next"}, {"mod", "string", ""}},
     true},
    {"scene_timeline", "A scene on a time axis: each step's start and duration on the default path (each choice's default answer), branches, camera keys.",
     {{"scene", "string", "", true}, {"mod", "string", ""}}},
    {"demo_lanes", "A demo step's timeline: its lanes (camera and each actor's position, rotation, scale) with their key frames, its events (clips, modes, messages, sounds, particles, attach, detach, time, weather), its actors, and why the game would refuse it.",
     {{"scene", "string", "", true}, {"step", "any", "the demo step (number or id)", true}, {"mod", "string", ""}}},
    {"demo_pose", "The demo at a frame (30 a second) without playing it: the camera and every actor's position, rotation and scale in the world (an origin \"link\" demo is placed at Link); the fly camera shows the demo camera unless move is false.",
     {{"scene", "string", "", true}, {"step", "any", "number or id", true}, {"frame", "number", "frames from the demo's start", true}, {"move", "boolean", "move the fly camera (default true)"}, {"mod", "string", ""}}},
    {"set_demo_key", "Sets a key of a demo lane (added when the lane has none at that frame): lanes camera.eye, camera.target, camera.fovy, camera.roll, <actor>.position, <actor>.rotation, <actor>.scale.",
     {{"scene", "string", "", true}, {"step", "any", "number or id", true}, {"lane", "string", "", true}, {"frame", "integer", "0..the demo's frames", true},
      {"value", "any", "{x, y, z} in the demo's own coordinates, or a number (fovy, roll)"}, {"from", "string", "camera (the fly camera's eye for camera.eye, its target for camera.target) or link (Link's position): instead of value"},
      {"curve", "string", "linear, smooth, bspline or step"}, {"mod", "string", ""}},
     true},
    {"move_demo_key", "Moves a demo key to another frame.", {{"scene", "string", "", true}, {"step", "any", "", true}, {"lane", "string", "", true}, {"frame", "integer", "", true}, {"to", "integer", "", true}, {"mod", "string", ""}}, true},
    {"remove_demo_key", "Removes a demo key (a lane goes with its last key).", {{"scene", "string", "", true}, {"step", "any", "", true}, {"lane", "string", "", true}, {"frame", "integer", "", true}, {"mod", "string", ""}}, true},
    {"set_demo_curve", "A demo lane's curve: linear, smooth (through the keys), bspline (rounder) or step.",
     {{"scene", "string", "", true}, {"step", "any", "", true}, {"lane", "string", "", true}, {"curve", "string", "", true}, {"mod", "string", ""}}, true},
    {"add_demo_event", "Adds an event to a demo list in frame order: clips, modes, data (an actor's: give actor), messages, sounds, particles, attach, detach, time, weather.",
     {{"scene", "string", "", true}, {"step", "any", "", true}, {"list", "string", "", true}, {"actor", "string", "the actor (clips, modes, data)"}, {"event", "object", "the event's fields, with frame", true}, {"mod", "string", ""}},
     true},
    {"edit_demo_event", "Sets fields of a demo event (null removes one).",
     {{"scene", "string", "", true}, {"step", "any", "", true}, {"list", "string", "", true}, {"actor", "string", ""}, {"index", "integer", "0-based, as demo_lanes lists them", true}, {"fields", "object", "", true}, {"mod", "string", ""}},
     true},
    {"remove_demo_event", "Removes a demo event.", {{"scene", "string", "", true}, {"step", "any", "", true}, {"list", "string", "", true}, {"actor", "string", ""}, {"index", "integer", "", true}, {"mod", "string", ""}}, true},
    {"add_demo_actor", "Adds an actor to a demo: {id, and actor (a game actor's stage name or Link), model {archive, file} or character (one of the mod's placed actors)}.",
     {{"scene", "string", "", true}, {"step", "any", "", true}, {"actor", "object", "", true}, {"mod", "string", ""}}, true},
    {"remove_demo_actor", "Removes an actor from a demo (refused while a sound, particle or attachment names it).",
     {{"scene", "string", "", true}, {"step", "any", "", true}, {"id", "string", "", true}, {"mod", "string", ""}}, true},
    {"scrub_scene", "The scene's camera at a time (frames on the default path); the fly camera shows it unless move is false.",
     {{"scene", "string", "", true}, {"time", "number", "frames from the start", true}, {"move", "boolean", "move the fly camera there (default true)"}, {"mod", "string", ""}}},
    {"set_scene_choice", "Changes a choice step: an option's label and the step it leads to, or the default answer.",
     {{"scene", "string", "", true}, {"step", "any", "the choice step (number or id)", true}, {"option", "integer", "1 or 2"}, {"label", "string", "the option's text (up to 32 characters)"},
      {"next", "any", "the step the option leads to (a later step's number or id)"}, {"default", "integer", "the answer the cursor starts on: 1 or 2"}, {"mod", "string", ""}},
     true},
    {"add_walk_point", "Adds a point to a walk step's path: where the fly camera looks (on the ground), at Link, or at a position.",
     {{"scene", "string", "", true}, {"step", "any", "a walk step (number or id)", true}, {"at", "integer", "the point's place (1 = first; default: the end)"},
      {"from", "string", "camera (default: the ground where it looks), link or position"}, {"position", "point", "with from = position"}, {"mod", "string", ""}},
     true},
    {"set_walk_point", "Moves a point of a walk step's path.", {{"scene", "string", "", true}, {"step", "any", "", true}, {"point", "integer", "1 = first", true}, {"position", "point", "", true}, {"mod", "string", ""}}, true},
    {"remove_walk_point", "Removes a point of a walk step's path (two stay at least).", {{"scene", "string", "", true}, {"step", "any", "", true}, {"point", "integer", "1 = first", true}, {"mod", "string", ""}}, true},
    {"native_scene", "The scene's native version (the game's own event: two uo2 residents turn, the camera holds and moves): made from its steps, set, or removed; says whether it would play.",
     {{"scene", "string", "", true}, {"from_steps", "boolean", "make it from the scene's face, camera and wait steps"}, {"value", "any", "the native block to set (null removes it)"}, {"mod", "string", ""}},
     true},
    {"preview_scene", "Test mode: plays a scene (or steps from..to of it) in the game from the editor's view; the editor's view comes back when it ends. Not while playing.",
     {{"scene", "string", "", true}, {"from", "any", "the first step (number or id)"}, {"to", "any", "the last step (default: from, or the scene's end)"},
      {"restore", "boolean", "afterwards the world goes back to how it was (default true)"}, {"mod", "string", ""}}},
    {"skip_scene", "Skips the scene that plays (as Plus / Start does): its end actions don't run.", {}},
    {"stop_scene", "Stops the scene preview at once and goes back to the editor's view.", {}},
    {"scene_state", "Whether a scene preview plays, which scene and step.", {}},
    {"demo_choices", "Names for a demo step's pickers: sound effects or particles (id and a readable name), actors (stage actor name, a readable name, its archive, a suggested id), Link's demo modes, the game's object archives, an archive's files or its models' joints.",
     {{"what", "string", "sounds, particles, actors, link_modes, archives, files or joints", true}, {"query", "string", "only those whose name contains these words"}, {"archive", "string", "with files and joints: the object archive"}}},
    {"play_sound", "Plays a sound effect with the game's own sound playback (a demo sound's preview).", {{"id", "number", "the sound effect id", true}}},
    // ---- letters on their own, treasure charts, items (commands_story.cpp)
    {"list_letters", "The open mod's letters on their own (not a quest's letter): id, sender, text, encloses, when, after_days. The Rito postbox hands each out while the mod is on; once read, Link has what it encloses.",
     {{"mod", "string", ""}}},
    {"add_letter", "Adds a letter on its own: no quest, no notebook entry. The Rito postbox hands it out while the mod is on (once Link has the Delivery Bag), or only once its when holds and after_days days have passed; once it is read, Link gets what it encloses, once. Up to 8 per mod; for a letter that starts a quest use set_quest letter.",
     {{"id", "string", "1..32 of a-z 0-9 - _ . (default: letter, letter-2...); not a quest letter's id"}, {"sender", "string", "one line, up to 48 bytes: the signature", true},
      {"text", "string", "up to 1200 bytes; \\n breaks a line (keep to about ten lines of 40 characters)", true},
      {"encloses", "array", "[\"chart:<id>\", \"item:<id>\"]: the mod's charts and items it gives"},
      {"when", "object", "send it only once all hold: {quest, step} (the quest reached the step or went past it), {quest, done: true}, quests: [{quest, step} or {quest, done: true}], flag, not_flag (the mod's flags). E.g. {quest: lighthouse, done: true}"},
      {"after_days", "integer", "1..255: game days (the game's clock passing midnight) after when first holds, or after the mod is on without when"},
      {"mod", "string", ""}},
     true},
    {"set_letter", "Sets fields of a letter on its own: sender, text, encloses, when, after_days (null removes encloses, when or after_days).",
     {{"id", "string", "the letter's id", true}, {"fields", "object", "{field: value}", true}, {"mod", "string", ""}}, true},
    {"remove_letter", "Removes a letter on its own (saves that got it keep it).", {{"id", "string", "", true}, {"confirm", "boolean", "must be true", true}, {"mod", "string", ""}}, true, true},
    {"list_charts", "The open mod's treasure charts: name, description, place (the X), what the chest holds, the sea square, and what gives, finds or salvages each (used_by).",
     {{"mod", "string", ""}}},
    {"add_chart", "Adds a treasure chart: while Link owns it the MAP screen's chart list shows it with an X at its place; salvaging there opens its chest. Up to 8 per mod. A letter (encloses chart:<id>), a resident's talk (then give), a quest reward or the mod's code gives it; a quest step's find: chart:<id> or a scene's salvage trigger uses it.",
     {{"id", "string", "1..32 of a-z 0-9 - _ . (default: from the name)"}, {"name", "string", "up to 40 bytes: the chart list's caption", true},
      {"description", "string", "up to 200 bytes"}, {"place", "object", "{x, z}: the X on the Great Sea (world units)"},
      {"at", "string", "link or camera: the X where Link or the fly camera is at sea (instead of place)"},
      {"item", "string", "what its chest holds: item:<id> (one of the mod's items) or game:purple_rupee, game:orange_rupee, game:silver_rupee, game:heart_piece, game:heart_container (default: an orange rupee)"},
      {"mod", "string", ""}},
     true},
    {"set_chart", "Sets fields of a chart: name, description, place {x, z}, item (null removes description or item); at = link or camera moves the X there.",
     {{"id", "string", "the chart's id", true}, {"fields", "object", "{field: value}"}, {"at", "string", "link or camera"}, {"mod", "string", ""}}, true},
    {"remove_chart", "Removes a chart; refused while a letter, quest, talk or scene names it (the error lists them).",
     {{"id", "string", "", true}, {"confirm", "boolean", "must be true", true}, {"mod", "string", ""}}, true, true},
    {"list_items", "The open mod's items (kept in the Delivery Bag's extra pages) and what gives, takes, drops or needs each (used_by).", {{"mod", "string", ""}}},
    {"add_item", "Adds an item: when Link gets one, its name and description show with the get jingle; it is kept in the Delivery Bag. Up to 32 per mod. Actors give or take it (set_then), enemies and props drop it (set_drops), a letter or chart can hold it, an actor can need it (when item).",
     {{"id", "string", "1..32 of a-z 0-9 - _ . (default: from the name)"}, {"name", "string", "up to 40 bytes", true},
      {"description", "string", "up to 200 bytes, \\n allowed: its Info box", true}, {"icon", "string", "a PNG in the package (its path inside it, e.g. icons/key.png)"},
      {"icon_file", "string", "a PNG file copied into the package's icons folder instead (best 128 x 128 with transparency; up to 2048 x 2048)"},
      {"stack", "integer", "how many Link can carry, 1..254 (default 1)"},
      {"use", "object", "what using it on X, Y or R does: {action: show} (its Info box), {action: open_notebook}, {action: quest_step, quest, step}, {action: event} (the mod's code is told); without it, it can't go on a button"},
      {"held_model", "string", "a static model of the mod Link holds in his hand when using it (needs use, or hold_up)"},
      {"held_frames", "integer", "how long he holds it when using it, 1..300 frames at 30 a second (default 90; needs held_model)"},
      {"hold_up", "boolean", "true: getting the item (a give, a letter, a chest, a quest reward) plays the game's item-get pose: Link holds held_model up over his head with the get message (\"You got <name>!\" and the description); needs held_model"},
      {"mod", "string", ""}},
     true},
    {"set_item", "Sets fields of an item: name, description, icon, stack, use, held_model, held_frames, hold_up (null removes the optional ones); icon_file copies a new PNG in.",
     {{"id", "string", "the item's id", true}, {"fields", "object", "{field: value}"}, {"icon_file", "string", "a PNG file to copy in as its icon"}, {"mod", "string", ""}}, true},
    {"remove_item", "Removes an item; refused while an actor, drop, letter, chart, quest or scene names it (the error lists them).",
     {{"id", "string", "", true}, {"confirm", "boolean", "must be true", true}, {"mod", "string", ""}}, true, true},
};

// ---------------------------------------------------------------- JSON helpers
bool is_point(const Value& v) {
    if (v.type != Value::Object || v.object.size() != 3) return false;
    for (const char* k : {"x", "y", "z"})
        if (v.get(k).type != Value::Number || !std::isfinite(v.get(k).number)) return false;
    return true;
}
bool type_ok(const std::string& type, const Value& v) {
    if (type == "any") return true;
    if (type == "string") return v.type == Value::String;
    if (type == "boolean") return v.type == Value::Bool;
    if (type == "number") return v.type == Value::Number;
    if (type == "integer") return v.type == Value::Number && std::floor(v.number) == v.number && std::fabs(v.number) < 1e9;
    if (type == "point") return is_point(v);
    if (type == "object") return v.type == Value::Object;
    if (type == "object|null") return v.type == Value::Object || v.type == Value::Null;
    if (type == "array") return v.type == Value::Array;
    if (type == "array|null") return v.type == Value::Array || v.type == Value::Null;
    if (type == "number|null") return v.type == Value::Number || v.type == Value::Null;
    if (type == "string|null") return v.type == Value::String || v.type == Value::Null;
    return false;
}
bool check_params(const CommandInfo& c, const Value& p, std::string& why) {
    if (p.type != Value::Null && p.type != Value::Object) return why = "parameters must be an object", false;
    for (const auto& [key, value] : p.object) {
        auto it = std::find_if(c.params.begin(), c.params.end(), [&](const Param& q) { return q.name == key; });
        if (it == c.params.end()) return why = "unknown parameter \"" + key + "\" for " + c.name, false;
        if (!type_ok(it->type, value)) return why = "parameter \"" + key + "\" must be " + it->type, false;
    }
    for (const auto& q : c.params)
        if (q.required && !p.object.contains(q.name)) return why = "missing parameter \"" + q.name + "\"", false;
    return true;
}
std::string text(const Value& p, const char* key) { return p.get(key).string(); }
Value place_value(const Place& p) {
    Value v;
    v["valid"] = p.valid;
    if (!p.valid) return v;
    v["stage"] = p.stage;
    v["room"] = p.room;
    v["position"] = point_value(p.position);
    v["angle"] = p.angle;
    return v;
}
Value location_value(const Location& l) {
    Value v;
    v["id"] = l.id;
    v["label"] = l.label;
    v["group"] = l.group;
    v["stage"] = l.stage;
    v["room"] = l.room;
    if (l.point >= 0) v["point"] = l.point;
    if (l.point == kAtPosition) {
        v["position"] = point_value(l.position);
        v["angle"] = l.angle;
    }
    return v;
}
Value* find_by_id(Value& list, const std::string& id) {
    if (list.type != Value::Array) return nullptr;
    for (auto& v : list.array)
        if (v.get("id").type == Value::String && v.get("id").text == id) return &v;
    return nullptr;
}
const Value* find_by_id(const Value& list, const std::string& id) { return find_by_id(const_cast<Value&>(list), id); }

void number_text(std::string& out, double n) {
    if (std::floor(n) == n && std::fabs(n) < 1e15) {
        char b[32];
        snprintf(b, sizeof b, "%.0f", n);
        out += strcmp(b, "-0") == 0 ? "0" : b;
        return;
    }
    char b[40];
    for (int precision = 1; precision <= 17; precision++) {
        snprintf(b, sizeof b, "%.*g", precision, n);
        if (std::strtod(b, nullptr) == n) break;
    }
    out += b;
}
void pretty_into(std::string& out, const Value& v, int depth) {
    auto indent = [&](int d) { out += '\n'; out.append(size_t(d) * 2, ' '); };
    switch (v.type) {
    case Value::Null: out += "null"; break;
    case Value::Bool: out += v.boolean ? "true" : "false"; break;
    case Value::Number:
        if (!std::isfinite(v.number)) exception_report::raise("Nonfinite JSON number");
        number_text(out, v.number);
        break;
    case Value::String: out += mods::json::quote(v.text); break;
    case Value::Array: {
        if (v.array.empty()) { out += "[]"; break; }
        // short lists of scalars stay on one line ("lines": ["...", "..."] reads better)
        bool flat = v.array.size() <= 4;
        for (const auto& a : v.array) flat = flat && a.type != Value::Array && a.type != Value::Object;
        out += '[';
        for (size_t i = 0; i < v.array.size(); i++) {
            if (i) out += flat ? ", " : ",";
            if (!flat) indent(depth + 1);
            pretty_into(out, v.array[i], depth + 1);
        }
        if (!flat) indent(depth);
        out += ']';
        break;
    }
    case Value::Object: {
        if (v.object.empty()) { out += "{}"; break; }
        out += '{';
        bool first = true;
        for (const auto& [k, a] : v.object) {
            if (!first) out += ',';
            first = false;
            indent(depth + 1);
            out += mods::json::quote(k) + ": ";
            pretty_into(out, a, depth + 1);
        }
        indent(depth);
        out += '}';
        break;
    }
    }
}

bool read_point(const Value& v, WorldPoint& p) {
    if (!is_point(v)) return false;
    p = {float(v.get("x").number), float(v.get("y").number), float(v.get("z").number)};
    return true;
}

// Model sources must be files of the package itself (never outside it: a link could reach game files).
bool model_files(const std::string& dir, const Value& manifest, std::string& why) {
    if (dir.empty()) return true;
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(dir, ec);
    for (const auto& m : manifest.get("models").array) {
        const fs::path file = fs::weakly_canonical(fs::path(dir) / m.get("source").string(), ec);
        const auto rel = file.lexically_relative(root);
        if (ec || rel.empty() || rel.begin()->string() == "..") return why = "model " + m.get("id").string() + ": its source is outside the package", false;
        if (!fs::is_regular_file(file, ec)) return why = "model " + m.get("id").string() + ": " + m.get("source").string() + " is not in the package", false;
    }
    return true;
}

// What the change left out of the game: listed in the result (MCP) and said in its note.
void note_held(const std::vector<std::string>& held, Value& result) {
    if (held.empty()) return;
    result["not_active"].type = Value::Array;
    for (const auto& h : held) result["not_active"].array.push_back(Value(h));
    std::string text;
    for (const auto& h : held) text += (text.empty() ? "" : " ") + h + ".";
    const std::string note = text + " It isn't in the game until it stands where it fits; Check lists it.";
    result["note"] = result.get("note").string().empty() ? note : result.get("note").string() + " " + note;
}
// A world object (or island building) where it can't stand: with `held`, it is listed there ("house-a can't stand here:
// in the way: ...") and the change goes ahead (the editor keeps it as a preview, the game leaves it out until it is
// moved where it fits); without, it is the refusal. A query that can't be made is always a refusal.
bool world_placements(Host* host, const std::string& mod, const Value& before, const Value& next, bool all, std::string& why,
                      std::vector<std::string>* held = nullptr) {
    if (!host) return true;
    std::vector<ManifestActor> actors;
    if (!story_notebook::parse_actors(next, actors, why)) return false;
    for (const auto& actor : actors) {
        if (!actor.world_object()) continue;
        const auto* old = find_by_id(before.get("actors"), actor.id);
        const auto* now = find_by_id(next.get("actors"), actor.id);
        bool changed = !old || !now || !(*old == *now);
        for (const auto& model : {actor.model, actor.collision_mesh}) {
            if (model.empty()) continue;
            const auto* previous = find_by_id(before.get("models"), model);
            const auto* current = find_by_id(next.get("models"), model);
            changed = changed || !previous || !current || !(*previous == *current);
        }
        if (!all && !changed) continue;
        // an island building that is still being made from the game files: the game checks its place when
        // it is ready (mod_actor::declare runs the same query), and the placement appears then
        if (actor.game_piece() && host->piece_state(mod, actor.piece) != "ready") continue;
        Value query;
        if (!host->world_object_query(mod, actor, false, query, why)) {
            // a body of water from the water tool whose models are still being prepared: check_water cleared its
            // place already, and the game checks it again when they are ready (mod_actor::declare)
            if (actor.water_body()) {
                why.clear();
                continue;
            }
            why = "world object " + actor.id + ": " + (why.empty() ? "placement coverage or collision is unavailable" : why);
            return false;
        }
        if (query.get("clear").type != Value::Bool || !query.get("clear").boolean) {
            const auto& conflicts = query.get("conflicts").array;
            // every reason, with the game's own names (MCP errors carry the whole list; the editor shows the first)
            std::string list;
            for (const auto& c : conflicts) list += (list.empty() ? "" : "; ") + conflict_words(c);
            // a single small thing: how far to move
            if (conflicts.size() == 1 && conflicts[0].get("move").number > 0) list += " (move it about " + std::to_string(int(conflicts[0].get("move").number)) + " units away)";
            const std::string problem = actor.id + " can't stand here: " + (list.empty() ? std::string("placement could not be cleared") : "in the way: " + list);
            if (held) {
                held->push_back(problem);
                continue;
            }
            why = "world object " + problem;
            return false;
        }
    }
    return true;
}

const std::map<std::string, std::string>& stage_names() {
    static const std::map<std::string, std::string> names = {
        {"sea", "Great Sea"}, {"LinkRM", "Link's House"}, {"MajyuE", "Forsaken Fortress"}, {"M_NewD2", "Dragon Roost Cavern"},
        {"kindan", "Forbidden Woods"}, {"Siren", "Tower of the Gods"}, {"Asoko", "Tetra's Ship"}, {"M_Dai", "Earth Temple"},
        {"kaze", "Wind Temple"}, {"GanonK", "Ganon's Tower"}, {"Hyroom", "Hyrule Castle"}, {"Obombh", "Bomb-Master Cannon's house"},
    };
    return names;
}

}  // namespace

std::vector<Problem> placement_problems(Host* host, const std::string& mod, const Value& manifest) {
    std::vector<Problem> out;
    std::vector<std::string> held;
    std::string why;
    std::vector<ManifestActor> actors;
    if (!story_notebook::parse_actors(manifest, actors, why)) return out;  // package_problems names that
    if (!world_placements(host, mod, manifest, manifest, true, why, &held)) {
        out.push_back({why, "package", "", "", ""});
        return out;
    }
    for (const auto& h : held) out.push_back({h, "actor", h.substr(0, h.find(" can't stand here")), "position", ""});
    return out;
}

const std::vector<CommandInfo>& command_list() { return kCommands; }
const CommandInfo* find_command(const std::string& name) {
    for (const auto& c : kCommands)
        if (c.name == name) return &c;
    return nullptr;
}

Value point_value(const WorldPoint& p) {
    Value v;
    v["x"] = double(p.x);
    v["y"] = double(p.y);
    v["z"] = double(p.z);
    return v;
}

std::string pretty(const Value& v) {
    std::string out;
    pretty_into(out, v, 0);
    return out + '\n';
}

// ---------------------------------------------------------------- new mods
bool package_id_valid(const std::string& id) {
    return !id.empty() && id.size() <= 64 && id[0] != '.' && std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    });
}

bool new_package_manifest(const Value& params, Value& out, std::string& why) {
    const std::string id = params.get("id").string(), name = params.get("name").string();
    const std::string author = params.get("author").string(), description = params.get("description").string();
    if (!package_id_valid(id))
        return why = "the id \"" + id + "\" is not a valid mod id: use 1 to 64 of a-z, 0-9, '-', '_' and '.' (not starting with '.')", false;
    if (id == story_notebook::kNotebookPackage) return why = "the id \"" + id + "\" belongs to the Story Notebook", false;
    auto has_nul = [](const std::string& s) { return s.find('\0') != std::string::npos; };
    if (name.find_first_not_of(" \t") == std::string::npos) return why = "the new mod needs a name", false;
    if (name.size() > 128 || has_nul(name)) return why = "the name is longer than 128 characters", false;
    if (author.size() > 256 || has_nul(author)) return why = "the author is longer than 256 characters", false;
    if (description.size() > 8192 || has_nul(description)) return why = "the description is longer than 8192 characters", false;
    Value m;
    m["format_version"] = 1;
    m["id"] = id;
    m["name"] = name;
    m["version"] = "0.1.0";
    if (!author.empty()) m["author"] = author;
    if (!description.empty()) m["description"] = description;
    m["game_id"] = "wwhd";
    m["minimum_manager_version"] = "1.5.0";
    m["kind"] = "content";
    if (params.get("stages").boolean) {  // a mod of stages only (docs/mod-stages.md): no story content, no Story Notebook
        m["stages"].type = Value::Array;
        out = m;
        return true;
    }
    // the story content (actors, quests, letters...) runs on the Story Notebook package
    Value dep;
    dep["id"] = story_notebook::kNotebookPackage;
    dep["minimum_version"] = "0.0.0";
    m["dependencies"].type = Value::Array;
    m["dependencies"].array.push_back(dep);
    m["actors"].type = Value::Array;  // a story section, empty: the editor fills it
    out = m;
    return true;
}

bool write_package_folder(const std::string& parent, const Value& manifest, std::string& dir, std::string& why) {
    const std::string id = manifest.get("id").string();
    if (!package_id_valid(id)) return why = "the manifest has no valid id", false;
    std::error_code ec;
    const fs::path root(parent), target = root / id;
    if (parent.empty() || !fs::is_directory(root, ec) || fs::is_symlink(root, ec)) return why = "the mods folder is not there", false;
    if (fs::exists(fs::symlink_status(target, ec))) return why = "a folder \"" + id + "\" exists in the mods folder already", false;
    // the folder first: creating it is the claim (false when someone else made it in the meantime)
    if (!fs::create_directory(target, ec) || ec) return why = "cannot create the folder \"" + id + "\" in the mods folder", false;
    const fs::path tmp = target / "manifest.json.editor-tmp", file = target / "manifest.json";
    bool written = false;
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << pretty(manifest);
        f.close();
        written = f.good();
    }
    if (written) {
        fs::rename(tmp, file, ec);
        written = !ec;
    }
    if (!written) {  // only what this call made: the file and the (still otherwise empty) folder
        fs::remove(tmp, ec);
        fs::remove(file, ec);
        fs::remove(target, ec);
        return why = "cannot write the new package's manifest.json", false;
    }
    dir = target.string();
    return true;
}

// ---------------------------------------------------------------- validation
bool validate_package(const std::string& mod, const Value& manifest, const std::vector<std::pair<std::string, Value>>& all,
                      std::string& why) {
    if (manifest.type != Value::Object) return why = "the manifest is not an object", false;
    std::vector<story_notebook::ManifestModel> models;
    std::vector<ManifestActor> actors;
    std::vector<story_notebook::ManifestScene> scenes;
    std::vector<story_notebook::ManifestQuest> quests;
    std::vector<story_notebook::ManifestItem> items;
    std::vector<story_notebook::ManifestChart> charts;
    std::vector<story_notebook::ManifestLetter> letters;
    if (!story_notebook::parse_models(manifest, models, why) || !story_notebook::parse_actors(manifest, actors, why) ||
        !story_notebook::parse_scenes(manifest, scenes, why) || !story_notebook::parse_items(manifest, items, why) ||
        !story_notebook::parse_quests(manifest, quests, why) || !story_notebook::parse_charts(manifest, charts, why) ||
        !story_notebook::parse_letters(manifest, letters, why))
        return false;
    // the package's stages (mod_stages_manifest.h). A room nothing leads to yet is kept while editing (a new room
    // before its door): Check and export list it (package_problems), the game does not build that stage
    std::vector<mod_stages::Stage> stages;
    if (!mod_stages::parse(manifest, stages, why, false)) return why = "stages: " + why, false;
    std::vector<std::pair<std::string, Value>> world;
    for (const auto& [id, m] : all)
        if (id != mod && (m.get("actors").type != Value::Null || m.get("scenes").type != Value::Null)) world.push_back({id, m});
    world.push_back({mod, manifest});
    return story_notebook::world_manifest::validate_world_packages(world, why);
}

// ---------------------------------------------------------------- the sea and locations
std::string conflict_words(const Value& c) {
    std::string one = c.get("message").string("placement conflicts with protected space");
    const std::string native = c.get("native").string();
    if (!native.empty() && one.find(native) == std::string::npos) one += " [" + native + "]";
    const Value& at = c.get("at");
    if (at.type == Value::Object)
        one += " at x " + std::to_string(int(std::lround(at.get("x").number))) + ", y " + std::to_string(int(std::lround(at.get("y").number))) + ", z " +
               std::to_string(int(std::lround(at.get("z").number)));
    return one;
}
int sea_room(float x, float z) {
    if (!(std::fabs(x) < 350000 && std::fabs(z) < 350000)) return 0;
    const int col = int(std::floor((double(x) + 350000) / 100000)), row = int(std::floor((double(z) + 350000) / 100000));
    return row * 7 + col + 1;
}
WorldPoint sea_room_centre(int room) {
    if (room < 1 || room > 49) return {};
    const int col = (room - 1) % 7, row = (room - 1) / 7;
    return {float(-350000 + col * 100000 + 50000), 0, float(-350000 + row * 100000 + 50000)};
}
const char* island_name(int room) {
    static const char* const names[49] = {
        "Forsaken Fortress", "Star Island", "Northern Fairy Island", "Gale Isle", "Crescent Moon Island", "Seven-Star Isles",
        "Overlook Island", "Four-Eye Reef", "Mother and Child Isles", "Spectacle Island", "Windfall Island", "Pawprint Isle",
        "Dragon Roost Island", "Flight Control Platform", "Western Fairy Island", "Rock Spire Isle", "Tingle Island",
        "Northern Triangle Island", "Eastern Fairy Island", "Fire Mountain", "Star Belt Archipelago", "Three-Eye Reef",
        "Greatfish Isle", "Cyclops Reef", "Six-Eye Reef", "Tower of the Gods", "Eastern Triangle Island", "Thorned Fairy Island",
        "Needle Rock Isle", "Islet of Steel", "Stone Watcher Island", "Southern Triangle Island", "Private Oasis", "Bomb Island",
        "Bird's Peak Rock", "Diamond Steppe Island", "Five-Eye Reef", "Shark Island", "Southern Fairy Island", "Ice Ring Isle",
        "Forest Haven", "Cliff Plateau Isles", "Horseshoe Island", "Outset Island", "Headstone Island", "Two-Eye Reef",
        "Angular Isles", "Boating Course", "Five-Star Isles"};
    return room >= 1 && room <= 49 ? names[room - 1] : nullptr;
}

std::vector<Location> build_locations(const std::vector<std::pair<std::string, std::vector<int>>>& stage_rooms,
                                      const std::vector<std::pair<std::string, Value>>& packages, const std::vector<Host::ModStage>& mod_stages) {
    std::vector<Location> out;
    for (int room = 1; room <= 49; room++) {
        Location l;
        l.id = "island:" + std::to_string(room);
        l.label = std::to_string(room) + "  " + island_name(room);
        l.group = "Islands";
        l.stage = "sea";
        l.room = room;
        l.position = sea_room_centre(room);  // the game's fallback when the room has no spawn point
        out.push_back(l);
    }
    for (const auto& [stage, rooms] : stage_rooms) {
        if (stage == "sea") continue;
        auto named = stage_names().find(stage);
        for (int room : rooms) {
            Location l;
            l.id = "room:" + stage + ":" + std::to_string(room);
            l.label = (named == stage_names().end() ? stage : named->second + " (" + stage + ")") + "  room " + std::to_string(room);
            l.group = "Rooms";
            l.stage = stage;
            l.room = room;
            out.push_back(l);
        }
    }
    // the packages' stages, by the package's own id (the game name is the port's)
    for (const auto& ms : mod_stages)
        for (int room : ms.rooms) {
            Location l;
            l.id = "stage:" + ms.mod + "/" + ms.id + ":" + std::to_string(room);
            l.label = ms.id + " (" + ms.mod + ")  room " + std::to_string(room);
            l.group = "Mod stages";
            l.stage = ms.name;
            l.room = room;
            out.push_back(l);
        }
    for (const auto& [mod, m] : packages) {
        std::vector<ManifestActor> actors;
        std::string why;
        if (story_notebook::parse_actors(m, actors, why))
            for (const auto& a : actors) {
                Location l;
                l.id = "actor:" + mod + ":" + a.id;
                l.label = a.id + "  (" + a.stage + " room " + std::to_string(a.room) + ")";
                l.group = "Quest";
                l.stage = a.stage;
                l.room = int(a.room);
                l.point = kAtPosition;
                // two metres in front of it, facing it
                const double yaw = double(a.rotation.y) * 3.14159265358979323846 / 180;
                l.position = {float(a.position.x + 200 * std::sin(yaw)), a.position.y + 20, float(a.position.z + 200 * std::cos(yaw))};
                l.angle = int(std::lround((double(a.rotation.y) + 180) * 32768 / 180)) & 0xFFFF;
                out.push_back(l);
            }
        for (const auto& q : m.get("quests").array)
            for (const auto& s : q.get("steps").array) {
                const auto& place = s.get("place");
                story_notebook::QuestDestination destination;std::string place_why;
                if(!story_notebook::parse_quest_destination(place,destination,place_why))continue;
                Location l;
                const int n = int(s.get("n").number);
                l.id = "step:" + mod + ":" + q.get("id").string() + ":" + std::to_string(n);
                l.label = q.get("id").string() + " step " + std::to_string(n) + "  (" + destination.stage + " room " + std::to_string(destination.room) + ")";
                l.group = "Quest";
                l.stage = destination.stage;
                l.room = int(destination.room);
                l.point = destination.point;
                l.position = destination.position;
                out.push_back(l);
            }
    }
    return out;
}

// ---------------------------------------------------------------- story records
std::vector<Record> mod_records(const std::string& mod, const Value& manifest) {
    using namespace story_mail;
    std::vector<Record> out;
    auto add = [&](uint8_t kind, uint16_t key) {
        Record r{kind, key};
        if (std::find(out.begin(), out.end(), r) == out.end()) out.push_back(r);
    };
    auto flag = [&](const std::string& id) {
        if (!id.empty()) add(kRecordActorFlag, letter_key(mod, "actor-flag:" + id));
    };
    auto actions = [&](const Value& then) {
        for (const auto& a : then.array)
            if (a.get("action").string() == "flag") flag(a.get("id").string());
    };
    for (const auto& a : manifest.get("actors").array) {
        flag(a.get("flag").string());
        actions(a.get("then"));
        for (const auto& talk : a.get("talks").array) actions(talk.get("then"));
    }
    for (const auto& s : manifest.get("scenes").array)
        for (const auto& st : s.get("steps").array) actions(st.get("then"));
    for (const auto& q : manifest.get("quests").array) {
        const uint16_t key = letter_key(mod, q.get("id").string());  // story_notebook_list.h quest_key
        for (uint8_t kind : {kRecordQuest, kRecordTracked, kRecordCarried, kRecordQuestFlags, kRecordSailLine, kRecordQuestOrder,
                             kRecordQuestActionReward, story_items::kRecordQuestRewardItem}) add(kind, key);
        if (q.get("letter").get("id").type == Value::String) {
            add(kRecordLetter, letter_key(mod, q.get("letter").get("id").text));
            add(kRecordLetterEnclosures, letter_key(mod, q.get("letter").get("id").text));
        }
    }
    for (const auto& l : manifest.get("letters").array)
        for (uint8_t kind : {kRecordLetter, kRecordLetterEnclosures, kRecordLetterDayLow, kRecordLetterDayHigh}) add(kind, letter_key(mod, l.get("id").string()));
    // the mod's charts (owned, opened, finished: story_chart.h) and items (how many, their place in the bag,
    // the game number lent to them, their buttons: story_items_core.h)
    for (const auto& c : manifest.get("charts").array) add(kRecordMarker, letter_key(mod, c.get("id").string()));
    for (const auto& it : manifest.get("items").array)
        for (uint8_t kind : {story_items::kRecordItemCount, story_items::kRecordItemPlace, story_items::kRecordItemNumber, story_items::kRecordItemButtons})
            add(kind, story_items::item_key(mod, it.get("id").string()));
    return out;
}

unsigned reset_records(story_mail::Book& book, const std::vector<Record>& own, const std::vector<Record>& shared, unsigned& skipped) {
    unsigned erased = 0;
    skipped = 0;
    for (const auto& r : own) {
        if (std::find(shared.begin(), shared.end(), r) != shared.end()) {
            skipped += book.has(r.kind, r.key);
            continue;
        }
        erased += book.erase(r.kind, r.key);
    }
    return erased;
}

// ---------------------------------------------------------------- session
void Session::add_package(const std::string& mod, const std::string& name, const std::string& dir, const Value& manifest) {
    if (find(mod)) return;
    packages_.push_back({mod, name, dir, manifest, manifest, manifest});
    // the editor's own sidecar in the package (quest graph places); the game never reads it
    if (!dir.empty()) {
        std::ifstream f(fs::path(dir) / "editor" / "quest-layout.json", std::ios::binary);
        if (f) {
            std::stringstream text;
            text << f.rdbuf();
            try {
                packages_.back().layout = packages_.back().layout_saved = mods::json::parse(text.str());
            } catch (const std::exception&) {
            }
        }
    }
    // open the first mod that places actors (a quest-only package like a shared notebook comes first otherwise)
    const Package* current = find(open_);
    if (open_.empty() || (current && current->manifest.get("actors").array.empty() && !manifest.get("actors").array.empty())) open_ = mod;
}
const Session::Package* Session::find(const std::string& mod) const {
    for (const auto& p : packages_)
        if (p.mod == mod) return &p;
    return nullptr;
}
Session::Package* Session::package(const Value& params, std::string& why) {
    const std::string mod = params.get("mod").type == Value::String ? params.get("mod").text : open_;
    if (mod.empty()) return why = "no mod is open (none of the active mods places actors or declares quests)", nullptr;
    auto* p = const_cast<Package*>(find(mod));
    if (!p) why = "no active mod \"" + mod + "\"";
    return p;
}
const Value* Session::manifest(const std::string& mod) const {
    auto* p = find(mod);
    return p ? &p->manifest : nullptr;
}
std::string Session::package_dir(const std::string& mod) const {
    auto* p = find(mod);
    return p ? p->dir : std::string();
}
bool Session::dirty(const std::string& mod) const {
    auto* p = find(mod);
    return p && (!(p->manifest == p->saved) || !(p->layout == p->layout_saved));
}
std::vector<std::string> Session::mods() const {
    std::vector<std::string> out;
    for (const auto& p : packages_) out.push_back(p.mod);
    return out;
}
std::vector<std::pair<std::string, Value>> Session::all_manifests() const {
    std::vector<std::pair<std::string, Value>> out;
    for (const auto& p : packages_) out.push_back({p.mod, p.manifest});
    return out;
}

Value Session::run(const std::string& command, const Value& params, bool log, const std::string& source) {
    Value out;
    std::string why;
    bool ok = false;
    try {
        const CommandInfo* info = find_command(command);
        if (!info) why = "unknown command \"" + command + "\"";
        else if (check_params(*info, params, why)) {
            if (info->destructive && !params.get("confirm").boolean) why = command + " needs \"confirm\": true";
            else {
                Value result = dispatch(command, params, why, ok);
                if (ok) out["result"] = result;
            }
        }
    } catch (const std::exception& e) {
        ok = false;
        why = e.what();
    }
    out["ok"] = ok;
    if (!ok) out["error"] = why.empty() ? std::string("failed") : why;
    if (!log) return out;
    note(command, params, out, source);
    return out;
}

// read_state: the open mod's quest steps and flags as the loaded Quest Log holds them (the game's last look)
Value Session::progress(const std::string& mod, const Value& manifest) {
    using namespace story_mail;
    Value out;
    std::vector<Record> want;
    std::vector<std::string> flag_ids;
    auto flag = [&](const std::string& id) {
        if (id.empty() || std::find(flag_ids.begin(), flag_ids.end(), id) != flag_ids.end()) return;
        flag_ids.push_back(id);
        want.push_back({kRecordActorFlag, letter_key(mod, "actor-flag:" + id)});
    };
    auto actions = [&](const Value& then) {
        for (const auto& a : then.array)
            if (a.get("action").string() == "flag") flag(a.get("id").string());
    };
    for (const auto& a : manifest.get("actors").array) {
        flag(a.get("flag").string());
        actions(a.get("then"));
        for (const auto& talk : a.get("talks").array) actions(talk.get("then"));
    }
    for (const auto& sc : manifest.get("scenes").array)
        for (const auto& st : sc.get("steps").array) actions(st.get("then"));
    const size_t quests_at = want.size();
    for (const auto& q : manifest.get("quests").array) {
        const uint16_t key = letter_key(mod, q.get("id").string());
        want.push_back({kRecordQuest, key});
        want.push_back({kRecordQuestFlags, key});
    }
    std::vector<int> values;
    std::string unknown;
    if (want.empty() || !host_ || !host_->read_records(want, values, unknown) || values.size() != want.size()) {
        out["known"] = false;
        return out;
    }
    out["known"] = true;
    Value flags;
    flags.type = Value::Object;
    for (size_t i = 0; i < flag_ids.size(); i++) flags[flag_ids[i]] = values[i] < 0 ? Value(false) : Value(values[i]);
    out["flags"] = flags;
    Value quests;
    quests.type = Value::Array;
    size_t i = quests_at;
    for (const auto& q : manifest.get("quests").array) {
        Value v;
        v["id"] = q.get("id").string();
        const int step = values[i++], bits = values[i++];
        if (step < 0 || step == 0) v["step"] = "not started";
        else if (step == 255) v["step"] = "done";
        else v["step"] = step;
        Value set;
        set.type = Value::Array;
        for (int b = 0; b < 8; b++)
            if (bits > 0 && (bits >> b) & 1) set.array.push_back(b + 1);
        v["flags"] = set;
        quests.array.push_back(v);
    }
    out["quests"] = quests;
    return out;
}

void Session::note(const std::string& command, const Value& params, const Value& result, const std::string& source) {
    LogEntry e{++seq_, command, params, result, source};
    log_.push_back(std::move(e));
    while (log_.size() > kLogSize) log_.pop_front();
}

std::string Session::run_json(const std::string& request) {
    Value out;
    try {
        const Value r = mods::json::parse(request);
        if (r.get("command").type != Value::String) {
            out["ok"] = false;
            out["error"] = "a request is {\"command\": \"...\", \"params\": {...}}";
        } else
            out = run(r.get("command").text, r.get("params"));
    } catch (const std::exception& e) {
        out["ok"] = false;
        out["error"] = std::string("invalid request: ") + e.what();
    }
    return mods::json::dump(out);
}

namespace {
// a result's note: one more sentence after what it says already
void add_note(Value& result, const std::string& text) {
    const std::string had = result.get("note").type == Value::String ? result.get("note").text : std::string();
    result["note"] = had.empty() ? text : had + " " + text;
}
}  // namespace

void Session::apply_live(const Package& p, const Value& before, Value& result) {
    std::vector<std::string> changed;
    for (const auto& a : p.manifest.get("actors").array) {
        const Value* old = find_by_id(before.get("actors"), a.get("id").string());
        bool differs = !old || !(*old == a);
        // an actor of a changed model shows the new model only when created again
        if (!differs && a.get("model").type == Value::String) {
            const Value *now = find_by_id(p.manifest.get("models"), a.get("model").text), *then = find_by_id(before.get("models"), a.get("model").text);
            differs = !now || !then || !(*now == *then);
        }
        if (differs) changed.push_back(a.get("id").string());
    }
    result["changed_actors"].type = Value::Array;
    for (const auto& id : changed) result["changed_actors"].array.push_back(Value(id));
    std::vector<ManifestActor> actors;
    std::string why, note;
    if (!story_notebook::parse_actors(p.manifest, actors, why)) {
        result["live"] = false;
        result["note"] = why;
        return;
    }
    if (!host_) {
        result["live"] = false;
        return;
    }
    const bool live = host_->apply_actors(p.mod, actors, changed, note, why);
    result["live"] = live;
    if (!live) result["note"] = why;
    else if (!note.empty()) result["note"] = note;
}

void Session::apply_paths(const Package& p, Value& result) {
    std::vector<story_room_path::Path> paths;
    std::string why;
    if (!story_notebook::parse_room_paths(p.manifest, paths, why) || !host_) {
        result["paths_live"] = false;
        return;
    }
    const bool live = host_->apply_paths(p.mod, paths, why);
    result["paths_live"] = live;
    if (!live) add_note(result, "Path changes take effect when the game starts again (save the package first)" + (why.empty() ? std::string(".") : ": " + why + "."));
}

bool Session::drag_preview(const std::string& mod, const Value& poses, std::vector<std::string>& later, std::string& why,
                           std::vector<std::string>* in_place) {
    later.clear();
    if (in_place) in_place->clear();
    const Package* p = find(mod);
    if (!p) return why = "no active mod \"" + mod + "\"", false;
    if (!host_) return why = "the game is not running", false;
    if (poses.type != Value::Array || poses.array.empty()) return why = "nothing is dragged", false;
    Value shown = p->manifest;  // a copy: the package itself never changes here
    Value placed;               // the poses with every place filled in (a pose without one: the package's)
    placed.type = Value::Array;
    for (const auto& pose : poses.array) {
        const std::string id = pose.get("id").string();
        Value* a = find_by_id(shown["actors"], id);
        if (!a) return why = "no placed actor \"" + id + "\" in " + mod, false;
        for (const char* field : {"position", "rotation"})
            if (pose.get(field).type == Value::Object) (*a)[field] = pose.get(field);
        Value full;
        full["id"] = id;
        for (const char* field : {"position", "rotation"})
            if (a->get(field).type != Value::Null) full[field] = a->get(field);
        placed.array.push_back(full);
    }
    // in place first: the actors that exist in the game move where they are (no new actor, every step)
    std::vector<std::string> moved;
    std::string ignore;
    host_->pose_actors(mod, placed, moved, ignore);
    if (in_place) *in_place = moved;
    std::vector<std::string> changed;
    for (const auto& pose : placed.array)
        if (std::find(moved.begin(), moved.end(), pose.get("id").string()) == moved.end()) changed.push_back(pose.get("id").string());
    if (changed.empty()) return true;
    // the others (not created yet, or kinds that can't move in place): created again at the new place
    std::vector<ManifestActor> actors;
    if (!story_notebook::parse_actors(shown, actors, why)) return false;
    return host_->preview_actors(mod, actors, changed, later, why);
}

bool Session::change(Package& p, const std::string& label, const std::function<bool(Value&, std::string&)>& edit, Value& result,
                     std::string& why) {
    Value next = p.manifest;
    if (!edit(next, why)) return false;
    if (next == p.manifest) {
        result["changed"] = false;
        return true;
    }
    auto all = all_manifests();
    std::vector<std::string> held;
    if (!validate_package(p.mod, next, all, why) || !model_files(p.dir, next, why) || !world_placements(host_, p.mod, p.manifest, next, false, why, &held)) return false;
    const bool actors_changed = !(next.get("actors") == p.manifest.get("actors")) || !(next.get("models") == p.manifest.get("models"));
    const bool story_changed = story_reload::story_changed(p.manifest, next);
    const bool paths_changed = !(next.get("paths") == p.manifest.get("paths"));
    const bool scenes_changed = !(next.get("scenes") == p.manifest.get("scenes"));  // read when the game starts
    undo_.push_back({p.mod, label, p.manifest, next});
    if (undo_.size() > kUndoSize) undo_.erase(undo_.begin());
    redo_.clear();
    const Value before = std::move(p.manifest);
    p.manifest = std::move(next);
    result["changed"] = true;
    if (paths_changed) apply_paths(p, result);  // before the actors: an actor on a new path finds it
    if (actors_changed) apply_live(p, before, result);
    note_held(held, result);
    if (story_changed) apply_story(p, result);
    if (scenes_changed) {
        result["live"] = false;
        add_note(result, "Scene changes take effect when the game starts again (save the package first).");
    }
    return true;
}

void Session::apply_story(const Package& p, Value& result) {
    std::string note, why;
    const bool live = host_ && host_->apply_story(p.mod, p.name, p.dir, p.started, p.manifest, note, why);
    result["story_live"] = live;
    if (!live) {
        result["live"] = false;
        add_note(result, "Quest, letter, chart and item changes take effect when the game starts again (save the package first)" +
                             (why.empty() ? std::string(".") : ": " + why + "."));
    } else if (!note.empty())
        add_note(result, note);
}

bool Session::restore(const std::string& mod, const Value& manifest, Value& result, std::string& why) {
    auto* p = const_cast<Package*>(find(mod));
    if (!p) return why = "the mod of this step is gone", false;
    std::vector<std::string> held;
    if (!world_placements(host_, mod, p->manifest, manifest, false, why, &held)) return false;
    const bool actors_changed = !(manifest.get("actors") == p->manifest.get("actors")) || !(manifest.get("models") == p->manifest.get("models"));
    const bool paths_changed = !(manifest.get("paths") == p->manifest.get("paths"));
    const bool story_changed = story_reload::story_changed(p->manifest, manifest);
    const Value before = p->manifest;
    p->manifest = manifest;
    result["mod"] = mod;
    if (paths_changed) apply_paths(*p, result);
    if (actors_changed) apply_live(*p, before, result);
    note_held(held, result);
    if (story_changed) apply_story(*p, result);  // undo and redo: the same live update
    return true;
}

std::vector<Location> Session::locations() const {
    if (stage_rooms_.empty() && host_) stage_rooms_ = host_->stage_rooms();
    std::vector<std::pair<std::string, Value>> open;
    if (auto* p = find(open_)) open.push_back({p->mod, p->manifest});
    return build_locations(stage_rooms_, open, host_ ? host_->mod_stages() : std::vector<Host::ModStage>{});
}

bool Session::warp_to(const Location& to, std::string& why) {
    if (!host_) return why = "the game is not running", false;
    if (host_->busy(why)) return false;
    const Place from = host_->link();
    if (!host_->warp(to, why)) return false;
    if (from.valid) last_place_ = from;
    return true;
}

Value Session::dispatch(const std::string& command, const Value& p, std::string& why, bool& ok) {
    Value r;
    ok = false;
    auto actor_edit = [&](const std::string& label, const std::function<bool(Value&, std::string&)>& set) -> Value {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const std::string id = text(p, "id");
        ok = change(*pkg, label, [&](Value& m, std::string& w) {
            Value* a = find_by_id(m["actors"], id);
            if (!a) return w = "no placed actor \"" + id + "\" in " + pkg->mod, false;
            return set(*a, w);
        }, r, why);
        if (ok) r["id"] = id;
        return r;
    };
    if (command == "help") {
        Value list;
        list.type = Value::Array;
        for (const auto& c : kCommands) {
            if (p.get("command").type == Value::String && c.name != p.get("command").text) continue;
            Value v;
            v["name"] = c.name;
            v["help"] = c.help;
            v["changes_package"] = c.changes_package;
            v["destructive"] = c.destructive;
            v["params"].type = Value::Array;
            for (const auto& q : c.params) {
                Value pv;
                pv["name"] = q.name;
                pv["type"] = q.type;
                pv["required"] = q.required;
                if (!q.help.empty()) pv["help"] = q.help;
                v["params"].array.push_back(pv);
            }
            list.array.push_back(v);
        }
        if (list.array.empty()) return why = "unknown command", r;
        ok = true;
        return list;
    }
    if (command == "list_mods") {
        r.type = Value::Array;
        for (const auto& pk : packages_) {
            Value v;
            v["id"] = pk.mod;
            v["name"] = pk.name;
            v["open"] = pk.mod == open_;
            v["dirty"] = !(pk.manifest == pk.saved);
            for (const char* k : {"actors", "models", "scenes", "quests", "letters", "charts", "items"}) v[k] = int(pk.manifest.get(k).array.size());
            r.array.push_back(v);
        }
        ok = true;
        return r;
    }
    if (command == "open_mod") {
        if (!find(text(p, "mod"))) return why = "no active mod \"" + text(p, "mod") + "\"", r;
        open_ = text(p, "mod");
        r["mod"] = open_;
        ok = true;
        return r;
    }
    if (command == "new_mod") {
        Value m;
        if (!new_package_manifest(p, m, why)) return r;
        const std::string id = m.get("id").text;
        if (find(id)) return why = "a mod with the id \"" + id + "\" exists already", r;
        std::string problem;
        if (!validate_package(id, m, all_manifests(), problem)) return why = "the new manifest is not valid: " + problem, r;
        if (!host_) return why = "the game is not running", r;
        std::string dir;
        if (!host_->create_package(m, dir, why)) return r;
        add_package(id, m.get("name").text, dir, m);
        open_ = id;
        r["mod"] = id;
        r["name"] = m.get("name");
        r["dir"] = dir;
        r["open"] = true;
        r["note"] = "Created " + id + ", turned on in the current mod profile and opened. Add actors, quests or story items, then save.";
        ok = true;
        return r;
    }
    if (command == "list_actors" || command == "list_models" || command == "list_scenes" || command == "list_quests") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const Value& m = pkg->manifest;
        r.type = Value::Array;
        if (command == "list_actors")
            for (const auto& a : m.get("actors").array) {
                Value v;
                v["id"] = a.get("id");
                v["kind"] = a.get("kind").string() == "mod" ? "mod" : "game";
                v["actor"] = a.get("kind").string() == "mod" ? Value(std::string("mod")) : a.get("actor");
                if (a.get("model").type == Value::String) v["model"] = a.get("model");
                v["stage"] = a.get("stage");
                v["room"] = a.get("room");
                v["position"] = a.get("position");
                r.array.push_back(v);
            }
        else if (command == "list_models")
            for (const auto& md : m.get("models").array) {
                Value v = md;
                v["used_by"].type = Value::Array;
                for (const auto& a : m.get("actors").array)
                    if (a.get("model") == md.get("id")) v["used_by"].array.push_back(a.get("id"));
                r.array.push_back(v);
            }
        else if (command == "list_scenes")
            for (const auto& s : m.get("scenes").array) {
                Value v;
                v["id"] = s.get("id");
                v["trigger"] = s.get("trigger");
                v["steps"] = int(s.get("steps").array.size());
                r.array.push_back(v);
            }
        else
            for (const auto& q : m.get("quests").array) {
                Value v;
                v["id"] = q.get("id");
                v["title"] = q.get("title");
                v["steps"].type = Value::Array;
                for (const auto& s : q.get("steps").array) {
                    Value sv;
                    for (const char* k : {"n", "text", "where", "place"})
                        if (s.get(k).type != Value::Null) sv[k] = s.get(k);
                    v["steps"].array.push_back(sv);
                }
                r.array.push_back(v);
            }
        ok = true;
        return r;
    }
    if (command == "get_actor" || command == "get_scene") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const Value* v = find_by_id(pkg->manifest.get(command == "get_actor" ? "actors" : "scenes"), text(p, "id"));
        if (!v) return why = "no " + std::string(command == "get_actor" ? "placed actor" : "scene") + " \"" + text(p, "id") + "\"", r;
        ok = true;
        return *v;
    }
    if (command == "set_actor") {
        static const std::set<std::string> allowed = {"actor", "kind", "model", "animation", "look_at", "path", "collision", "attention",
                                                      "stage", "room", "position", "rotation", "scale", "params", "parameters", "flag",
                                                      "when", "lines", "then", "talks", "defeated_flag", "broken_flag", "drops",
                                                      "native_drops", "combat", "carry", "switch_flag", "switch", "defeat_switch", "texture", "variant", "behaviour", "buoyancy", "drift", "anchor", "grapple_points", "distant", "shoreline", "chart_marker", "arrival"};
        const std::string property = text(p, "property");
        if (!allowed.contains(property)) return why = "\"" + property + "\" is not an actor property the editor sets", r;
        return actor_edit("Set " + property + " of " + text(p, "id"), [&](Value& a, std::string&) {
            if (p.get("value").type == Value::Null) a.object.erase(property);
            else a[property] = p.get("value");
            return true;
        });
    }
    if (command == "move_actor" || command == "rotate_actor" || command == "scale_actor") {
        const char* field = command == "move_actor" ? "position" : command == "rotate_actor" ? "rotation" : "scale";
        const char* verb = command == "move_actor" ? "Move " : command == "rotate_actor" ? "Turn " : "Scale ";
        return actor_edit(verb + text(p, "id"), [&](Value& a, std::string&) {
            a[field] = p.get(field);
            if (command == "move_actor" && sea_level_piece(a) && a[field].type == Value::Object) a[field]["y"] = 0.0;
            return true;
        });
    }
    if (command == "set_dialogue") {
        return actor_edit("Dialogue of " + text(p, "id"), [&](Value& a, std::string&) {
            a["lines"] = p.get("lines");
            return true;
        });
    }
    if (command == "set_scene") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const std::string property = text(p, "property"), id = text(p, "id");
        if (property != "trigger" && property != "steps" && property != "native") return why = "a scene's properties are trigger, steps and native", r;
        if (property == "steps" && p.get("value").type == Value::Null) return why = "a scene keeps its steps", r;
        ok = change(*pkg, "Set " + property + " of scene " + id, [&](Value& m, std::string& w) {
            Value* s = find_by_id(m["scenes"], id);
            if (!s) return w = "no scene \"" + id + "\"", false;
            if (p.get("value").type == Value::Null) s->object.erase(property);
            else (*s)[property] = p.get("value");
            return true;
        }, r, why);
        return r;
    }
    if (command == "set_quest_step") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const std::string property = text(p, "property"), quest = text(p, "quest");
        const double n = p.get("step").number;
        if (property == "n") return why = "the step number is not changed here", r;
        ok = change(*pkg, "Quest " + quest + " step " + std::to_string(int(n)) + ": " + property, [&](Value& m, std::string& w) {
            Value* q = find_by_id(m["quests"], quest);
            if (!q) return w = "no quest \"" + quest + "\"", false;
            for (auto& s : (*q)["steps"].array)
                if (s.get("n").type == Value::Number && s.get("n").number == n) {
                    if (p.get("value").type == Value::Null) s.object.erase(property);
                    else s[property] = p.get("value");
                    return true;
                }
            return w = "quest " + quest + " has no step " + std::to_string(int(n)), false;
        }, r, why);
        return r;
    }
    if (command == "undo" || command == "redo") {
        auto& from = command == "undo" ? undo_ : redo_;
        auto& to = command == "undo" ? redo_ : undo_;
        if (from.empty()) return why = std::string("nothing to ") + command, r;
        Step s = from.back();
        if (s.layout) {  // the quest graph's node places
            auto* lp = const_cast<Package*>(find(s.mod));
            if (!lp) return why = "the mod of this step is gone", r;
            lp->layout = command == "undo" ? s.before : s.after;
            r["mod"] = s.mod;
        } else if (!restore(s.mod, command == "undo" ? s.before : s.after, r, why))
            return r;
        from.pop_back();
        to.push_back(s);
        r["step"] = s.label;
        ok = true;
        return r;
    }
    if (command == "history") {
        r["undo"].type = Value::Array;
        r["redo"].type = Value::Array;
        for (const auto& s : undo_) r["undo"].array.push_back(Value(s.mod + ": " + s.label));
        for (const auto& s : redo_) r["redo"].array.push_back(Value(s.mod + ": " + s.label));
        ok = true;
        return r;
    }
    if (command == "validate") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        std::string problem;
        const bool valid = validate_package(pkg->mod, pkg->manifest, all_manifests(), problem) && model_files(pkg->dir, pkg->manifest, problem) && world_placements(host_, pkg->mod, pkg->manifest, pkg->manifest, true, problem);
        r["valid"] = valid;
        if (!valid) r["problem"] = problem;
        ok = true;
        return r;
    }
    if (command == "save_package") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        std::string problem;
        // a placement where it can't stand is saved (the work is kept) and listed as a problem (Check and export refuse it)
        std::vector<std::string> held;
        if (!validate_package(pkg->mod, pkg->manifest, all_manifests(), problem) || !model_files(pkg->dir, pkg->manifest, problem) || !world_placements(host_, pkg->mod, pkg->manifest, pkg->manifest, true, problem, &held))
            return why = "not saved: " + problem, r;
        note_held(held, r);
        const fs::path file = fs::path(pkg->dir) / "manifest.json";
        std::error_code ec;
        if (pkg->dir.empty() || !fs::is_regular_file(file, ec) || fs::is_symlink(file, ec)) return why = "the package has no manifest.json to replace", r;
        const fs::path tmp = fs::path(pkg->dir) / "manifest.json.editor-tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            f << pretty(pkg->manifest);
            if (!f.good()) return why = "cannot write the package", r;
        }
        fs::rename(tmp, file, ec);
        if (ec) {
            fs::remove(tmp, ec);
            return why = "cannot replace manifest.json", r;
        }
        pkg->saved = pkg->manifest;
        r["file"] = file.string();
        // the quest graph's node places, next to it in the package's editor folder
        if (!(pkg->layout == pkg->layout_saved) && pkg->layout.type == Value::Object) {
            const fs::path dir = fs::path(pkg->dir) / "editor";
            fs::create_directories(dir, ec);
            std::ofstream f(dir / "quest-layout.json", std::ios::binary | std::ios::trunc);
            f << pretty(pkg->layout);
            if (f.good()) {
                pkg->layout_saved = pkg->layout;
                r["layout"] = (dir / "quest-layout.json").string();
            } else
                r["note"] = "the quest graph's node places could not be written";
        }
        ok = true;
        return r;
    }
    if (command == "revert_package") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        pkg->layout = pkg->layout_saved;  // the quest graph's node places too
        if (pkg->manifest == pkg->saved) {
            r["changed"] = false;
            ok = true;
            return r;
        }
        undo_.push_back({pkg->mod, "Revert to the saved package", pkg->manifest, pkg->saved});
        redo_.clear();
        ok = restore(pkg->mod, pkg->saved, r, why);
        return r;
    }
    if (command == "reload_room") {
        if (!host_) return why = "the game is not running", r;
        if (host_->busy(why) || !host_->reload_room(why)) return r;
        r["requested"] = true;
        ok = true;
        return r;
    }
    if (command == "list_locations") {
        r.type = Value::Array;
        for (const auto& l : locations())
            if (p.get("group").type != Value::String || p.get("group").text == l.group) r.array.push_back(location_value(l));
        ok = true;
        return r;
    }
    if (command == "warp" || command == "warp_to" || command == "warp_back") {
        Location to;
        if (command == "warp") {
            to.id = "manual";
            to.stage = text(p, "stage");
            to.room = int(p.get("room").number);
            if (to.stage.rfind("stage:", 0) == 0 && host_) {  // a package's stage: "stage:<mod>/<id>"
                std::string name;
                for (const auto& ms : host_->mod_stages())
                    if ("stage:" + ms.mod + "/" + ms.id == to.stage) name = ms.name;
                if (name.empty()) return why = "no mod stage \"" + to.stage + "\" is installed (list_locations, group Mod stages)", r;
                to.stage = name;
            }
            if (to.stage.empty() || to.stage.size() > 7 || to.room < 0 || to.room > 63) return why = "a stage name of up to 7 characters and a room 0..63", r;
            if (p.get("point").type == Value::Number) to.point = int(p.get("point").number);
            if (p.get("position").type != Value::Null) {
                read_point(p.get("position"), to.position);
                to.point = kAtPosition;
                to.angle = int(p.get("angle").number) & 0xFFFF;
            }
        } else if (command == "warp_to") {
            bool found = false;
            for (const auto& l : locations())
                if (l.id == text(p, "location")) to = l, found = true;
            if (!found) return why = "no location \"" + text(p, "location") + "\" (list_locations)", r;
        } else {
            if (!last_place_.valid) return why = "no warp yet", r;
            to.id = "back";
            to.stage = last_place_.stage;
            to.room = last_place_.room;
            to.point = kAtPosition;
            to.position = last_place_.position;
            to.angle = last_place_.angle;
        }
        if (!warp_to(to, why)) return r;  // warp_back twice: back and forth between the two places
        r["requested"] = true;
        r["to"] = location_value(to);
        ok = true;
        return r;
    }
    if (command == "read_state") {
        r["open_mod"] = open_;
        r["dirty"] = dirty(open_);
        r["undo"] = int(undo_.size());
        r["redo"] = int(redo_.size());
        r["last_place"] = place_value(last_place_);
        if (host_) {
            r["link"] = place_value(host_->link());
            r["fly"] = host_->fly_state();
            r["test_mode"] = host_->test_mode();
            const auto loaded=host_->load_state_result();
            if(loaded.type!=Value::Null)r["state_load"]=loaded;
            std::string busy;
            if (host_->busy(busy)) r["busy"] = busy;
            if (const Package* pkg = find(open_)) r["progress"] = progress(pkg->mod, pkg->manifest);
        }
        ok = true;
        return r;
    }
    if (command == "fly_camera") {
        if (!host_) return why = "the game is not running", r;
        const bool on = p.get("on").boolean;
        if (on && host_->busy(why)) return r;
        if (!host_->fly(on, p.get("place_link").boolean, why)) return r;
        r["requested"] = true;
        ok = true;
        return r;
    }
    if (command == "test_mode") {
        if (!host_) return why = "the game is not running", r;
        if (!host_->set_test_mode(p.get("on").boolean, why)) return r;
        r["test_mode"] = p.get("on").boolean;
        ok = true;
        return r;
    }
    if (command == "reset_quest" || command == "save_state" || command == "load_state") {
        if (!host_) return why = "the game is not running", r;
        if (!host_->test_mode()) return why = command + " works in test mode only (test_mode on)", r;
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const auto own = mod_records(pkg->mod, pkg->manifest);
        if (command == "reset_quest") {
            std::vector<Record> shared;
            for (const auto& other : packages_)
                if (other.mod != pkg->mod)
                    for (const auto& rec : mod_records(other.mod, other.manifest)) shared.push_back(rec);
            unsigned erased = 0;
            if (!host_->reset_records(pkg->mod, own, shared, erased, why)) return r;
            r["requested"] = true;
            ok = true;
            return r;
        }
        const int slot = int(p.get("slot").number);
        if (slot < 1 || slot > kTestSlots) return why = "test slots are 1.." + std::to_string(kTestSlots), r;
        const auto&kind=p.get("kind");
        if(kind.type!=Value::Null&&(kind.type!=Value::String||(kind.string()!="portable"&&kind.string()!="full")))return why="kind must be portable or full",r;
        if (!host_->test_state_format(command == "save_state", slot, pkg->mod, own, kind.string()=="full", why)) return r;
        r["requested"] = true;
        r["slot"] = slot;
        if(command=="load_state"){
            const auto loaded=host_->load_state_result();
            if(loaded.type!=Value::Null)r["state_load"]=loaded;
        }
        ok = true;
        return r;
    }
    if (command == "command_log") {
        const size_t count = p.get("count").type == Value::Number ? size_t(std::clamp(p.get("count").number, 1.0, double(kLogSize))) : 20;
        r.type = Value::Array;
        for (size_t i = log_.size() > count ? log_.size() - count : 0; i < log_.size(); i++) {
            Value v;
            v["seq"] = double(log_[i].seq);
            v["command"] = log_[i].command;
            v["params"] = log_[i].params;
            v["result"] = log_[i].result;
            if (!log_[i].source.empty()) v["source"] = log_[i].source;
            r.array.push_back(v);
        }
        ok = true;
        return r;
    }
    bool handled = false;
    r = dispatch_build(command, p, why, ok, handled);
    if (handled) return r;
    r = dispatch_scene(command, p, why, ok, handled);
    if (handled) return r;
    r = dispatch_game_objects(command, p, why, ok, handled);
    if (handled) return r;
    r = dispatch_quest(command, p, why, ok, handled);
    if (handled) return r;
    r = dispatch_story(command, p, why, ok, handled);
    if (handled) return r;
    r = dispatch_route(command, p, why, ok, handled);
    if (handled) return r;
    r = dispatch_paths(command, p, why, ok, handled);
    if (handled) return r;
    r = dispatch_water(command, p, why, ok, handled);
    if (handled) return r;
    r = dispatch_stages(command, p, why, ok, handled);
    if (handled) return r;
    why = "command \"" + command + "\" is not implemented";
    return r;
}

}  // namespace editor
