// SPDX-License-Identifier: MPL-2.0
// Mod stages: the manifest's "stages" section (docs/mod-stages.md), the stage names the port gives them and
// the recipe the stage builder (tools/story_mods/stage_builder.py) builds a stage from. Pure code, unit-tested
// in runtime/tools/mod_stages_test.cpp; the running game's side is mod_stages.h.
//
// A mod stage is a stage of its own name that the game loads through its own stage code. The package names
// it by its id; the port gives it a seven-character game name ("Z" and six letters or digits from the
// package and stage ids), which the package never sees. Its rooms are the package's ground models drawn with
// the materials of a room of the template stage (the "kit"), its lighting, camera and door models are the
// template's, and the game's save file only ever stores the stage's entrance, a place of the game.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "mod_json.h"
#include "mod_stages_actors.h"

namespace mod_stages {

using mods::json::Value;

inline constexpr unsigned kMaxStagesPerMod = 4, kMaxRooms = 16, kMaxSpawns = 8, kMaxExits = 8, kMaxDoors = 16, kMaxEntries = 4, kMaxWarps = 4,
                          kMaxRoomActors = 16, kMaxChests = 32;
// the dungeon switches doors are given, from the top down (a package's own switch numbers stay below)
inline constexpr unsigned kFirstDoorSwitch = 0x7F, kOwnSwitches = 0x40;
inline constexpr double kMaxCoordinate = 100000;  // room coordinates (game units)
inline constexpr double kMaxWorld = 350000;       // entry positions in the game's rooms (the sea is large)

struct Point {
    double x = 0, y = 0, z = 0;
    bool operator==(const Point&) const = default;
};
// A place: a stage, room and spawn point. In a manifest, "stage" is a stage of the game, "stage:<id>" one of
// the package's stages, or left out for the stage it is written in.
struct Place {
    std::string stage;   // a game stage name, or (own == true) the package's stage id
    bool own = false;
    unsigned room = 0, point = 0;
    bool operator==(const Place&) const = default;
};
struct Spawn {
    unsigned point = 0;
    Point position;
    double angle = 0;          // degrees about the vertical axis (0: towards +z, 90: towards +x)
    std::string start = "stand";  // or "fall"
    bool operator==(const Spawn&) const = default;
};
struct Exit {
    std::string id;
    Point min, max;  // the box: floor triangles inside it take this exit
    Place to;
    bool operator==(const Exit&) const = default;
};
// An actor of the game placed in a room of the stage (mod_stages_actors.h: the kinds a stage may hold). A
// chest's flag (which of the stage's 32 chest bits it uses) is given by parse, in the order chests are declared.
struct RoomActor {
    std::string id, actor, variant, item;
    Point position;
    double angle = 0;
    unsigned flag = 0;  // a chest's flag (0..31)
    bool operator==(const RoomActor&) const = default;
};
struct Room {
    unsigned room = 0, kit = 0;
    std::string kit_stage;  // the kit room's stage ("" the template)
    bool kit_ground = false;  // "ground": "kit": the kit room's own models and collision
    std::string ground;    // the package's GLB (a path in the package)
    std::string material;  // the kit material the ground is drawn with ("" the kit's first)
    std::map<std::string, std::string> materials;  // GLB material name -> kit material
    std::vector<Spawn> spawns;
    std::vector<Exit> exits;
    std::vector<RoomActor> actors;
    int floor = 1;  // the map's floor: 1 (1F) and up, -1 (B1) and down
    bool operator==(const Room&) const = default;
};
struct Door {
    Point position;
    double angle = 0;
    unsigned a = 0, b = 0;  // the rooms on its two sides
    std::string lock = "none";  // none, small_key, boss or shutter
    unsigned switch_no = 0xFF;  // the switch a locked door keeps unlocked (given by parse) or a shutter opens on
    bool operator==(const Door&) const = default;
};
// An entry: a box in a room of the game that takes Link into this stage (the stage's way in).
struct Entry {
    std::string id, stage;
    unsigned room = 0;
    Point position, size;  // the box: its centre bottom and full size
    unsigned to_room = 0, to_point = 0;
    std::string style = "walk";  // "walk": the fade as he steps in; "updraft": the wind lifts him first
    double height = 1500;        // an updraft's lift (units) before the stage takes over
    bool operator==(const Entry&) const = default;
};
// A return warp: a box in a room of the stage that takes Link to a place (by default the stage's entrance)
// with the game's warp light.
struct Warp {
    std::string id;
    unsigned room = 0;
    Point position, size;
    Place to;  // the entrance when "to" is left out
    std::string after;    // a chest of the stage (its id) that must be open first ("" always open)
    int after_flag = -1;  // that chest's flag
    bool operator==(const Warp&) const = default;
};
struct Stage {
    std::string id, template_stage, kind = "cave", music_like;
    std::string shaders;  // a code-free shader template (.wwht) in the package: setup prepares its render states
    std::string save_group;  // stages of one group share their save record and dungeon switches (default: the id)
    Place entrance;
    std::vector<Room> rooms;
    std::vector<Door> doors;
    std::vector<std::pair<unsigned, unsigned>> joins;  // rooms joined by an open passage (no door)
    std::vector<Entry> entries;
    std::vector<Warp> warps;
    bool keys = false;  // the key counter shows (a small-key door or a small key in the stage)
    bool map = false;   // the stage has a dungeon map (the map screen's dungeon view, drawn by the port)
    std::string map_title;  // its title on the map screen ("" none)
    bool operator==(const Stage&) const = default;
};

namespace detail {
inline bool keys(const Value& v, std::initializer_list<const char*> allowed) {
    if (v.type != Value::Object) return false;
    for (const auto& [key, value] : v.object)
        if (std::none_of(allowed.begin(), allowed.end(), [&](const char* a) { return key == a; })) return false;
    return true;
}
inline bool integer(const Value& v, double lo, double hi, unsigned& out) {
    if (v.type != Value::Number || !std::isfinite(v.number) || v.number < lo || v.number > hi || std::floor(v.number) != v.number) return false;
    out = unsigned(v.number);
    return true;
}
inline bool number(const Value& v, double lo, double hi, double& out) {
    if (v.type != Value::Number || !std::isfinite(v.number) || v.number < lo || v.number > hi) return false;
    out = v.number;
    return true;
}
inline bool point(const Value& v, double limit, Point& p) {
    return keys(v, {"x", "y", "z"}) && number(v.get("x"), -limit, limit, p.x) && number(v.get("y"), -limit, limit, p.y) &&
           number(v.get("z"), -limit, limit, p.z);
}
inline bool id(const Value& v) {
    if (v.type != Value::String || v.text.empty() || v.text.size() > 32) return false;
    return std::all_of(v.text.begin(), v.text.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'; });
}
}  // namespace detail

// A stage name of the game: 1..7 letters, digits or underscores (the game copies names into 8 bytes).
inline bool game_stage_name(const std::string& s) {
    return !s.empty() && s.size() <= 7 &&
           std::all_of(s.begin(), s.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; });
}
// A path in the package: relative, "/"-separated, no "." or ".." parts, ending in ".glb".
inline bool package_glb(const std::string& s) {
    if (s.empty() || s.size() > 240 || s.front() == '/' || s.size() < 5 || s.substr(s.size() - 4) != ".glb") return false;
    size_t start = 0;
    while (start <= s.size()) {
        const size_t end = s.find('/', start);
        const std::string part = s.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part.empty() || part == "." || part == "..") return false;
        for (char c : part)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

// {"stage": ..., "room": n, "point": n}: "stage" a game stage, "stage:<id>" one of the package's, or left out
// (allow_self) for the stage being declared.
inline bool parse_place(const Value& v, bool allow_self, Place& p) {
    using namespace detail;
    if (!keys(v, {"stage", "room", "point"}) || !integer(v.get("room"), 0, 63, p.room) || !integer(v.get("point"), 0, 255, p.point)) return false;
    const Value& s = v.get("stage");
    if (s.type == Value::Null) {
        if (!allow_self) return false;
        p.stage.clear();
        p.own = true;
        return true;
    }
    if (s.type != Value::String) return false;
    if (s.text.rfind("stage:", 0) == 0) {
        Value idv(s.text.substr(6));
        if (!id(idv)) return false;
        p.stage = idv.text;
        p.own = true;
        return true;
    }
    if (!game_stage_name(s.text)) return false;
    p.stage = s.text;
    p.own = false;
    return true;
}

// Stages whose music follows the story: for the boss rooms (Dragon Roost, Forbidden Woods, Tower of the
// Gods, Earth and Wind Temple bosses) the game's audio reads the dungeon's save bits (its boss beaten) when it
// sets the scene, so a mod stage that borrowed their music would follow that dungeon's state. The runtime
// refuses any name whose scene is one of those too (mod_stages.cpp kStoryScenes).
inline bool music_allowed(const std::string& stage) {
    static const std::set<std::string> refused = {"M_DragB", "kinBOSS", "kazeB", "SirenB", "M_DaiB"};
    return game_stage_name(stage) && !refused.count(stage);
}

// The "stages" section. Checks what can be checked without the game files: ids, places, rooms numbered 0, 1,
// ... in order, every room with a spawn, spawn points unique in the stage, exits into the stage reaching a
// spawn of it, doors joining two rooms of the stage, entries into a spawn of the stage, and references to the
// package's other stages. The builder checks the rest against the player's files (the template, the kit
// rooms and their materials, each exit box covering floor). Without `require_reachable` a room nothing leads
// to yet passes (the editor keeps it while a stage is being made; its Check and export list it).
inline bool parse(const Value& m, std::vector<Stage>& out, std::string& why, bool require_reachable = true) {
    using namespace detail;
    out.clear();
    const Value& list = m.get("stages");
    if (list.type == Value::Null) return true;
    if (list.type != Value::Array || list.array.size() > kMaxStagesPerMod) return why = "stages must be a list of up to " + std::to_string(kMaxStagesPerMod), false;
    for (const auto& v : list.array) {
        Stage s;
        if (!keys(v, {"id", "template", "kind", "music_like", "save_group", "entrance", "rooms", "doors", "joins", "entries", "warps", "map", "shaders"})) return why = "unknown stage field", false;
        if (!id(v.get("id"))) return why = "invalid stage id", false;
        s.id = v.get("id").text;
        const std::string at = "stage " + s.id + ": ";
        for (const auto& old : out)
            if (old.id == s.id) return why = at + "duplicate stage id", false;
        if (v.get("template").type != Value::String || !game_stage_name(v.get("template").text)) return why = at + "template names a stage of the game", false;
        s.template_stage = v.get("template").text;
        s.save_group = s.id;
        if (v.get("map").type != Value::Null) {
            const Value& mv = v.get("map");
            if (!keys(mv, {"title"}) || (mv.get("title").type != Value::Null && (mv.get("title").type != Value::String || mv.get("title").text.size() > 40)))
                return why = at + "map is {\"title\": up to 40 characters}", false;
            s.map = true;
            s.map_title = mv.get("title").string();
        }
        if (v.get("save_group").type != Value::Null) {
            if (!id(v.get("save_group"))) return why = at + "invalid save_group", false;
            s.save_group = v.get("save_group").text;
        }
        if (v.get("kind").type != Value::Null) {
            s.kind = v.get("kind").string();
            if (s.kind != "cave" && s.kind != "dungeon") return why = at + "kind is cave or dungeon", false;
        }
        if (v.get("shaders").type != Value::Null) {
            s.shaders = v.get("shaders").string();
            const bool ok = s.shaders.size() > 5 && s.shaders.size() <= 200 && s.shaders.substr(s.shaders.size() - 5) == ".wwht" &&
                            s.shaders.front() != '/' && s.shaders.find('\\') == std::string::npos && s.shaders.find("..") == std::string::npos;
            if (!ok) return why = at + "\"shaders\" is a .wwht file in the package (assets/stage.wwht)", false;
        }
        if (v.get("music_like").type != Value::Null) {
            s.music_like = v.get("music_like").string();
            if (!music_allowed(s.music_like)) return why = at + "music_like names a stage whose music has no story state (a cave, a house, ...)", false;
        }
        if (!parse_place(v.get("entrance"), false, s.entrance) || s.entrance.own)
            return why = at + "entrance is a stage, room and point of the game (where a save made in the stage continues)", false;
        const Value& rooms = v.get("rooms");
        if (rooms.type != Value::Array || rooms.array.empty() || rooms.array.size() > kMaxRooms) return why = at + "rooms must be a list of 1 to " + std::to_string(kMaxRooms), false;
        std::set<unsigned> points;
        for (const auto& rv : rooms.array) {
            Room r;
            if (!keys(rv, {"room", "kit", "ground", "material", "materials", "spawns", "exits", "actors", "floor"})) return why = at + "unknown room field", false;
            if (!integer(rv.get("room"), 0, 63, r.room) || r.room != s.rooms.size()) return why = at + "rooms are numbered 0, 1, ... in order", false;
            const std::string rat = at + "room " + std::to_string(r.room) + ": ";
            if (rv.get("floor").type != Value::Null) {
                const Value& f = rv.get("floor");
                if (f.type != Value::Number || std::floor(f.number) != f.number || f.number < -5 || f.number > 5 || f.number == 0)
                    return why = rat + "floor is 1..5 (1F..5F) or -1..-5 (B1..B5)", false;
                r.floor = int(f.number);
            }
            const Value& kit = rv.get("kit");
            if (kit.type == Value::Object) {  // a room of any stage of the game
                if (!keys(kit, {"stage", "room"}) || kit.get("stage").type != Value::String || !game_stage_name(kit.get("stage").text) || !integer(kit.get("room"), 0, 63, r.kit))
                    return why = rat + "kit is a room of the template, or {\"stage\", \"room\"} of any stage", false;
                r.kit_stage = kit.get("stage").text == s.template_stage ? "" : kit.get("stage").text;
            } else if (kit.type != Value::Null && !integer(kit, 0, 63, r.kit))
                return why = rat + "kit is a room of the template, or {\"stage\", \"room\"} of any stage", false;
            if (kit.type == Value::Null) r.kit = r.room;
            if (rv.get("ground").type == Value::String && rv.get("ground").text == "kit")
                r.kit_ground = true;  // the kit room as it is: its models and collision
            else if (rv.get("ground").type != Value::String || !package_glb(rv.get("ground").text))
                return why = rat + "ground is a .glb file of the package, or \"kit\"", false;
            else
                r.ground = rv.get("ground").text;
            if (rv.get("material").type != Value::Null) {
                if (rv.get("material").type != Value::String || rv.get("material").text.empty() || rv.get("material").text.size() > 64) return why = rat + "invalid material", false;
                r.material = rv.get("material").text;
            }
            if (rv.get("materials").type != Value::Null) {
                if (rv.get("materials").type != Value::Object || rv.get("materials").object.size() > 16) return why = rat + "materials maps up to 16 model materials to kit materials", false;
                for (const auto& [k, mv] : rv.get("materials").object) {
                    if (k.empty() || k.size() > 64 || mv.type != Value::String || mv.text.empty() || mv.text.size() > 64) return why = rat + "invalid materials entry", false;
                    r.materials[k] = mv.text;
                }
            }
            const Value& spawns = rv.get("spawns");
            if (spawns.type != Value::Array || spawns.array.empty() || spawns.array.size() > kMaxSpawns) return why = rat + "spawns must be a list of 1 to " + std::to_string(kMaxSpawns), false;
            for (const auto& sv : spawns.array) {
                Spawn sp;
                if (!keys(sv, {"point", "position", "angle", "start"}) || !integer(sv.get("point"), 0, 99, sp.point) || !point(sv.get("position"), kMaxCoordinate, sp.position))
                    return why = rat + "a spawn has a point (0..99) and a position", false;
                if (sv.get("angle").type != Value::Null && !number(sv.get("angle"), -360, 360, sp.angle)) return why = rat + "invalid spawn angle", false;
                if (sv.get("start").type != Value::Null) {
                    sp.start = sv.get("start").string();
                    if (sp.start != "stand" && sp.start != "fall") return why = rat + "a spawn's start is stand or fall", false;
                }
                if (!points.insert(sp.point).second) return why = rat + "spawn point " + std::to_string(sp.point) + " is used twice in the stage", false;
                r.spawns.push_back(sp);
            }
            const Value& exits = rv.get("exits");
            if (exits.type != Value::Null && (exits.type != Value::Array || exits.array.size() > kMaxExits)) return why = rat + "exits must be a list of up to " + std::to_string(kMaxExits), false;
            for (const auto& ev : exits.array) {
                Exit e;
                if (!keys(ev, {"id", "box", "to"}) || !id(ev.get("id"))) return why = rat + "an exit has an id, a box and a place it leads to", false;
                e.id = ev.get("id").text;
                for (const auto& old : r.exits)
                    if (old.id == e.id) return why = rat + "duplicate exit id", false;
                const Value& box = ev.get("box");
                if (!keys(box, {"min", "max"}) || !point(box.get("min"), kMaxCoordinate, e.min) || !point(box.get("max"), kMaxCoordinate, e.max) ||
                    !(e.min.x < e.max.x && e.min.y < e.max.y && e.min.z < e.max.z))
                    return why = rat + "exit " + e.id + ": the box needs min below max", false;
                if (!parse_place(ev.get("to"), true, e.to)) return why = rat + "exit " + e.id + ": invalid place it leads to", false;
                r.exits.push_back(e);
            }
            const Value& actors = rv.get("actors");
            if (actors.type != Value::Null && (actors.type != Value::Array || actors.array.size() > kMaxRoomActors))
                return why = rat + "actors must be a list of up to " + std::to_string(kMaxRoomActors), false;
            for (const auto& av : actors.array) {
                RoomActor a;
                if (!keys(av, {"id", "actor", "variant", "item", "position", "angle"}) || !id(av.get("id")) || av.get("actor").type != Value::String ||
                    !point(av.get("position"), kMaxCoordinate, a.position))
                    return why = rat + "an actor has an id, an actor kind and a position", false;
                a.id = av.get("id").text;
                a.actor = av.get("actor").text;
                for (const auto& old : r.actors)
                    if (old.id == a.id) return why = rat + "duplicate actor id " + a.id, false;
                if (av.get("variant").type != Value::Null) {
                    if (av.get("variant").type != Value::String) return why = rat + "actor " + a.id + ": invalid variant", false;
                    a.variant = av.get("variant").text;
                }
                if (av.get("item").type != Value::Null) {
                    if (a.actor != "chest" || av.get("item").type != Value::String) return why = rat + "actor " + a.id + ": only a chest holds an item", false;
                    a.item = av.get("item").text;
                }
                if (av.get("angle").type != Value::Null && !number(av.get("angle"), -360, 360, a.angle)) return why = rat + "actor " + a.id + ": invalid angle", false;
                NativeActor n;
                std::string reason;
                if (!native_actor(a.actor, a.variant, a.item, 0, n, reason)) return why = rat + "actor " + a.id + ": " + reason, false;
                r.actors.push_back(a);
            }
            s.rooms.push_back(std::move(r));
        }
        const Value& doors = v.get("doors");
        if (doors.type != Value::Null && (doors.type != Value::Array || doors.array.size() > kMaxDoors)) return why = at + "doors must be a list of up to " + std::to_string(kMaxDoors), false;
        for (const auto& dv : doors.array) {
            Door d;
            const Value& rr = dv.get("rooms");
            if (!keys(dv, {"position", "angle", "rooms", "lock", "switch"}) || !point(dv.get("position"), kMaxCoordinate, d.position) || rr.type != Value::Array || rr.array.size() != 2 ||
                !integer(rr.array[0], 0, 63, d.a) || !integer(rr.array[1], 0, 63, d.b))
                return why = at + "a door has a position and the two rooms it joins", false;
            if (dv.get("angle").type != Value::Null && !number(dv.get("angle"), -360, 360, d.angle)) return why = at + "invalid door angle", false;
            if (d.a == d.b || d.a >= s.rooms.size() || d.b >= s.rooms.size()) return why = at + "a door joins two different rooms of the stage", false;
            if (dv.get("lock").type != Value::Null) {
                d.lock = dv.get("lock").string();
                if (d.lock != "none" && d.lock != "small_key" && d.lock != "boss" && d.lock != "shutter") return why = at + "a door's lock is none, small_key, boss or shutter", false;
            }
            if (d.lock == "shutter") {
                if (!integer(dv.get("switch"), 0, kOwnSwitches - 1, d.switch_no))
                    return why = at + "a shutter opens on a switch: \"switch\" 0.." + std::to_string(kOwnSwitches - 1), false;
            } else if (dv.get("switch").type != Value::Null)
                return why = at + "only a shutter takes a switch", false;
            s.doors.push_back(d);
        }
        const Value& entries = v.get("entries");
        if (entries.type != Value::Null && (entries.type != Value::Array || entries.array.size() > kMaxEntries)) return why = at + "entries must be a list of up to " + std::to_string(kMaxEntries), false;
        for (const auto& nv : entries.array) {
            Entry n;
            const Value& to = nv.get("to");
            if (!keys(nv, {"id", "stage", "room", "position", "size", "to", "style", "height"}) || !id(nv.get("id")))
                return why = at + "an entry has an id, a place in the game and the spawn it leads to", false;
            n.id = nv.get("id").text;
            if (nv.get("stage").type != Value::String || !game_stage_name(nv.get("stage").text) || !integer(nv.get("room"), 0, 63, n.room))
                return why = at + "entry " + n.id + ": stage and room name a room of the game", false;
            n.stage = nv.get("stage").text;
            if (!point(nv.get("position"), kMaxWorld, n.position) || !point(nv.get("size"), 5000, n.size) || n.size.x < 10 || n.size.y < 10 || n.size.z < 10)
                return why = at + "entry " + n.id + ": a position and a size (10..5000 units each way)", false;
            if (!keys(to, {"room", "point"}) || !integer(to.get("room"), 0, 63, n.to_room) || !integer(to.get("point"), 0, 99, n.to_point))
                return why = at + "entry " + n.id + ": \"to\" is a room and spawn point of the stage", false;
            if (nv.get("style").type != Value::Null) {
                n.style = nv.get("style").string();
                if (n.style != "walk" && n.style != "updraft") return why = at + "entry " + n.id + ": style is walk or updraft", false;
            }
            if (nv.get("height").type != Value::Null) {
                const Value& h = nv.get("height");
                if (n.style != "updraft") return why = at + "entry " + n.id + ": only an updraft has a height", false;
                if (h.type != Value::Number || !(h.number >= 200 && h.number <= 5000)) return why = at + "entry " + n.id + ": height is 200..5000 units", false;
                n.height = h.number;
            }
            for (const auto& old : s.entries)
                if (old.id == n.id) return why = at + "duplicate entry id", false;
            s.entries.push_back(n);
        }
        const Value& joins = v.get("joins");
        if (joins.type != Value::Null && (joins.type != Value::Array || joins.array.size() > 32)) return why = at + "joins must be a list of up to 32 room pairs", false;
        for (const auto& jv : joins.array) {
            unsigned a = 0, b = 0;
            if (jv.type != Value::Array || jv.array.size() != 2 || !integer(jv.array[0], 0, 63, a) || !integer(jv.array[1], 0, 63, b) || a == b || a >= s.rooms.size() ||
                b >= s.rooms.size())
                return why = at + "a join is two different rooms of the stage", false;
            s.joins.push_back({a, b});
        }
        const Value& warps = v.get("warps");
        if (warps.type != Value::Null && (warps.type != Value::Array || warps.array.size() > kMaxWarps)) return why = at + "warps must be a list of up to " + std::to_string(kMaxWarps), false;
        for (const auto& wv : warps.array) {
            Warp w;
            if (!keys(wv, {"id", "room", "position", "size", "to", "after"}) || !id(wv.get("id")) || !integer(wv.get("room"), 0, 63, w.room) || w.room >= s.rooms.size())
                return why = at + "a warp has an id and a room of the stage", false;
            w.id = wv.get("id").text;
            if (!point(wv.get("position"), kMaxCoordinate, w.position) || !point(wv.get("size"), 5000, w.size) || w.size.x < 10 || w.size.y < 10 || w.size.z < 10)
                return why = at + "warp " + w.id + ": a position and a size (10..5000 units each way)", false;
            w.to = s.entrance;
            if (wv.get("to").type != Value::Null && !parse_place(wv.get("to"), true, w.to)) return why = at + "warp " + w.id + ": invalid place it leads to", false;
            if (wv.get("after").type != Value::Null) {
                w.after = wv.get("after").string();
                bool chest = false;
                for (const auto& r : s.rooms)
                    for (const auto& a : r.actors) chest |= a.id == w.after && a.actor == "chest";
                if (!chest) return why = at + "warp " + w.id + ": \"after\" names a chest of the stage (its id)", false;
            }
            for (const auto& old : s.warps)
                if (old.id == w.id) return why = at + "duplicate warp id", false;
            s.warps.push_back(w);
        }
        out.push_back(std::move(s));
    }
    // chests and doors take their flags and switches per save group, in the order they are declared (a save
    // keeps them by number: keep the order when a package changes)
    {
        std::map<std::string, unsigned> chests, switches;
        for (auto& s : out) {
            unsigned& chest = chests[s.save_group];
            unsigned& sw = switches.emplace(s.save_group, kFirstDoorSwitch).first->second;
            for (auto& r : s.rooms)
                for (auto& a : r.actors)
                    if (a.actor == "chest") {
                        if (chest >= kMaxChests) return why = "save group " + s.save_group + ": more than 32 chests", false;
                        a.flag = chest++;
                    }
            for (auto& w : s.warps)
                for (const auto& r : s.rooms)
                    for (const auto& a : r.actors)
                        if (!w.after.empty() && a.id == w.after) w.after_flag = int(a.flag);
            for (auto& d : s.doors)
                if (d.lock == "small_key" || d.lock == "boss") {
                    if (sw < kOwnSwitches) return why = "save group " + s.save_group + ": too many locked doors", false;
                    d.switch_no = sw--;
                }
        }
    }
    // keys: a small-key door needs a small key, a boss door the boss key, somewhere in the save group
    for (auto& s : out) {
        auto items = [&](const std::string& item) {
            unsigned n = 0;
            for (const auto& o : out)
                if (o.save_group == s.save_group)
                    for (const auto& r : o.rooms)
                        for (const auto& a : r.actors) n += a.actor == "chest" && a.item == item;
            return n;
        };
        unsigned small_doors = 0;
        for (const auto& d : s.doors) {
            small_doors += d.lock == "small_key";
            if (d.lock == "boss" && !items("boss_key")) return why = "stage " + s.id + ": a boss door needs a boss key in a chest of the save group", false;
        }
        if (small_doors && !items("small_key")) return why = "stage " + s.id + ": a small-key door needs a small key in a chest of the save group", false;
        s.keys = small_doors || items("small_key");
    }
    // places inside the package: rooms and spawns that exist
    auto spawn_of = [&](const Stage& s, unsigned room, unsigned point) {
        return room < s.rooms.size() && std::any_of(s.rooms[room].spawns.begin(), s.rooms[room].spawns.end(), [&](const Spawn& sp) { return sp.point == point; });
    };
    for (const auto& s : out) {
        for (const auto& r : s.rooms)
            for (const auto& e : r.exits) {
                if (!e.to.own) continue;
                const Stage* target = &s;
                if (!e.to.stage.empty()) {
                    auto it = std::find_if(out.begin(), out.end(), [&](const Stage& o) { return o.id == e.to.stage; });
                    if (it == out.end()) return why = "stage " + s.id + ": exit " + e.id + " leads to a stage the package does not declare", false;
                    target = &*it;
                }
                if (!spawn_of(*target, e.to.room, e.to.point)) return why = "stage " + s.id + ": exit " + e.id + " leads to a spawn its stage does not have", false;
            }
        for (const auto& n : s.entries)
            if (!spawn_of(s, n.to_room, n.to_point)) return why = "stage " + s.id + ": entry " + n.id + " leads to a spawn the stage does not have", false;
        for (const auto& w : s.warps)
            if (w.to.own) {
                const Stage* target = &s;
                if (!w.to.stage.empty()) {
                    auto it = std::find_if(out.begin(), out.end(), [&](const Stage& o) { return o.id == w.to.stage; });
                    if (it == out.end()) return why = "stage " + s.id + ": warp " + w.id + " leads to a stage the package does not declare", false;
                    target = &*it;
                }
                if (!spawn_of(*target, w.to.room, w.to.point)) return why = "stage " + s.id + ": warp " + w.id + " leads to a spawn its stage does not have", false;
            }
        // every room can be reached: from where Link comes in (the entries' rooms, room 0 without entries, the
        // rooms the package's other stages lead to), through doors, joins and exits inside the stage
        std::vector<bool> reached(s.rooms.size(), false);
        std::vector<unsigned> todo;
        auto reach = [&](unsigned room) {
            if (room < reached.size() && !reached[room]) reached[room] = true, todo.push_back(room);
        };
        if (s.entries.empty()) reach(0);
        for (const auto& n : s.entries) reach(n.to_room);
        for (const auto& o : out)
            for (const auto& r : o.rooms)
                for (const auto& e : r.exits)
                    if (&o != &s && e.to.own && e.to.stage == s.id) reach(e.to.room);
        while (!todo.empty()) {
            const unsigned room = todo.back();
            todo.pop_back();
            for (const auto& d : s.doors) {
                if (d.a == room) reach(d.b);
                if (d.b == room) reach(d.a);
            }
            for (const auto& [a, b] : s.joins) {
                if (a == room) reach(b);
                if (b == room) reach(a);
            }
            for (const auto& e : s.rooms[room].exits)
                if (e.to.own && e.to.stage.empty()) reach(e.to.room);
        }
        for (unsigned room = 0; room < reached.size(); ++room)
            if (require_reachable && !reached[room])
                return why = "stage " + s.id + ": room " + std::to_string(room) + " can't be reached (no door, join, exit or entry leads to it)", false;
    }
    return true;
}

// The stage's game name: "Z" and six base-36 digits of a hash of the package and stage ids; on the rare
// collision with a stage that exists (taken), the next of a sequence. The same package and stage get the
// same name on every machine and in every session (saves and save states name it).
inline uint64_t fnv1a(const std::string& s, uint64_t h = 1469598103934665603ull) {
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
    return h;
}
inline std::string allocate_name(const std::string& mod, const std::string& stage, const std::function<bool(const std::string&)>& taken) {
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    for (unsigned attempt = 0; attempt < 1000; ++attempt) {
        uint64_t h = fnv1a(mod + std::string(1, '\0') + stage + (attempt ? std::string(1, '\0') + std::to_string(attempt) : std::string()));
        std::string name = "Z";
        for (int i = 0; i < 6; ++i) {
            name += digits[h % 36];
            h /= 36;
        }
        if (!taken(name)) return name;
    }
    return {};
}
inline bool allocated_name(const std::string& s) {
    return s.size() == 7 && s[0] == 'Z' && std::all_of(s.begin() + 1, s.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); });
}

// The key of a stage in the mod save area (story_mail::Book records): a 16-bit hash of the package and stage ids.
inline uint16_t save_key(const std::string& mod, const std::string& stage) {
    const uint64_t h = fnv1a(mod + std::string(1, '\0') + stage + std::string(1, '\0') + "stage");
    return uint16_t((h >> 48) ^ (h >> 32) ^ (h >> 16) ^ h);
}

// The builder's recipe (stage_builder.py, format 1). names: the package's stage ids -> their game names;
// ground: the ground's path in the package -> its file on this machine.
inline Value recipe(const Stage& s, const std::string& name, const std::map<std::string, std::string>& names,
                    const std::function<std::string(const std::string&)>& ground) {
    auto vec = [](const Point& p) {
        Value v;
        v.type = Value::Array;
        v.array = {Value(p.x), Value(p.y), Value(p.z)};
        return v;
    };
    auto place = [&](const Place& p) {
        Value v;
        v["stage"] = !p.own ? p.stage : p.stage.empty() ? name : names.at(p.stage);
        v["room"] = Value(double(p.room));
        v["point"] = Value(double(p.point));
        return v;
    };
    Value r;
    r["format"] = Value(1);
    r["name"] = name;
    r["template"] = s.template_stage;
    r["kind"] = s.kind;
    r["entrance"] = place(s.entrance);
    r["keys"] = Value(s.keys);
    r["map_mode"] = Value(s.map ? 1 : 0);
    Value& rooms = r["rooms"];
    rooms.type = Value::Array;
    for (const auto& room : s.rooms) {
        Value v;
        v["room"] = Value(double(room.room));
        Value& kit = v["kit"];
        kit["stage"] = room.kit_stage.empty() ? s.template_stage : room.kit_stage;
        kit["room"] = Value(double(room.kit));
        v["ground"] = room.kit_ground ? std::string() : ground(room.ground);
        v["kit_ground"] = Value(room.kit_ground);
        v["floor"] = Value(double(room.floor));
        if (!room.material.empty()) v["material"] = room.material;
        Value& mats = v["materials"];
        mats.type = Value::Object;
        for (const auto& [k, m] : room.materials) mats[k] = m;
        Value& spawns = v["spawns"];
        spawns.type = Value::Array;
        for (const auto& sp : room.spawns) {
            Value e;
            e["point"] = Value(double(sp.point));
            e["position"] = vec(sp.position);
            e["angle"] = Value(sp.angle);
            e["start"] = sp.start;
            spawns.array.push_back(e);
        }
        Value& exits = v["exits"];
        exits.type = Value::Array;
        for (const auto& x : room.exits) {
            Value e;
            e["min"] = vec(x.min);
            e["max"] = vec(x.max);
            e["to"] = place(x.to);
            exits.array.push_back(e);
        }
        Value& actors = v["actors"];
        actors.type = Value::Array;
        for (const auto& a : room.actors) {
            NativeActor n;
            std::string why;
            if (!native_actor(a.actor, a.variant, a.item, a.flag, n, why)) continue;  // parse checked it
            Value e;
            e["tag"] = n.tag;
            e["name"] = n.name;
            e["params"] = Value(double(n.params));
            e["position"] = vec(a.position);
            e["angle"] = Value(a.angle);
            e["angle_x"] = Value(double(n.angle_x));
            e["angle_z"] = Value(double(n.angle_z));
            actors.array.push_back(e);
        }
        rooms.array.push_back(v);
    }
    Value& doors = r["doors"];
    doors.type = Value::Array;
    for (const auto& d : s.doors) {
        Value v;
        v["position"] = vec(d.position);
        v["angle"] = Value(d.angle);
        Value& rr = v["rooms"];
        rr.type = Value::Array;
        rr.array = {Value(double(d.a)), Value(double(d.b))};
        // the game's knob door (door10): type 4 a small-key lock, type 1 the boss door (its own model, door20),
        // type 0 with a switch the stop bars of a shutter; the switch keeps a locked door open once unlocked
        const uint32_t sw = d.switch_no & 0xFF;
        v["name"] = d.lock == "boss" ? "door20" : "door10";
        v["params"] = Value(double(d.lock == "small_key" ? 0x0FFFF000u | 4u << 8 | sw
                                   : d.lock == "boss"    ? 0x0FFFF000u | 1u << 8 | sw
                                   : d.lock == "shutter" ? 0x0FFFF000u | sw
                                                         : 0x0FFFF0FFu));
        doors.array.push_back(v);
    }
    Value& joins = r["joins"];
    joins.type = Value::Array;
    for (const auto& [a, b] : s.joins) {
        Value j;
        j.type = Value::Array;
        j.array = {Value(double(a)), Value(double(b))};
        joins.array.push_back(j);
    }
    return r;
}

// A box (its position is its centre bottom) holds a point.
inline bool inside_box(const Point& position, const Point& size, double x, double y, double z) {
    return std::fabs(x - position.x) <= size.x / 2 && std::fabs(z - position.z) <= size.z / 2 && y >= position.y - 1 && y <= position.y + size.y;
}
inline bool inside(const Entry& e, double x, double y, double z) { return inside_box(e.position, e.size, x, y, z); }
inline bool inside(const Warp& w, double x, double y, double z) { return inside_box(w.position, w.size, x, y, z); }

}  // namespace mod_stages
