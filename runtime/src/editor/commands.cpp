// World editor command layer (commands.h). No game dependencies.
#include "commands.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <system_error>

#include "../mods/story_mail_book.h"
#include "../mods/story_manifest.h"

namespace editor {
namespace {
namespace fs = std::filesystem;
using story_notebook::ManifestActor;

// ---------------------------------------------------------------- the command list
const std::vector<CommandInfo> kCommands = {
    {"help", "The command list with each command's parameters, or one command.", {{"command", "string", "a command name"}}},
    {"list_mods", "The active mods the editor can open (each with placed actors, models, scenes or quests).", {}},
    {"open_mod", "Opens a mod: later commands without \"mod\" act on it.", {{"mod", "string", "the mod's id", true}}},
    {"list_actors", "The open mod's placed actors (game actors and mod models).", {{"mod", "string", "another mod than the open one"}}},
    {"get_actor", "One placed actor as the manifest declares it.", {{"id", "string", "the actor's id", true}, {"mod", "string", ""}}},
    {"set_actor", "Sets one property of a placed actor (null removes it): position, rotation, scale, model, lines, collision, attention, animation, flag, stage, room, when, then, look_at, path, parameters, defeated_flag, broken_flag, drops, native_drops, combat, carry, switch_flag, switch, texture.",
     {{"id", "string", "the actor's id", true}, {"property", "string", "the manifest field", true}, {"value", "any", "the new value; null removes the field", true}, {"mod", "string", ""}},
     true},
    {"move_actor", "Moves a placed actor.", {{"id", "string", "", true}, {"position", "point", "{x, y, z} in world units", true}, {"mod", "string", ""}}, true},
    {"rotate_actor", "Turns a placed actor.", {{"id", "string", "", true}, {"rotation", "point", "{x, y, z} in degrees", true}, {"mod", "string", ""}}, true},
    {"scale_actor", "Scales a placed actor.", {{"id", "string", "", true}, {"scale", "point", "{x, y, z}, each above 0 and up to 10", true}, {"mod", "string", ""}}, true},
    {"set_dialogue", "Sets what a placed actor says (up to three lines).", {{"id", "string", "", true}, {"lines", "any", "a string or a list of up to three strings", true}, {"mod", "string", ""}}, true},
    {"list_models", "The open mod's models and the actors that use them.", {{"mod", "string", ""}}},
    {"list_scenes", "The open mod's scenes.", {{"mod", "string", ""}}},
    {"get_scene", "One scene as the manifest declares it.", {{"id", "string", "the scene's id", true}, {"mod", "string", ""}}},
    {"set_scene", "Sets one property of a scene (trigger or steps).", {{"id", "string", "", true}, {"property", "string", "trigger or steps", true}, {"value", "any", "", true}, {"mod", "string", ""}}, true},
    {"list_quests", "The open mod's quests and their steps.", {{"mod", "string", ""}}},
    {"set_quest_step", "Sets one property of a quest step (text, where, place, npc, available, sail_line, find...).",
     {{"quest", "string", "the quest's id", true}, {"step", "integer", "the step's n", true}, {"property", "string", "", true}, {"value", "any", "null removes the field", true}, {"mod", "string", ""}},
     true},
    {"undo", "Takes back the last change.", {}},
    {"redo", "Does the last undone change again.", {}},
    {"history", "The undo and redo steps.", {}},
    {"validate", "Checks the open mod as the mod manager does.", {{"mod", "string", ""}}},
    {"save_package", "Writes the open mod's manifest into its package folder (replaces manifest.json).", {{"mod", "string", ""}, {"confirm", "boolean", "must be true: the file is replaced", true}}, false, true},
    {"revert_package", "Forgets the unsaved changes (an undo step).", {{"mod", "string", ""}, {"confirm", "boolean", "must be true", true}}, true, true},
    {"reload_room", "Leaves and enters the current room again (Link stays where he is): every actor of the room is created again.", {}},
    {"list_locations", "Warp destinations: the islands, the game's stages and rooms, and the open mod's actors and quest places.", {{"group", "string", "Islands, Rooms or Quest"}}},
    {"warp", "Warps Link with the game's own stage change. Not during a cutscene, dialogue or menu.",
     {{"stage", "string", "the stage name (sea, M_NewD2, ...)", true}, {"room", "integer", "0..63", true}, {"point", "integer", "a spawn point; without it: the room's first spawn point, or \"position\""},
      {"position", "point", "stand here instead of at a spawn point"}, {"angle", "integer", "heading with position, the game's 16-bit angle"}}},
    {"warp_to", "Warps to an entry of list_locations.", {{"location", "string", "the location's id", true}}},
    {"warp_back", "Back to where Link was before the last warp.", {}},
    {"read_state", "Link's stage, room and position, the fly camera, test mode and the open mod's state.", {}},
    {"fly_camera", "The free camera: Link stays where he is and gets no input; fly with the mouse, WASD, Q/E, Shift (faster), or the controller's sticks and shoulder buttons.",
     {{"on", "boolean", "", true}, {"place_link", "boolean", "when turning it off: Link moves to the camera's position"}}},
    {"test_mode", "Test mode: in-game saves go to a separate test save, and the test commands are allowed.", {{"on", "boolean", "", true}}},
    {"reset_quest", "Test mode: erases the open mod's quest progress and flags from the loaded Quest Log (never another mod's).", {{"mod", "string", ""}, {"confirm", "boolean", "must be true", true}}, false, true},
    {"save_state", "Test mode: saves a test state (the game's progress, Link's place and the open mod's flags).", {{"slot", "integer", "1..3", true}}},
    {"load_state", "Test mode: loads a test state.", {{"slot", "integer", "1..3", true}}},
    {"command_log", "The last commands with their parameters and results.", {{"count", "integer", "how many (default 20)"}}},
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
    if (type == "array") return v.type == Value::Array;
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

const std::map<std::string, std::string>& stage_names() {
    static const std::map<std::string, std::string> names = {
        {"sea", "Great Sea"}, {"LinkRM", "Link's House"}, {"MajyuE", "Forsaken Fortress"}, {"M_NewD2", "Dragon Roost Cavern"},
        {"kindan", "Forbidden Woods"}, {"Siren", "Tower of the Gods"}, {"Asoko", "Tetra's Ship"}, {"M_Dai", "Earth Temple"},
        {"kaze", "Wind Temple"}, {"GanonK", "Ganon's Tower"}, {"Hyroom", "Hyrule Castle"}, {"Obombh", "Bomb-Master Cannon's house"},
    };
    return names;
}

}  // namespace

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

// ---------------------------------------------------------------- validation
bool validate_package(const std::string& mod, const Value& manifest, const std::vector<std::pair<std::string, Value>>& all,
                      std::string& why) {
    if (manifest.type != Value::Object) return why = "the manifest is not an object", false;
    std::vector<story_notebook::ManifestModel> models;
    std::vector<ManifestActor> actors;
    std::vector<story_notebook::ManifestScene> scenes;
    std::vector<story_notebook::ManifestQuest> quests;
    std::vector<story_notebook::ManifestItem> items;
    if (!story_notebook::parse_models(manifest, models, why) || !story_notebook::parse_actors(manifest, actors, why) ||
        !story_notebook::parse_scenes(manifest, scenes, why) || !story_notebook::parse_items(manifest, items, why) ||
        !story_notebook::parse_quests(manifest, quests, why))
        return false;
    std::vector<std::pair<std::string, Value>> world;
    for (const auto& [id, m] : all)
        if (id != mod && (m.get("actors").type != Value::Null || m.get("scenes").type != Value::Null)) world.push_back({id, m});
    world.push_back({mod, manifest});
    return story_notebook::world_manifest::validate_world_packages(world, why);
}

// ---------------------------------------------------------------- the sea and locations
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
                                      const std::vector<std::pair<std::string, Value>>& packages) {
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
                if (place.get("x").type != Value::Number || place.get("z").type != Value::Number) continue;
                const float x = float(place.get("x").number), z = float(place.get("z").number);
                const int room = sea_room(x, z);
                if (!room) continue;
                Location l;
                const int n = int(s.get("n").number);
                l.id = "step:" + mod + ":" + q.get("id").string() + ":" + std::to_string(n);
                l.label = q.get("id").string() + " step " + std::to_string(n) + "  (" + island_name(room) + ")";
                l.group = "Quest";
                l.stage = "sea";
                l.room = room;
                l.point = kAtPosition;
                l.position = {x, 2000, z};  // a step's place has no height: Link drops onto the ground
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
    }
    for (const auto& s : manifest.get("scenes").array)
        for (const auto& st : s.get("steps").array) actions(st.get("then"));
    for (const auto& q : manifest.get("quests").array) {
        const uint16_t key = letter_key(mod, q.get("id").string());  // story_notebook_list.h quest_key
        for (uint8_t kind : {kRecordQuest, kRecordTracked, kRecordCarried, kRecordQuestFlags, kRecordSailLine, kRecordQuestOrder}) add(kind, key);
        if (q.get("letter").get("id").type == Value::String) {
            add(kRecordLetter, letter_key(mod, q.get("letter").get("id").text));
            add(kRecordLetterEnclosures, letter_key(mod, q.get("letter").get("id").text));
        }
    }
    for (const auto& l : manifest.get("letters").array) {
        add(kRecordLetter, letter_key(mod, l.get("id").string()));
        add(kRecordLetterEnclosures, letter_key(mod, l.get("id").string()));
    }
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
    packages_.push_back({mod, name, dir, manifest, manifest});
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
bool Session::dirty(const std::string& mod) const {
    auto* p = find(mod);
    return p && !(p->manifest == p->saved);
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

Value Session::run(const std::string& command, const Value& params, bool log) {
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
    LogEntry e{++seq_, command, params, out};
    log_.push_back(std::move(e));
    while (log_.size() > kLogSize) log_.pop_front();
    return out;
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

bool Session::change(Package& p, const std::string& label, const std::function<bool(Value&, std::string&)>& edit, Value& result,
                     std::string& why) {
    Value next = p.manifest;
    if (!edit(next, why)) return false;
    if (next == p.manifest) {
        result["changed"] = false;
        return true;
    }
    auto all = all_manifests();
    if (!validate_package(p.mod, next, all, why) || !model_files(p.dir, next, why)) return false;
    const bool actors_changed = !(next.get("actors") == p.manifest.get("actors")) || !(next.get("models") == p.manifest.get("models"));
    const bool other_changed = !(next.get("scenes") == p.manifest.get("scenes")) || !(next.get("quests") == p.manifest.get("quests"));
    undo_.push_back({p.mod, label, p.manifest, next});
    if (undo_.size() > kUndoSize) undo_.erase(undo_.begin());
    redo_.clear();
    const Value before = std::move(p.manifest);
    p.manifest = std::move(next);
    result["changed"] = true;
    if (actors_changed) apply_live(p, before, result);
    if (other_changed) {
        result["live"] = false;
        result["note"] = "Scene and quest changes take effect when the game starts again (save the package first).";
    }
    return true;
}

bool Session::restore(const std::string& mod, const Value& manifest, Value& result, std::string& why) {
    auto* p = const_cast<Package*>(find(mod));
    if (!p) return why = "the mod of this step is gone", false;
    const bool actors_changed = !(manifest.get("actors") == p->manifest.get("actors")) || !(manifest.get("models") == p->manifest.get("models"));
    const Value before = p->manifest;
    p->manifest = manifest;
    result["mod"] = mod;
    if (actors_changed) apply_live(*p, before, result);
    return true;
}

std::vector<Location> Session::locations() const {
    if (stage_rooms_.empty() && host_) stage_rooms_ = host_->stage_rooms();
    std::vector<std::pair<std::string, Value>> open;
    if (auto* p = find(open_)) open.push_back({p->mod, p->manifest});
    return build_locations(stage_rooms_, open);
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
            for (const char* k : {"actors", "models", "scenes", "quests"}) v[k] = int(pk.manifest.get(k).array.size());
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
                                                      "when", "lines", "then", "defeated_flag", "broken_flag", "drops",
                                                      "native_drops", "combat", "carry", "switch_flag", "switch", "texture"};
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
        if (property != "trigger" && property != "steps") return why = "a scene's properties are trigger and steps", r;
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
        if (!restore(s.mod, command == "undo" ? s.before : s.after, r, why)) return r;
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
        const bool valid = validate_package(pkg->mod, pkg->manifest, all_manifests(), problem) && model_files(pkg->dir, pkg->manifest, problem);
        r["valid"] = valid;
        if (!valid) r["problem"] = problem;
        ok = true;
        return r;
    }
    if (command == "save_package") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        std::string problem;
        if (!validate_package(pkg->mod, pkg->manifest, all_manifests(), problem) || !model_files(pkg->dir, pkg->manifest, problem))
            return why = "not saved: " + problem, r;
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
        ok = true;
        return r;
    }
    if (command == "revert_package") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
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
            std::string busy;
            if (host_->busy(busy)) r["busy"] = busy;
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
        if (!host_->test_state(command == "save_state", slot, pkg->mod, own, why)) return r;
        r["requested"] = true;
        r["slot"] = slot;
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
            r.array.push_back(v);
        }
        ok = true;
        return r;
    }
    why = "command \"" + command + "\" is not implemented";
    return r;
}

}  // namespace editor
