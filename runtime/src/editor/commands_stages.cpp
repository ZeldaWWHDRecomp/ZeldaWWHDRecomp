// SPDX-License-Identifier: MPL-2.0
// World editor: the open mod's stages (docs/mod-stages.md): list them, add a stage or a room from a ground model
// in the package, set any of a stage's fields, check them (structured problems: the manifest's rules, rooms that
// can't be reached with the keys the stage gives, ground models without UVs or named materials), and rebuild
// them from the package's files (the stage Link is in loads again where he stands). Each change is one undo step;
// a change takes effect in the game with the next rebuild.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../mods/mod_stages_check.h"
#include "commands.h"

namespace editor {
namespace {
namespace fs = std::filesystem;
std::string text(const Value& p, const char* key) { return p.get(key).string(); }
Value* stage_by_id(Value& m, const std::string& id) {
    for (auto& v : m["stages"].array)
        if (v.get("id").string() == id) return &v;
    return nullptr;
}
Value point_value(double x, double y, double z) {
    Value v;
    v["x"] = std::round(x * 10) / 10;
    v["y"] = std::round(y * 10) / 10;
    v["z"] = std::round(z * 10) / 10;
    return v;
}
std::vector<uint8_t> read_bytes(const fs::path& p, size_t limit) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec) || fs::is_symlink(p, ec) || fs::file_size(p, ec) > limit) return {};
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}
// A ground model's bounds (game units), from its POSITION accessors' min and max (node transforms are not
// applied: a default spawn's guess, which add_stage and add_room report).
bool glb_bounds(const std::vector<uint8_t>& b, double lo[3], double hi[3]) {
    if (b.size() < 20 || std::memcmp(b.data(), "glTF", 4) != 0) return false;
    const uint32_t length = uint32_t(b[12]) | uint32_t(b[13]) << 8 | uint32_t(b[14]) << 16 | uint32_t(b[15]) << 24;
    if (20 + uint64_t(length) > b.size()) return false;
    Value doc;
    try {
        doc = mods::json::parse(std::string(reinterpret_cast<const char*>(b.data() + 20), length));
    } catch (...) {
        return false;
    }
    bool any = false;
    for (int k = 0; k < 3; ++k) lo[k] = 1e30, hi[k] = -1e30;
    for (const auto& m : doc.get("meshes").array)
        for (const auto& p : m.get("primitives").array) {
            const Value& at = p.get("attributes").get("POSITION");
            if (at.type != Value::Number || size_t(at.number) >= doc.get("accessors").array.size()) continue;
            const Value& acc = doc.get("accessors").array[size_t(at.number)];
            if (acc.get("min").array.size() != 3 || acc.get("max").array.size() != 3) continue;
            for (int k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], acc.get("min").array[k].number * 100);
                hi[k] = std::max(hi[k], acc.get("max").array[k].number * 100);
            }
            any = true;
        }
    return any;
}
// a ground path in the package: relative, "/"-separated, a .glb
bool ground_path(const std::string& g) { return mod_stages::package_glb(g); }
}  // namespace

Value Session::dispatch_stages(const std::string& command, const Value& p, std::string& why, bool& ok, bool& handled) {
    Value r;
    ok = false;
    handled = true;
    // a change of stage "stage": one undo step
    auto stage_edit = [&](const std::string& label, const std::function<bool(Value&, Value&, std::string&)>& set) {
        Package* pkg = package(p, why);
        if (!pkg) return;
        const std::string id = text(p, "stage");
        ok = change(*pkg, label, [&](Value& m, std::string& w) {
            Value* s = stage_by_id(m, id);
            if (!s) return w = "no stage \"" + id + "\" in " + pkg->mod + " (list_stages)", false;
            return set(m, *s, w);
        }, r, why);
        if (ok) {
            r["stage"] = id;
            r["next"] = "rebuild_stage to see the change in the game";
        }
    };
    // a room's spawn: the given one, else the middle of the ground model's bounds, on its lowest point
    auto spawn_for = [&](Package& pkg, const std::string& ground, Value& spawn, unsigned point) {
        if (p.get("spawn").type == Value::Object) {
            spawn["position"] = p.get("spawn");
        } else {
            double lo[3], hi[3];
            const auto bytes = read_bytes(fs::path(pkg.dir) / ground, 16u << 20);
            if (!bytes.empty() && glb_bounds(bytes, lo, hi)) {
                spawn["position"] = point_value((lo[0] + hi[0]) / 2, lo[1], (lo[2] + hi[2]) / 2);
                r["spawn_note"] = "the spawn is the middle of the model's bounds, on its lowest point: check it (or give \"spawn\")";
            } else {
                spawn["position"] = point_value(0, 0, 0);
                r["spawn_note"] = "the ground model's bounds are unknown: the spawn is at the origin (give \"spawn\")";
            }
        }
        spawn["point"] = double(point);
        spawn["angle"] = p.get("angle").type == Value::Number ? p.get("angle").number : 0.0;
    };

    if (command == "place_in_stage") {
        // what goes where Link (or the camera, or a position) is: in a room of the stage, or (an entry) in a game room
        Package* pkg = package(p, why);
        if (!pkg) return r;
        Place place;
        if (!where(p, place, why)) return r;
        const std::string what = text(p, "what"), id = text(p, "stage");
        std::string here_stage;  // the package's stage id Link is in ("" a game stage)
        if (host_)
            for (const auto& b : host_->mod_stages())
                if (b.name == place.stage && b.mod == pkg->mod) here_stage = b.id;
        // at = position: "stage" names the package's stage (its id), "room" one of its rooms
        if (p.get("at").string() == "position" && place.stage == id) here_stage = id;
        if (what != "entry" && here_stage != id)
            return why = "go into stage " + id + " first (warp with \"stage\": \"stage:" + pkg->mod + "/" + id + "\"), or give at = position with \"room\"", r;
        if (what == "entry" && !here_stage.empty()) return why = "an entry is in a room of the game (the way in): stand there first", r;
        const double yaw = place.angle * 360.0 / 65536.0;
        auto box = [&](double sx, double sy, double sz) {
            Value b;
            const double w = p.get("size").get("x").type == Value::Number ? p.get("size").get("x").number : sx;
            const double h = p.get("size").get("y").type == Value::Number ? p.get("size").get("y").number : sy;
            const double d = p.get("size").get("z").type == Value::Number ? p.get("size").get("z").number : sz;
            b["min"] = point_value(place.position.x - w / 2, place.position.y - 50, place.position.z - d / 2);
            b["max"] = point_value(place.position.x + w / 2, place.position.y - 50 + h, place.position.z + d / 2);
            return b;
        };
        const std::string room = std::to_string(place.room);
        stage_edit("Place " + what, [&](Value&, Value& s, std::string& w) {
            auto list = [&](Value& parent, const char* key) -> Value& {
                Value& l = parent[key];
                if (l.type != Value::Array) l.type = Value::Array;
                return l;
            };
            Value* rv = nullptr;
            for (auto& v : s["rooms"].array)
                if (v.get("room").number == place.room) rv = &v;
            if (what != "entry" && !rv) return w = "the stage has no room " + room, false;
            const std::string item_id = p.get("id").type == Value::String ? p.get("id").text : what + "-" + std::to_string(int(std::fabs(place.position.x)) % 10000);
            if (what == "spawn") {
                Value sp;
                unsigned point = 0;
                for (const auto& v : s["rooms"].array)
                    for (const auto& q : v.get("spawns").array) point = std::max(point, unsigned(q.get("point").number) + 1);
                if (p.get("point").type == Value::Number) {
                    for (auto& q : list(*rv, "spawns").array)
                        if (q.get("point").number == p.get("point").number) {
                            q["position"] = point_value(place.position.x, place.position.y, place.position.z), q["angle"] = std::round(yaw);
                            r["moved"] = q;
                            return true;
                        }
                    point = unsigned(p.get("point").number);
                }
                sp["point"] = double(point);
                sp["position"] = point_value(place.position.x, place.position.y, place.position.z);
                sp["angle"] = std::round(yaw);
                list(*rv, "spawns").array.push_back(sp);
                r["added"] = sp;
            } else if (what == "exit") {
                if (p.get("to").type != Value::Object) return w = "an exit needs \"to\": a place of the game {stage, room, point}, or {room, point} of this stage", false;
                Value e;
                e["id"] = item_id;
                e["box"] = box(400, 200, 400);
                e["to"] = p.get("to");
                list(*rv, "exits").array.push_back(e);
                r["added"] = e;
                r["note"] = "the floor inside the box takes Link out: keep it on floor (rebuild_stage reports a box on no floor)";
            } else if (what == "door") {
                if (p.get("to_room").type != Value::Number) return w = "a door needs \"to_room\": the room on its other side", false;
                Value d;
                d["position"] = point_value(place.position.x, place.position.y, place.position.z);
                d["angle"] = std::round(yaw);
                d["rooms"].type = Value::Array;
                d["rooms"].array = {Value(double(place.room)), p.get("to_room")};
                if (p.get("lock").type == Value::String) d["lock"] = p.get("lock").text;
                if (p.get("switch").type == Value::Number) d["switch"] = p.get("switch").number;
                list(s, "doors").array.push_back(d);
                r["added"] = d;
                r["note"] = "the door stands where Link stands, facing the way he faces: leave a 3 m wide, 3 m high opening in both rooms there";
            } else if (what == "warp") {
                Value wv;
                wv["id"] = item_id;
                wv["room"] = double(place.room);
                wv["position"] = point_value(place.position.x, place.position.y, place.position.z);
                wv["size"] = point_value(200, 200, 200);
                if (p.get("to").type == Value::Object) wv["to"] = p.get("to");
                if (p.get("after").type == Value::String) wv["after"] = p.get("after").text;  // a chest of the stage that opens it
                list(s, "warps").array.push_back(wv);
                r["added"] = wv;
            } else if (what == "entry") {
                Value e;
                e["id"] = item_id;
                e["stage"] = place.stage;
                e["room"] = double(place.room);
                e["position"] = point_value(place.position.x, place.position.y, place.position.z);
                e["size"] = point_value(200, 200, 200);
                e["to"]["room"] = p.get("to_room").type == Value::Number ? p.get("to_room").number : 0.0;
                e["to"]["point"] = p.get("to_point").type == Value::Number ? p.get("to_point").number : 0.0;
                if (p.get("style").type == Value::String) e["style"] = p.get("style").text;  // walk or updraft
                if (p.get("height").type == Value::Number) e["height"] = p.get("height").number;
                if (p.get("size").type == Value::Object) e["size"] = p.get("size");
                else if (e.get("style").string() == "updraft") e["size"] = point_value(400, 800, 400);  // a column of wind
                list(s, "entries").array.push_back(e);
                r["added"] = e;
            } else if (what == "chest" || what == "enemy" || what == "pot" || what == "big_pot") {
                Value a;
                a["id"] = item_id;
                a["actor"] = what == "enemy" ? (p.get("actor").type == Value::String ? p.get("actor").text : std::string("bokoblin")) : what;
                if (p.get("variant").type == Value::String) a["variant"] = p.get("variant").text;
                if (what == "chest") a["item"] = p.get("item").type == Value::String ? p.get("item").text : std::string("small_key");
                // a chest or pot 1.5 m in front of Link, facing him (he opens a chest from its front); an enemy 3 m
                const double ahead = what == "enemy" ? 300 : 150;
                a["position"] = point_value(place.position.x + std::sin(yaw * 3.14159265358979 / 180) * ahead, place.position.y,
                                            place.position.z + std::cos(yaw * 3.14159265358979 / 180) * ahead);
                a["angle"] = std::round(std::fmod(yaw + 180, 360));
                list(*rv, "actors").array.push_back(a);
                r["added"] = a;
            } else
                return w = "\"what\" is spawn, exit, door, warp, entry, chest, enemy, pot or big_pot", false;
            return true;
        });
        return r;
    }
    if (command == "import_ground") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        if (pkg->dir.empty()) return why = "the mod has no package folder", r;
        const fs::path from = fs::u8path(text(p, "file"));
        const auto bytes = read_bytes(from, 16u << 20);
        if (bytes.size() < 20 || std::memcmp(bytes.data(), "glTF", 4) != 0) return why = "\"file\" is a .glb (glTF 2.0) file of up to 16 MiB", r;
        std::string as = text(p, "as");
        if (as.empty()) {
            std::string stem;
            for (char c : from.stem().string()) stem += (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') ? c : '-';
            as = "assets/" + (stem.empty() ? std::string("room") : stem.substr(0, 60)) + ".glb";
        }
        if (!ground_path(as)) return why = "\"as\" is a .glb path in the package (assets/hall.glb)", r;
        const fs::path to = fs::u8path(pkg->dir) / fs::u8path(as);
        std::error_code ec;
        if (fs::exists(to, ec)) {
            const auto old = read_bytes(to, 16u << 20);
            if (old != bytes && !p.get("confirm").boolean) return why = "the package has a different " + as + ": \"confirm\": true replaces it", r;
        }
        fs::create_directories(to.parent_path(), ec);
        {
            std::ofstream out(to, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
            if (!out) return why = "cannot write " + as + " into the package", r;
        }
        mod_stages::GlbFacts facts;
        r["ground"] = as;
        r["bytes"] = double(bytes.size());
        if (mod_stages::glb_facts(bytes, facts)) {
            r["visible_parts"] = double(facts.visible_primitives);
            r["parts_without_uvs"] = double(facts.without_uvs);
            Value& mats = r["materials"];
            mats.type = Value::Array;
            for (const auto& m : facts.materials) mats.array.push_back(Value(m));
        }
        r["next"] = "add_stage or add_room with \"ground\": \"" + as + "\" (or rebuild_stage when a room uses it already)";
        ok = true;
        return r;
    }
    if (command == "list_stage_templates") {
        if (!host_) return why = "the game is not running", r;
        const Value all = host_->stage_templates(why);
        if (!why.empty()) return r;
        const std::string door = text(p, "door");
        if (!door.empty() && door != "knob" && door != "boss" && door != "small_key" && door != "shutter")
            return why = "\"door\" is knob, boss, small_key or shutter", r;
        Value& list = r["templates"];
        list.type = Value::Array;
        for (const auto& t : all.array) {
            if (p.get("sky").type == Value::Bool && t.get("sky").boolean != p.get("sky").boolean) continue;
            if (!door.empty() && std::none_of(t.get("doors").array.begin(), t.get("doors").array.end(), [&](const Value& d) { return d.string() == door; })) continue;
            list.array.push_back(t);
        }
        r["note"] = "a template gives the stage its light, sky, camera and door models; a room's kit ({stage, room}) can be any room listed here. "
                    "With a sky, the stage's open-air rooms show it (an enclosed model hides it).";
        ok = true;
        return r;
    }
    if (command == "list_stages") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        std::vector<mod_stages::Stage> stages;
        if (!mod_stages::parse(pkg->manifest, stages, why)) return r;
        std::vector<Host::ModStage> built;
        if (host_) built = host_->mod_stages();
        Value& list = r["stages"];
        list.type = Value::Array;
        for (const auto& s : stages) {
            Value v;
            v["id"] = s.id;
            v["template"] = s.template_stage;
            v["kind"] = s.kind;
            v["map"] = s.map;
            v["save_group"] = s.save_group;
            for (const auto& b : built)
                if (b.mod == pkg->mod && b.id == s.id) v["built"] = true, v["game_name"] = b.name;
            if (v.get("built").type == Value::Null) v["built"] = false;
            Value& rooms = v["rooms"];
            rooms.type = Value::Array;
            for (const auto& room : s.rooms) {
                Value rv;
                rv["room"] = double(room.room);
                rv["ground"] = room.kit_ground ? std::string("kit") : room.ground;
                rv["kit"] = (room.kit_stage.empty() ? s.template_stage : room.kit_stage) + " room " + std::to_string(room.kit);
                rv["floor"] = double(room.floor);
                Value& spawns = rv["spawns"];
                spawns.type = Value::Array;
                for (const auto& sp : room.spawns) spawns.array.push_back(Value(double(sp.point)));
                rv["exits"] = double(room.exits.size());
                rv["actors"] = double(room.actors.size());
                rooms.array.push_back(rv);
            }
            v["doors"] = double(s.doors.size());
            v["entries"] = double(s.entries.size());
            v["warp"] = "warp with {\"stage\": \"stage:" + pkg->mod + "/" + s.id + "\", \"room\": 0, \"point\": " +
                        (s.rooms.empty() || s.rooms[0].spawns.empty() ? std::string("0") : std::to_string(s.rooms[0].spawns[0].point)) + "}";
            list.array.push_back(v);
        }
        ok = true;
        return r;
    }
    if (command == "add_stage") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const std::string id = text(p, "id"), ground = text(p, "ground");
        if (!ground_path(ground)) return why = "\"ground\" is a .glb file in the package (a path such as assets/hall.glb)", r;
        ok = change(*pkg, "Add stage " + id, [&](Value& m, std::string& w) {
            if (stage_by_id(m, id)) return w = "the package has a stage \"" + id + "\" already", false;
            Value s;
            s["id"] = id;
            s["template"] = p.get("template").type == Value::String ? p.get("template").text : std::string("Cave01");
            s["kind"] = p.get("kind").type == Value::String ? p.get("kind").text : std::string("dungeon");
            if (p.get("music_like").type == Value::String) s["music_like"] = p.get("music_like").text;
            if (p.get("title").type == Value::String) s["map"]["title"] = p.get("title").text;
            if (p.get("entrance").type == Value::Object) s["entrance"] = p.get("entrance");
            else {
                s["entrance"]["stage"] = "sea";
                s["entrance"]["room"] = 34.0;
                s["entrance"]["point"] = 1.0;
                r["entrance_note"] = "the entrance (where a save made in the stage continues without the package) is sea room 34 point 1: set it with set_stage_field";
            }
            Value room;
            room["room"] = 0.0;
            room["kit"] = p.get("kit").type == Value::Number ? p.get("kit").number : 0.0;
            room["ground"] = ground;
            if (p.get("materials").type == Value::Object) room["materials"] = p.get("materials");
            if (p.get("material").type == Value::String) room["material"] = p.get("material").text;
            Value spawn;
            spawn_for(*pkg, ground, spawn, 0);
            room["spawns"].type = Value::Array;
            room["spawns"].array.push_back(spawn);
            s["rooms"].type = Value::Array;
            s["rooms"].array.push_back(room);
            if (p.get("entry").type == Value::Object) {
                Value e = p.get("entry");
                if (e.get("id").type == Value::Null) e["id"] = "way-in";
                if (e.get("size").type == Value::Null) e["size"] = point_value(200, 200, 200);
                if (e.get("to").type == Value::Null) e["to"]["room"] = 0.0, e["to"]["point"] = 0.0;
                s["entries"].type = Value::Array;
                s["entries"].array.push_back(e);
            }
            m["stages"].type = Value::Array;
            m["stages"].array.push_back(s);
            return true;
        }, r, why);
        if (ok) {
            r["stage"] = id;
            r["next"] = "rebuild_stage, then warp with {\"stage\": \"stage:" + pkg->mod + "/" + id + "\", \"room\": 0, \"point\": 0}";
        }
        return r;
    }
    if (command == "add_room") {
        const bool kit = text(p, "ground") == "kit";
        const std::string ground = text(p, "ground");
        if (!kit && !ground_path(ground)) return why = "\"ground\" is a .glb file in the package, or \"kit\" (the kit room as it is)", r;
        Package* pkg = package(p, why);
        if (!pkg) return r;
        stage_edit("Add a room", [&](Value&, Value& s, std::string& w) {
            Value& rooms = s["rooms"];
            const unsigned number = unsigned(rooms.array.size());
            unsigned point = 0;  // the next free spawn point of the stage
            for (const auto& room : rooms.array)
                for (const auto& sp : room.get("spawns").array) point = std::max(point, unsigned(sp.get("point").number) + 1);
            Value room;
            room["room"] = double(number);
            room["ground"] = ground;
            if (p.get("kit").type != Value::Null) room["kit"] = p.get("kit");
            else if (kit) return w = "a kit room as it is needs \"kit\" (a room number of the template, or {\"stage\", \"room\"})", false;
            if (p.get("floor").type == Value::Number) room["floor"] = p.get("floor").number;
            if (p.get("materials").type == Value::Object) room["materials"] = p.get("materials");
            if (p.get("material").type == Value::String) room["material"] = p.get("material").text;
            Value spawn;
            if (kit && p.get("spawn").type != Value::Object) return w = "a kit room needs \"spawn\" (a position on its floor)", false;
            spawn_for(*pkg, kit ? std::string() : ground, spawn, point);
            room["spawns"].type = Value::Array;
            room["spawns"].array.push_back(spawn);
            rooms.array.push_back(room);
            r["room"] = double(number);
            r["note"] = "join it to a room with a door (set_stage_field \"doors\") or an open passage (\"joins\"): Check refuses a room nothing leads to";
            return true;
        });
        return r;
    }
    if (command == "set_stage_field") {
        const std::string field = text(p, "field");
        if (field.empty()) return why = "\"field\": the field's path in the stage, such as map, entrance, doors, joins, entries, warps, rooms/1/exits, rooms/0/actors", r;
        stage_edit("Set " + field, [&](Value&, Value& s, std::string& w) {
            // walk "a/b/3/c": object keys and list indices; the last part is set (or removed with null)
            std::vector<std::string> parts;
            for (size_t at = 0; at <= field.size();) {
                const size_t end = field.find('/', at);
                parts.push_back(field.substr(at, end == std::string::npos ? std::string::npos : end - at));
                if (end == std::string::npos) break;
                at = end + 1;
            }
            if (parts.empty() || parts.front() == "id") return w = "a stage's id can't be changed (its saves are kept by it)", false;
            Value* at = &s;
            for (size_t i = 0; i + 1 < parts.size(); ++i) {
                const std::string& k = parts[i];
                if (at->type == Value::Array) {
                    char* end = nullptr;
                    const unsigned long n = std::strtoul(k.c_str(), &end, 10);
                    if (k.empty() || *end || n >= at->array.size()) return w = "\"" + field + "\": no item " + k, false;
                    at = &at->array[n];
                } else if (at->type == Value::Object) {
                    if (!at->object.count(k)) return w = "\"" + field + "\": no field " + k, false;
                    at = &(*at)[k];
                } else
                    return w = "\"" + field + "\": " + k + " is not a list or an object", false;
            }
            const std::string& last = parts.back();
            const Value& value = p.get("value");
            if (at->type == Value::Array) {
                char* end = nullptr;
                const unsigned long n = std::strtoul(last.c_str(), &end, 10);
                if (last.empty() || *end || n > at->array.size()) return w = "\"" + field + "\": no item " + last, false;
                if (value.type == Value::Null) {
                    if (n == at->array.size()) return w = "\"" + field + "\": no item " + last, false;
                    at->array.erase(at->array.begin() + std::ptrdiff_t(n));
                } else if (n == at->array.size())
                    at->array.push_back(value);  // one past the end adds
                else
                    at->array[n] = value;
                return true;
            }
            if (at->type != Value::Object) return w = "\"" + field + "\": its parent is not an object", false;
            if (value.type == Value::Null) at->object.erase(last);
            else (*at)[last] = value;
            return true;
        });
        return r;
    }
    if (command == "remove_stage") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const std::string id = text(p, "stage");
        ok = change(*pkg, "Remove stage " + id, [&](Value& m, std::string& w) {
            auto& list = m["stages"].array;
            const auto it = std::find_if(list.begin(), list.end(), [&](const Value& v) { return v.get("id").string() == id; });
            if (it == list.end()) return w = "no stage \"" + id + "\"", false;
            list.erase(it);
            if (list.empty()) m.object.erase("stages");
            return true;
        }, r, why);
        return r;
    }
    if (command == "check_stage") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const std::string only = text(p, "stage");
        const auto problems = mod_stages::check(pkg->manifest, [&](const std::string& path) { return read_bytes(fs::path(pkg->dir) / path, 16u << 20); }, only);
        Value& list = r["problems"];
        list.type = Value::Array;
        unsigned errors = 0;
        for (const auto& pr : problems) {
            Value v;
            v["severity"] = pr.severity;
            if (!pr.stage.empty()) v["stage"] = pr.stage;
            if (!pr.field.empty()) v["field"] = pr.field;
            v["message"] = pr.message;
            errors += pr.severity == "error";
            list.array.push_back(v);
        }
        r["errors"] = double(errors);
        r["warnings"] = double(problems.size() - errors);
        r["note"] = "the builder's own checks (kit materials, exit boxes on floor, budgets) run with rebuild_stage";
        ok = true;
        return r;
    }
    if (command == "rebuild_stage") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        if (!host_) return why = "the game is not running", r;
        Value results = host_->rebuild_stages(pkg->mod, pkg->manifest, pkg->dir, text(p, "stage"), why);
        if (!why.empty()) return r;
        r["stages"] = results;
        bool all = true;
        for (const auto& v : results.array) all &= v.get("ok").boolean;
        r["ok"] = all;
        r["note"] = "a rebuilt stage loads again where Link stands when he is in it; otherwise it is used from his next way in";
        ok = true;
        return r;
    }
    if (command == "reset_stage_save") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        if (!host_) return why = "the game is not running", r;
        if (!host_->test_mode()) return why = "only in test mode (test_mode on): it changes the loaded Quest Log", r;
        std::vector<mod_stages::Stage> stages;
        if (!mod_stages::parse(pkg->manifest, stages, why)) return r;
        const std::string only = text(p, "stage");
        Value& groups = r["save_groups"];
        groups.type = Value::Array;
        for (const auto& s : stages) {
            if (!only.empty() && s.id != only) continue;
            if (std::find_if(groups.array.begin(), groups.array.end(), [&](const Value& g) { return g.string() == s.save_group; }) != groups.array.end()) continue;
            if (!host_->reset_stage_save(pkg->mod, s.save_group, why)) return r;
            groups.array.push_back(Value(s.save_group));
        }
        ok = true;
        return r;
    }
    handled = false;
    return r;
}

}  // namespace editor
