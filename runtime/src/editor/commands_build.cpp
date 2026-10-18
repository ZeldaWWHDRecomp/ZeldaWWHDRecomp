// World editor command layer: building new content (commands.h). No game dependencies: the game's side
// (ground checks, model preparation, the message box, Play) is reached through Host.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>

#include "../mods/skinned_model_validation.h"
#include "../mods/static_model_validation.h"
#include "../mods/story_manifest.h"
#include "commands.h"
#include "quests.h"

namespace editor {
namespace {
namespace fs = std::filesystem;
constexpr double kPi = 3.14159265358979323846;
// paths are UTF-8 text in the commands (Windows needs the conversion)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
fs::path utf8_path(const std::string& s) { return fs::u8path(s); }
#pragma GCC diagnostic pop

std::string text(const Value& p, const char* key) { return p.get(key).string(); }
Value* by_id(Value& list, const std::string& id) {
    if (list.type != Value::Array) return nullptr;
    for (auto& v : list.array)
        if (v.get("id").type == Value::String && v.get("id").text == id) return &v;
    return nullptr;
}
const Value* by_id(const Value& list, const std::string& id) { return by_id(const_cast<Value&>(list), id); }
bool read_point(const Value& v, WorldPoint& p) {
    if (v.type != Value::Object) return false;
    for (const char* k : {"x", "y", "z"})
        if (v.get(k).type != Value::Number || !std::isfinite(v.get(k).number)) return false;
    p = {float(v.get("x").number), float(v.get("y").number), float(v.get("z").number)};
    return true;
}
Value array() {
    Value v;
    v.type = Value::Array;
    return v;
}
// rounded to a tenth: positions from the game's floats stay readable in the manifest
double tenth(double v) { return std::round(v * 10) / 10; }
Value rounded(const WorldPoint& p) {
    Value v;
    v["x"] = tenth(p.x);
    v["y"] = tenth(p.y);
    v["z"] = tenth(p.z);
    return v;
}
bool id_ok(const std::string& id) {
    if (id.empty() || id.size() > 32) return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
    return true;
}
std::string slug(std::string s) {
    std::string out;
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.size() > 24) out.resize(24);
    return out.empty() ? std::string("model") : out;
}
// which field a validator message is about (for the problem list)
std::string field_of(const std::string& why) {
    static const std::pair<const char*, const char*> words[] = {
        {"position", "position"}, {"rotation", "rotation"}, {"scale", "scale"},     {"collision", "collision"}, {"look_at", "look_at"},
        {"path", "path"},         {"animation", "animation"}, {"model", "model"},   {"defeated_flag", "defeated_flag"},
        {"broken_flag", "broken_flag"}, {"switch_flag", "switch_flag"}, {"door", "switch"}, {"drop", "drops"}, {"combat", "combat"},
        {"carry", "carry"},       {"texture", "texture"},   {"dialogue", "lines"},  {"lines", "lines"},         {"then", "then"},
        {"action", "then"},       {"flag", "flag"},         {"stage", "stage"},     {"room", "room"},           {"resident", "parameters"},
        {"params", "parameters"}, {"parameter", "parameters"}};
    for (const auto& [word, field] : words)
        if (why.find(word) != std::string::npos) return field;
    return "";
}
// a validator message in words for people
std::string model_message(const std::string& why) {
    static const std::pair<const char*, const char*> kinds[] = {
        {"oversized", "Size: the file is larger than a model may be (8 MB) or is not a GLB 2.0 file."},
        {"texture exceeds", "Texture: the texture is larger than 512 x 512 pixels."},
        {"PNG", "Texture: the texture must be an embedded PNG (RGB or RGBA, not interlaced)."},
        {"base colour", "Material: the model needs one material with an embedded base colour PNG on UV0."},
        {"material", "Material: one simple material with a base colour texture, no extensions."},
        {"skin joints", "Bones: a skinned model has 1 to 32 bones."},
        {"bone", "Bones: bone names are letters, digits and _ (up to 31), each once."},
        {"joint", "Bones: the skeleton must be one connected hierarchy with unit-scale joints."},
        {"clips allowed", "Clips: a model has at most eight animation clips."},
        {"clip", "Clips: clips need distinct lower-case names and at most 900 frames each."},
        {"indices", "Triangles: up to 8192 triangles and 4096 vertices, as plain triangles."},
        {"triangles", "Triangles: plain triangles only (no morph targets or extensions)."},
        {"mesh", "Mesh: one mesh, used once in the default scene."},
        {"extensions", "Extensions: glTF extensions are not supported."}};
    for (const auto& [word, text] : kinds)
        if (why.find(word) != std::string::npos) return std::string(text) + " (" + why + ")";
    return "The model can't be used: " + why + ".";
}
bool game_file(const fs::path& p) {
    static const std::set<std::string> kinds = {".szs", ".arc", ".rarc", ".bfres", ".rpx", ".rpl", ".pack", ".bflim", ".bntx", ".sarc",
                                                ".yaz0", ".wux", ".wud", ".iso", ".rvz", ".gcm", ".bfsar", ".bars", ".bfstm", ".msbt"};
    std::string ext = p.extension().string();
    for (char& c : ext) c = char(std::tolower((unsigned char)c));
    return kinds.contains(ext);
}
}  // namespace

// ---------------------------------------------------------------- actor types
const std::vector<ActorType>& actor_types() {
    static const std::vector<ActorType> types = {
        {"sign", "Sign", "Game actors", "A sign with up to three lines; its flag is set when read."},
        {"resident", "Resident", "Game actors", "A villager who talks: uo2, um1 or the seated ym1 (options.resident)."},
        {"chest", "Chest", "Game actors", "A treasure chest; its flag is set when opened."},
        {"pot", "Pot", "Props", "A pot to lift and break; its broken flag and drops.", false, false, false},
        {"barrel", "Barrel", "Props", "A barrel to lift and break.", false, false, false},
        {"crate", "Crate", "Props", "A crate to break.", false, false, false},
        {"switch", "Switch", "Switches", "A floor switch; sets its switch flag when pressed.", false, false, false},
        {"door", "Door", "Switches", "A door opened by one of the mod's switches (options.switch).", false, false, false},
        {"chuchu", "ChuChu", "Enemies", "The game's ChuChu; its defeated flag and drops.", false, false, false},
        {"bokoblin", "Bokoblin", "Enemies", "The game's Bokoblin.", false, false, false},
        {"moblin", "Moblin", "Enemies", "The game's Moblin.", false, false, false},
        {"keese", "Keese", "Enemies", "The game's Keese.", false, false, false},
        {"model", "Your model", "Your models", "One of the mod's models (static or skinned), with collision.", true, false, true},
        {"model_enemy", "Your enemy", "Your models", "A skinned model that fights (needs flinch, stun, death and idle clips).", true, true, false},
        {"model_carry", "Carried prop", "Your models", "A model Link can lift and carry.", true, false, false},
    };
    return types;
}

std::string free_id(const Value& list, const std::string& base) {
    if (!by_id(list, base)) return base;
    for (int n = 2;; n++) {
        const std::string id = base + "-" + std::to_string(n);
        if (!by_id(list, id)) return id;
    }
}

bool new_actor(const std::string& type, const std::string& id, const Place& place, const std::string& model, const Value& manifest,
               const Value& options, Value& a, std::string& why) {
    const auto& types = actor_types();
    const auto t = std::find_if(types.begin(), types.end(), [&](const ActorType& x) { return x.type == type; });
    if (t == types.end()) return why = "unknown actor type \"" + type + "\" (list_actor_types)", false;
    if (!id_ok(id)) return why = "an actor id is 1 to 32 lower-case letters, digits, '-', '_' or '.'", false;
    if (!place.valid) return why = "no place to put it (Link is not in the scene)", false;
    a = Value();
    a["id"] = id;
    a["stage"] = place.stage;
    a["room"] = place.room;
    a["position"] = rounded(place.position);
    const double yaw = tenth(std::fmod(place.angle * 360.0 / 65536.0 + 360.0, 360.0));
    if (yaw != 0) a["rotation"] = rounded({0, float(yaw), 0});
    auto lines = [&](const char* fallback) {
        if (options.get("lines").type == Value::Array || options.get("lines").type == Value::String) a["lines"] = options.get("lines");
        else {
            a["lines"] = array();
            a["lines"].array.push_back(Value(std::string(fallback)));
        }
    };
    if (t->needs_model) {
        const Value* m = by_id(manifest.get("models"), model);
        if (model.empty() || !m) return why = "a " + t->label + " needs one of the mod's models (\"model\"; import_model adds one)", false;
        if (t->skinned && m->get("kind").string() != "skinned") return why = "a " + t->label + " needs a skinned model", false;
        a["kind"] = "mod";
        a["model"] = model;
        Value collision;
        collision["radius"] = 30;
        collision["height"] = type == "model_carry" ? 60 : 100;
        collision["mode"] = "solid";
        a["collision"] = collision;
        if (type == "model_enemy") {
            a["animation"] = "idle";
            a["combat"] = mods::json::parse(
                R"({"health": 4, "invulnerability_frames": 15, "flinch": {"clip": "flinch", "frames": 12, "knockback_speed": 180, "knockback_frames": 6},
                    "stun": {"clip": "stun", "frames": 30}, "death": {"clip": "death", "frames": 30},
                    "attack": {"damage": 1, "radius": 55, "height": 100, "forward_offset": 55, "y_offset": 0, "active_frames": 8, "cooldown_frames": 45},
                    "target_priority": "normal"})");
            a["defeated_flag"] = id + "-defeated";
            a["drops"] = array();
        } else if (type == "model_carry") {
            Value carry;
            carry["break_on_impact"] = false;
            a["carry"] = carry;
        }
        return true;
    }
    a["actor"] = type;
    if (type == "sign") {
        a["flag"] = id + "-read";
        lines("A new sign.");
    } else if (type == "resident") {
        Value params;
        params["resident"] = options.get("resident").type == Value::String ? options.get("resident").text : std::string("uo2");
        a["parameters"] = params;
        lines("Hello!");
    } else if (type == "chest")
        a["flag"] = id + "-opened";
    else if (type == "pot" || type == "barrel" || type == "crate") {
        a["broken_flag"] = id + "-broken";
        a["drops"] = array();
    } else if (type == "switch")
        a["switch_flag"] = id + "-pressed";
    else if (type == "door") {
        std::string sw = options.get("switch").string();
        if (sw.empty())
            for (const auto& other : manifest.get("actors").array)
                if (other.get("actor").string() == "switch") {
                    sw = other.get("id").string();
                    break;
                }
        if (sw.empty()) return why = "a door opens with one of the mod's switches: add a switch first (or name it in options.switch)", false;
        a["switch"] = sw;
    } else {  // the enemies
        a["defeated_flag"] = id + "-defeated";
        a["drops"] = array();
    }
    return true;
}

Value duplicate_actor(const Value& a, const std::string& id) {
    Value out = a;
    const std::string old = a.get("id").string();
    out["id"] = id;
    for (const char* key : {"flag", "broken_flag", "defeated_flag", "switch_flag"}) {
        if (out.get(key).type != Value::String) continue;
        const std::string f = out.get(key).text;
        out[key] = f.rfind(old, 0) == 0 ? id + f.substr(old.size()) : f + "-" + id;
    }
    return out;
}

// ---------------------------------------------------------------- models
bool check_model_file(const std::string& path, Value& info, std::string& why) {
    std::error_code ec;
    const fs::path file = utf8_path(path);
    if (!fs::is_regular_file(file, ec)) return why = "no file " + path, false;
    std::string ext = file.extension().string();
    for (char& c : ext) c = char(std::tolower((unsigned char)c));
    if (ext != ".glb") return why = "a model is a .glb file (glTF 2.0, binary)", false;
    const auto size = fs::file_size(file, ec);
    if (ec || size > static_models::kMaxSourceBytes) {
        char mb[32];
        std::snprintf(mb, sizeof mb, "%.1f", double(size) / (1024 * 1024));
        return why = std::string("Size: the file is ") + mb + " MB; a model can be up to 8 MB.", false;
    }
    std::ifstream f(file, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.size() != size) return why = "cannot read " + path, false;
    info = Value();
    info["size"] = double(size);
    static_models::ModelInfo s;
    std::string problem;
    if (static_models::validate_glb(bytes, s, problem)) {
        info["kind"] = "static";
        info["vertices"] = double(s.vertex_count);
        info["triangles"] = double(s.triangle_count);
        info["texture"] = std::to_string(s.texture_width) + "x" + std::to_string(s.texture_height);
        info["bones"] = 0;
        info["clips"] = array();
        return true;
    }
    if (problem.find("skins, animations") == std::string::npos) return why = model_message(problem), false;
    skinned_models::ModelInfo k;
    if (!skinned_models::validate_glb(bytes, k, problem)) return why = model_message(problem), false;
    info["kind"] = "skinned";
    info["vertices"] = double(k.vertex_count);
    info["triangles"] = double(k.triangle_count);
    info["texture"] = std::to_string(k.texture_width) + "x" + std::to_string(k.texture_height);
    info["bones"] = double(k.joints);
    info["clips"] = array();
    for (const auto& c : k.clips) info["clips"].array.push_back(Value(c.name));
    return true;
}

// ---------------------------------------------------------------- problems and export
std::vector<Problem> package_problems(const std::string& mod, const Value& manifest, const std::vector<std::pair<std::string, Value>>& all,
                                      const std::string& dir) {
    std::vector<Problem> out;
    auto add = [&](Problem p) {
        for (const auto& q : out)
            if (q.message == p.message && q.kind == p.kind && q.id == p.id) return;
        out.push_back(std::move(p));
    };
    std::string why;
    // scene actions name other scenes: left out where a check keeps only some scenes
    auto without_scene_actions = [](Value& list) {
        for (auto& item : list.array) {
            auto strip = [](Value& then) {
                if (then.type != Value::Array) return;
                Value kept = array();
                for (const auto& t : then.array)
                    if (t.get("action").string() != "scene") kept.array.push_back(t);
                then = kept;
            };
            strip(item["then"]);
            if (item.get("then").array.empty()) item.object.erase("then");
            if (item.get("steps").type == Value::Array)
                for (auto& step : item["steps"].array) {
                    strip(step["then"]);
                    if (step.get("then").array.empty()) step.object.erase("then");
                }
        }
    };
    // the model files first: a missing or unusable file is always a problem
    std::error_code ec;
    const fs::path root = dir.empty() ? fs::path() : fs::weakly_canonical(utf8_path(dir), ec);
    for (const auto& m : manifest.get("models").array) {
        const std::string id = m.get("id").string(), source = m.get("source").string();
        if (dir.empty()) break;
        const fs::path file = fs::weakly_canonical(utf8_path(dir) / utf8_path(source), ec);
        const auto rel = file.lexically_relative(root);
        if (ec || rel.empty() || rel.begin()->string() == "..") {
            add({"model " + id + ": its source is outside the package", "model", id, "source"});
            continue;
        }
        Value info;
        if (!fs::is_regular_file(file, ec)) add({"model " + id + ": " + source + " is not in the package", "model", id, "source"});
        else if (!check_model_file(file.string(), info, why)) add({"model " + id + ": " + why, "model", id, "source"});
        else if (m.get("kind").string("static") != info.get("kind").string())
            add({"model " + id + ": the file is a " + info.get("kind").string() + " model, the package says " + m.get("kind").string("static"), "model", id, "kind"});
    }
    why.clear();
    if (validate_package(mod, manifest, all, why)) return out;
    const std::string whole = why;
    const size_t before = out.size();
    // a scene or quest check that repeats an actor's problem is about that actor
    auto known = [&](const std::string& message) {
        return std::any_of(out.begin(), out.end(), [&](const Problem& p) { return p.message == message; });
    };
    // the package is refused: find what it is about. Each actor on its own (with the switch a door
    // names, without scenes), each scene with the actors, each quest on its own.
    for (const auto& a : manifest.get("actors").array) {
        Value one = manifest;
        one.object.erase("scenes");
        one["actors"] = array();
        one["actors"].array.push_back(a);
        if (a.get("switch").type == Value::String)
            if (const Value* sw = by_id(manifest.get("actors"), a.get("switch").text); sw && sw != &a) one["actors"].array.push_back(*sw);
        without_scene_actions(one["actors"]);
        why.clear();
        if (!validate_package(mod, one, all, why)) add({why, "actor", a.get("id").string(), field_of(why)});
    }
    for (const auto& sc : manifest.get("scenes").array) {
        Value one = manifest;
        one["scenes"] = array();
        one["scenes"].array.push_back(sc);
        without_scene_actions(one["actors"]);
        without_scene_actions(one["scenes"]);
        why.clear();
        if (!validate_package(mod, one, all, why) && !known(why)) add({why, "scene", sc.get("id").string(), ""});
    }
    for (const auto& q : manifest.get("quests").array) {
        Value one = manifest;
        one.object.erase("actors");
        one.object.erase("scenes");
        one["quests"] = array();
        one["quests"].array.push_back(q);
        why.clear();
        if (!validate_package(mod, one, all, why) && !known(why)) add({why, "quest", q.get("id").string(), ""});
    }
    // the whole package (limits per room, flags shared with other mods...) when nothing above says why
    if (out.size() == before) add({whole, "package", "", field_of(whole)});
    return out;
}

bool export_package(const std::string& dir, const Value& manifest, const std::string& dest, std::vector<std::string>& written, std::string& why) {
    written.clear();
    std::error_code ec;
    const fs::path src = utf8_path(dir), out = utf8_path(dest);
    if (dir.empty() || !fs::is_directory(src, ec)) return why = "the mod has no package folder", false;
    if (dest.empty()) return why = "no folder to export to", false;
    if (fs::exists(out, ec) && (!fs::is_directory(out, ec) || !fs::is_empty(out, ec))) return why = dest + " is not a new or empty folder", false;
    const fs::path a = fs::weakly_canonical(src, ec), b = fs::weakly_canonical(out, ec);
    const auto inside = b.lexically_relative(a);
    if (!inside.empty() && inside.begin()->string() != "..") return why = "the export folder can't be inside the package", false;
    // the package's own files: no links, no hidden files, never game files
    std::vector<fs::path> files;
    std::vector<std::string> refused;
    for (auto it = fs::recursive_directory_iterator(src, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path rel = it->path().lexically_relative(src);
        bool hidden = false;
        for (const auto& part : rel) hidden = hidden || part.string().starts_with(".") || part == "__pycache__";  // also tool caches
        if (rel.extension() == ".pyc") hidden = true;
        if (hidden || it->is_symlink(ec)) {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        if (rel == "manifest.json" || rel.string().ends_with(".editor-tmp")) continue;
        if (game_file(rel)) refused.push_back(rel.generic_string());
        else files.push_back(rel);
    }
    if (ec) return why = "cannot read the package folder: " + ec.message(), false;
    if (!refused.empty()) {
        why = "the package holds files in the game's formats, which an export never includes: ";
        for (size_t i = 0; i < refused.size() && i < 5; i++) why += (i ? ", " : "") + refused[i];
        return why += ". Remove them from the package first.", false;
    }
    fs::create_directories(out, ec);
    if (ec) return why = "cannot create " + dest + ": " + ec.message(), false;
    {
        std::ofstream f(out / "manifest.json", std::ios::binary | std::ios::trunc);
        f << pretty(manifest);
        if (!f.good()) return why = "cannot write manifest.json", false;
    }
    written.push_back("manifest.json");
    for (const auto& rel : files) {
        fs::create_directories((out / rel).parent_path(), ec);
        fs::copy_file(src / rel, out / rel, fs::copy_options::overwrite_existing, ec);
        if (ec) return why = "cannot copy " + rel.generic_string() + ": " + ec.message(), false;
        written.push_back(rel.generic_string());
    }
    return true;
}

// ---------------------------------------------------------------- session: where things go
bool Session::where(const Value& p, Place& place, std::string& why) {
    const std::string at = p.get("at").string("link");
    if (!host_ && at != "position") return why = "the game is not running", false;
    if (at == "link") place = host_->link();
    else if (at == "camera") {
        place = host_->camera();
        if (!place.valid) return why = "the fly camera is off", false;
    } else if (at == "position") {
        if (!read_point(p.get("position"), place.position)) return why = "at = position needs \"position\": {x, y, z}", false;
        const Place link = host_ ? host_->link() : Place();
        place.stage = p.get("stage").type == Value::String ? p.get("stage").text : link.stage;
        place.room = p.get("room").type == Value::Number ? int(p.get("room").number) : link.room;
        if (place.stage == "sea" && p.get("room").type != Value::Number) place.room = sea_room(place.position.x, place.position.z);
        place.angle = link.angle;
        place.valid = !place.stage.empty() && place.room >= 0 && place.room <= 63;
        if (!place.valid) return why = "say which stage and room (\"stage\", \"room\")", false;
    } else
        return why = "\"at\" is link, camera or position", false;
    if (!place.valid) return why = "Link is not in the scene", false;
    if (p.get("angle").type == Value::Number) place.angle = int(std::lround(p.get("angle").number * 65536.0 / 360.0)) & 0xFFFF;
    return true;
}

// ---------------------------------------------------------------- session: the building commands
Value Session::dispatch_build(const std::string& command, const Value& p, std::string& why, bool& ok, bool& handled) {
    Value r;
    ok = false;
    handled = true;
    // snaps `pos` onto the ground below (a note when it can't)
    auto snap = [&](WorldPoint& pos, Value& result) {
        if (!p.get("snap").boolean && p.get("snap").type == Value::Bool) return;
        if (!host_) return;
        float h = 0;
        std::string w;
        if (host_->ground({pos.x, pos.y + 50, pos.z}, h, w)) {
            pos.y = h;
            result["snapped"] = true;
        } else
            result["snap_note"] = "not on the ground: " + w;
    };
    auto actor_change = [&](const std::string& label, const std::function<bool(Value&, std::string&)>& set) {
        Package* pkg = package(p, why);
        if (!pkg) return;
        const std::string id = text(p, "id");
        ok = change(*pkg, label, [&](Value& m, std::string& w) {
            Value* a = by_id(m["actors"], id);
            if (!a) return w = "no placed actor \"" + id + "\" in " + pkg->mod, false;
            return set(*a, w);
        }, r, why);
        if (ok) r["id"] = id;
    };

    if (command == "list_actor_types") {
        r = array();
        for (const auto& t : actor_types()) {
            Value v;
            v["type"] = t.type;
            v["label"] = t.label;
            v["group"] = t.group;
            v["help"] = t.help;
            v["needs_model"] = t.needs_model;
            if (t.skinned) v["skinned"] = true;
            v["live"] = host_ ? host_->live_kind(t.type, t.live) : t.live;
            r.array.push_back(v);
        }
        ok = true;
        return r;
    }
    if (command == "place_world_object") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const auto id = text(p, "id");
        if (!id_ok(id) || by_id(pkg->manifest.get("actors"), id)) return why = "a world object needs a free actor id", r;
        Value actor;
        actor["id"] = id;
        actor["kind"] = "mod";
        actor["behaviour"] = p.get("behaviour").string("static");
        for (const char* key : {"model", "collision", "stage", "room", "position", "rotation", "scale", "buoyancy", "drift", "anchor", "grapple_points", "distant", "shoreline", "chart_marker", "arrival"})
            if (p.get(key).type != Value::Null) actor[key] = p.get(key);
        ok = change(*pkg, "Place world object " + id, [&](Value& manifest, std::string&) {
            if (manifest.get("actors").type != Value::Array) manifest["actors"] = array();
            manifest["actors"].array.push_back(actor);
            return true;
        }, r, why);
        if (ok) { r["id"] = id; r["actor"] = actor; }
        return r;
    }
    if (command == "set_world_object") {
        actor_change("Set world object " + text(p, "id"), [&](Value& actor, std::string& error) {
            if (actor.get("kind").string() != "mod") return error = "world objects require a mod model actor", false;
            if (p.object.contains("behaviour")) actor["behaviour"] = p.get("behaviour");
            else if (actor.get("behaviour").type == Value::Null) actor["behaviour"] = "static";
            for (const char* key : {"collision", "buoyancy", "drift", "anchor", "grapple_points", "distant", "shoreline", "chart_marker", "arrival"}) {
                if (!p.object.contains(key)) continue;
                if (p.get(key).type == Value::Null) actor.object.erase(key);
                else actor[key] = p.get(key);
            }
            const auto behaviour = actor.get("behaviour").string();
            // Drop inherited incompatible settings on a behaviour change. Explicit
            // incompatible parameters still reach the validator and are refused.
            for (const char* key : {"buoyancy", "drift", "anchor"})
                if ((behaviour == "static" || (behaviour == "floating" && std::string(key) != "buoyancy")) && !p.object.contains(key))
                    actor.object.erase(key);
            return true;
        });
        return r;
    }
    if (command == "preview_collision" || command == "placement_conflicts") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        if (!host_) return why = "the game is not running", r;
        std::vector<story_notebook::ManifestActor> actors;
        if (!story_notebook::parse_actors(pkg->manifest, actors, why)) return r;
        const auto found = std::find_if(actors.begin(), actors.end(), [&](const auto& actor) { return actor.id == text(p, "id"); });
        if (found == actors.end() || !found->world_object()) return why = "no placed world object with that id", r;
        ok = host_->world_object_query(pkg->mod, *found, command == "preview_collision", r, why);
        return r;
    }
    if (command == "prepare_collision") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        if (!host_) return why = "the game is not running", r;
        const auto* model = by_id(pkg->manifest.get("models"), text(p, "model"));
        if (!model || model->get("kind").string("static") != "static") return why = "collision needs a declared static model", r;
        Value request = *model;
        request["collision"] = true; // preparation option only; never a manifest field
        std::string note;
        ok = host_->prepare_model(pkg->mod, pkg->dir, request, note, why);
        if (ok) { r["model"] = model->get("id"); r["note"] = note; }
        return r;
    }
    if (command == "add_actor") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        Place place;
        if (!where(p, place, why)) return r;
        Value info;
        snap(place.position, info);
        const std::string type = text(p, "type");
        const std::string id = p.get("id").type == Value::String ? p.get("id").text : free_id(pkg->manifest.get("actors"), type == "model" || type == "model_enemy" || type == "model_carry" ? text(p, "model").empty() ? type : text(p, "model") : type);
        Value a;
        if (!new_actor(type, id, place, text(p, "model"), pkg->manifest, p.get("options"), a, why)) return r;
        if (by_id(pkg->manifest.get("actors"), id)) return why = "the mod has an actor \"" + id + "\" already", r;
        ok = change(*pkg, "Add " + id, [&](Value& m, std::string&) {
            if (m.get("actors").type != Value::Array) m["actors"] = array();
            m["actors"].array.push_back(a);
            return true;
        }, r, why);
        if (!ok) return r;
        r["id"] = id;
        r["actor"] = a;
        for (const auto& [k, v] : info.object) r[k] = v;
        return r;
    }
    if (command == "remove_actor") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const std::string id = text(p, "id");
        ok = change(*pkg, "Remove " + id, [&](Value& m, std::string& w) {
            auto& list = m["actors"].array;
            const auto it = std::find_if(list.begin(), list.end(), [&](const Value& a) { return a.get("id").string() == id; });
            if (it == list.end()) return w = "no placed actor \"" + id + "\" in " + pkg->mod, false;
            list.erase(it);
            return true;
        }, r, why);
        if (ok) r["id"] = id;
        return r;
    }
    if (command == "duplicate_actor") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const Value* a = by_id(pkg->manifest.get("actors"), text(p, "id"));
        if (!a) return why = "no placed actor \"" + text(p, "id") + "\"", r;
        const std::string id = p.get("new_id").type == Value::String ? p.get("new_id").text : free_id(pkg->manifest.get("actors"), text(p, "id"));
        if (!id_ok(id) || by_id(pkg->manifest.get("actors"), id)) return why = "\"" + id + "\" is not a free actor id", r;
        Value copy = duplicate_actor(*a, id);
        WorldPoint pos, off{100, 0, 0};
        read_point(copy.get("position"), pos);
        if (p.get("offset").type != Value::Null) read_point(p.get("offset"), off);
        copy["position"] = rounded({pos.x + off.x, pos.y + off.y, pos.z + off.z});
        ok = change(*pkg, "Duplicate " + text(p, "id") + " as " + id, [&](Value& m, std::string&) {
            m["actors"].array.push_back(copy);
            return true;
        }, r, why);
        if (ok) r["id"] = id;
        return r;
    }
    if (command == "place_actor") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const Value* a = by_id(pkg->manifest.get("actors"), text(p, "id"));
        if (!a) return why = "no placed actor \"" + text(p, "id") + "\"", r;
        Place place;
        Value info;
        if (text(p, "at") == "ground") {
            place.valid = read_point(a->get("position"), place.position);
            place.stage = a->get("stage").string();
            place.room = int(a->get("room").number);
            if (!host_) return why = "the game is not running", r;
            float h = 0;
            if (!host_->ground({place.position.x, place.position.y + 50, place.position.z}, h, why)) return why = "no ground below it: " + why, r;
            place.position.y = h;
        } else {
            if (!where(p, place, why)) return r;
            snap(place.position, info);
        }
        const bool move_room = text(p, "at") != "ground";
        actor_change("Place " + text(p, "id"), [&](Value& actor, std::string&) {
            actor["position"] = rounded(place.position);
            if (move_room) {
                actor["stage"] = place.stage;
                actor["room"] = place.room;
            }
            return true;
        });
        for (const auto& [k, v] : info.object) r[k] = v;
        return r;
    }
    if (command == "pick" || command == "ground") {
        if (!host_) return why = "the game is not running", r;
        if (command == "ground") {
            WorldPoint at;
            read_point(p.get("position"), at);
            float h = 0;
            if (!host_->ground(at, h, why)) return r;
            r["height"] = double(h);
            ok = true;
            return r;
        }
        WorldPoint from, dir, hit;
        read_point(p.get("origin"), from);
        read_point(p.get("direction"), dir);
        const double len = std::sqrt(double(dir.x) * dir.x + double(dir.y) * dir.y + double(dir.z) * dir.z);
        if (!(len > 1e-6)) return why = "the direction has no length", r;
        dir = {float(dir.x / len), float(dir.y / len), float(dir.z / len)};
        const float range = p.get("range").type == Value::Number ? float(std::clamp(p.get("range").number, 1.0, 200000.0)) : 20000.0f;
        if (!host_->ray(from, dir, range, hit, why)) return r;
        r["position"] = rounded(hit);
        if (const Place link = host_->link(); link.valid) {
            r["stage"] = link.stage;
            r["room"] = link.stage == "sea" ? sea_room(hit.x, hit.z) : link.room;
        }
        ok = true;
        return r;
    }
    if (command == "set_then") {
        actor_change("Actions of " + text(p, "id"), [&](Value& a, std::string&) {
            if (p.get("actions").array.empty()) a.object.erase("then");
            else a["then"] = p.get("actions");
            return true;
        });
        return r;
    }
    if (command == "set_drops") {
        actor_change("Drops of " + text(p, "id"), [&](Value& a, std::string&) {
            a["drops"] = p.get("drops");
            return true;
        });
        return r;
    }
    if (command == "preview_dialogue") {
        if (!host_) return why = "the game is not running", r;
        std::string shown;
        if (!p.get("close").boolean) {
            Value lines = p.get("lines");
            if (lines.type == Value::Null) {
                Package* pkg = package(p, why);
                if (!pkg) return r;
                const Value* a = by_id(pkg->manifest.get("actors"), text(p, "id"));
                if (!a) return why = "say which actor (\"id\") or which lines (\"lines\")", r;
                lines = a->get("lines");
            }
            if (lines.type == Value::String) shown = lines.text;
            else
                for (const auto& l : lines.array) shown += (shown.empty() ? "" : "\n") + l.string();
            if (shown.empty()) return why = "no lines to show", r;
        }
        if (!host_->preview_text(shown, p.get("close").boolean, why)) return r;
        r["shown"] = shown;
        ok = true;
        return r;
    }
    if (command == "check_model") {
        if (!check_model_file(text(p, "file"), r, why)) return r;
        ok = true;
        return r;
    }
    if (command == "import_model") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        if (pkg->dir.empty()) return why = "the mod has no package folder", r;
        Value info;
        if (!check_model_file(text(p, "file"), info, why)) return r;
        const fs::path from = utf8_path(text(p, "file"));
        const std::string id = p.get("id").type == Value::String ? p.get("id").text : slug(from.stem().string());
        if (!id_ok(id)) return why = "a model id is 1 to 32 lower-case letters, digits, '-', '_' or '.'", r;
        if (by_id(pkg->manifest.get("models"), id)) return why = "the mod has a model \"" + id + "\" already", r;
        const std::string source = "models/" + id + ".glb";
        const fs::path to = utf8_path(pkg->dir) / utf8_path(source);
        std::error_code ec;
        bool created = false;
        if (fs::exists(to, ec)) {
            std::ifstream a(from, std::ios::binary), b(to, std::ios::binary);
            const std::string x((std::istreambuf_iterator<char>(a)), {}), y((std::istreambuf_iterator<char>(b)), {});
            if (x != y && !p.get("confirm").boolean) return why = "the package has a different " + source + ": \"confirm\": true replaces it", r;
            if (x != y) fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
        } else {
            fs::create_directories(to.parent_path(), ec);
            fs::copy_file(from, to, ec);
            created = !ec;
        }
        if (ec) return why = "cannot copy the model into the package: " + ec.message(), r;
        Value entry;
        entry["id"] = id;
        entry["source"] = source;
        entry["kind"] = info.get("kind");
        ok = change(*pkg, "Import model " + id, [&](Value& m, std::string&) {
            if (m.get("models").type != Value::Array) m["models"] = array();
            m["models"].array.push_back(entry);
            return true;
        }, r, why);
        if (!ok) {
            if (created) fs::remove(to, ec);
            return r;
        }
        r["model"] = entry;
        r["info"] = info;
        if (host_) {
            std::string note, w;
            if (host_->prepare_model(pkg->mod, pkg->dir, entry, note, w)) r["note"] = note;
            else r["note"] = "the model is in the package; it is prepared when the game starts (" + w + ")";
        }
        return r;
    }
    if (command == "check_package") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        r["problems"] = array();
        for (const auto& pr : package_problems(pkg->mod, pkg->manifest, all_manifests(), pkg->dir)) {
            Value v;
            v["message"] = pr.message;
            v["kind"] = pr.kind;
            if (!pr.id.empty()) v["id"] = pr.id;
            if (!pr.field.empty()) v["field"] = pr.field;
            r["problems"].array.push_back(v);
        }
        r["valid"] = r["problems"].array.empty();
        // the quest editor's own checks: warnings, they never block saving or exporting (quests.h)
        for (const auto& pr : quest_problems(pkg->mod, pkg->manifest)) {
            Value v;
            v["message"] = pr.message;
            v["kind"] = pr.kind;
            v["id"] = pr.id;
            if (!pr.field.empty()) v["field"] = pr.field;
            v["severity"] = pr.severity;
            r["problems"].array.push_back(v);
        }
        ok = true;
        return r;
    }
    if (command == "export_package") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const auto problems = package_problems(pkg->mod, pkg->manifest, all_manifests(), pkg->dir);
        if (!problems.empty()) {
            why = "not exported: " + std::to_string(problems.size()) + " problem(s), the first: " + problems.front().message + " (check_package lists them)";
            return r;
        }
        std::vector<std::string> written;
        if (!export_package(pkg->dir, pkg->manifest, text(p, "path"), written, why)) return r;
        r["path"] = text(p, "path");
        r["files"] = array();
        for (const auto& f : written) r["files"].array.push_back(Value(f));
        ok = true;
        return r;
    }
    if (command == "focus") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const Value* a = by_id(pkg->manifest.get("actors"), text(p, "id"));
        WorldPoint at;
        if (!a || !read_point(a->get("position"), at)) return why = "no placed actor \"" + text(p, "id") + "\"", r;
        if (!host_) return why = "the game is not running", r;
        if (!host_->focus(at, why)) return r;
        r["requested"] = true;
        ok = true;
        return r;
    }
    if (command == "play" || command == "stop") {
        if (!host_) return why = "the game is not running", r;
        if (command == "play") {
            const std::string from = p.get("from").string("camera");
            if (from != "camera" && from != "link") return why = "\"from\" is camera or link", r;
            const bool restore = p.get("restore").type == Value::Bool ? p.get("restore").boolean : true;
            if (!host_->play(from == "camera", restore, why)) return r;
            r["from"] = from;
            r["restore"] = restore;
        } else if (!host_->stop(why))
            return r;
        r["requested"] = true;
        ok = true;
        return r;
    }
    (void)kPi;
    handled = false;
    return r;
}

}  // namespace editor
