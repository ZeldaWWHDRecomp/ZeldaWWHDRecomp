// World editor command layer: building new content (commands.h). No game dependencies: the game's side
// (ground checks, model preparation, the message box, Play) is reached through Host.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>

#include "../mods/mod_stages_manifest.h"
#include "../mods/skinned_model_validation.h"
#include "../mods/static_model_validation.h"
#include "../mods/story_manifest.h"
#include "commands.h"
#include "model_import.h"
#include <cstring>
#include "ray_ground.h"
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
        {"1 MiB", "Texture: all textures of one model together take at most 1 MiB (one 512 x 512, four 256 x 256 or sixteen 128 x 128)."},
        {"alphaMode BLEND", "Material: blended transparency is not supported; use MASK (cut-out) or OPAQUE."},
        {"base colour", "Material: every material needs an embedded base colour PNG on UV0."},
        {"material", "Material: up to 16 simple materials, each with one base colour texture (opaque or MASK cut-out), no extensions."},
        {"primitives", "Mesh: the visible mesh has 1 to 32 triangle primitives."},
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
                                                ".yaz0", ".wux", ".wud", ".iso", ".rvz", ".gcm", ".bfsar", ".bars", ".bfstm", ".msbt",
                                                ".dzb", ".dzr", ".dzs", ".ftex"};
    std::string ext = p.extension().string();
    for (char& c : ext) c = char(std::tolower((unsigned char)c));
    return kinds.contains(ext);
}
}  // namespace

// ---------------------------------------------------------------- actor types
const std::vector<ActorType>& actor_types() {
    static const std::vector<ActorType> types = {
        {"sign", "Sign", "Game actors", "A sign with up to three lines; its flag is set when read."},
        {"resident", "Resident", "Game actors", "A villager of the game who talks with the mod's lines: options.resident picks who (the variants list them)."},
        {"chest", "Chest", "Game actors", "A treasure chest; its flag is set when opened."},
        {"pot", "Pot", "Props", "A pot to lift and break; its broken flag and drops.", false, false, false},
        {"barrel", "Barrel", "Props", "A barrel to lift and break.", false, false, false},
        {"crate", "Crate", "Props", "A crate to break.", false, false, false},
        {"switch", "Switch", "Switches", "A floor switch; sets its switch flag when pressed.", false, false, false},
        {"door", "Door", "Switches", "A door opened by one of the mod's switches (options.switch).", false, false, false},
        {"chuchu", "ChuChu", "Enemies", "The game's ChuChu (green, red, blue, yellow or black: options.variant); its defeated flag and drops.", false, false, false},
        {"bokoblin", "Bokoblin", "Enemies", "The game's Bokoblin (a stick, a shield or a machete: options.variant).", false, false, false},
        {"moblin", "Moblin", "Enemies", "The game's Moblin.", false, false, false},
        {"keese", "Keese", "Enemies", "The game's Keese (normal or fire: options.variant).", false, false, false},
        {"peahat", "Peahat", "Enemies", "The game's Peahat (options.variant seahat: the Seahat over the sea): it flies; knock it down (the boomerang or the Deku Leaf's wind), then strike.", false, false, false},
        {"bokobaba", "Boko Baba", "Enemies", "The game's Boko Baba: a biting plant rooted in the ground; strike its head, then its stem. It never grabs Link.", false, false, false},
        {"bubble", "Bubble", "Enemies", "The game's Bubble (red or blue: options.variant): put out its fire, then strike the skull.", false, false, false},
        {"octorok", "Octorok", "Enemies", "The game's Octorok (the small one): it rises from water and spits rocks; reflect one with the shield, or use arrows or the boomerang.", false, false, false},
        {"miniblin", "Miniblin", "Enemies", "The game's Miniblin: it pops up when Link comes near and is defeated for good.", false, false, false},
        {"darknut", "Darknut", "Enemies", "The game's Darknut, with its sword (with a shield, a helmet, or the Mighty Darknut's cape: options.variant).", false, false, false},
        {"rat_hole", "Rat hole", "Enemies", "A rat hole three rats come out of, one after the other (rats, bomb rats or both: options.variant); each defeated rat drops the hole's drops, and the hole closes when all three are defeated.", false, false, false},
        {"stalfos", "Stalfos", "Enemies", "The game's Stalfos with its club: break it apart, then strike its head before it pulls itself together.", false, false, false},
        {"redead", "Redead", "Enemies", "The game's Redead: its scream freezes Link, then it grabs him; light stops it.", false, false, false},
        {"mothula", "Mothula", "Enemies", "The game's Mothula, on the ground or with wings (options.variant).", false, false, false},
        {"morth", "Morth", "Enemies", "The game's Morth: it clings to Link and slows him down; a spin attack or a roll throws it off.", false, false, false},
        {"magtail", "Magtail", "Enemies", "The game's Magtail: strike its head to curl it into a ball, then strike the ball (or lift and throw it).", false, false, false},
        {"poe", "Poe", "Enemies", "The game's Poe with its lantern: without it, it possesses Link for a while; outdoors only Light Arrows defeat it.", false, false, false},
        {"armos", "Armos", "Enemies", "The game's Armos statue: it wakes when Link comes near; strike its back.", false, false, false},
        {"armos_knight", "Armos Knight", "Enemies", "The game's Armos Knight: only a bomb in its open mouth (or a Light Arrow) defeats it.", false, false, false},
        {"wizzrobe", "Wizzrobe", "Enemies", "The game's red Wizzrobe: it appears and vanishes around where it was placed and casts fire at Link.", false, false, false},
        {"big_octo", "Big Octo (target)", "Enemies", "The game's Big Octo, as a target only (on the sea): it rises when Link sails near; shoot all its eyes. It never swallows the boat and makes no whirlpool.", false, false, false},
        {"floormaster", "Floormaster", "Enemies", "The game's Floormaster: a hand that rises from its dark hole and grabs Link for a moment (it never drags him away).", false, false, false},
        {"gyorg", "Gyorgs", "Enemies", "A group of the game's Gyorgs (on the sea only, once Link has the boomerang): they come out when Link is near and bite him; defeated when every one is.", false, false, false},
        {"warship", "Warship", "Enemies", "The game's enemy warship (on the sea only): it fires its cannon at Link; three hits from the boat's cannon sink it.", false, false, false},
        {"wall_cannon", "Wall cannon", "Enemies", "The game's wall cannon: it turns to Link and fires cannonballs while he is inside its firing area (one of the mod's paths, an area; a new one in front is added with it, or options.path names one); a bomb destroys it.", false, false, false},
        {"kargaroc", "Kargaroc", "Enemies", "The game's Kargaroc: a big bird that circles and swoops at Link (or perches until he comes near).", false, false, false},
        {"model", "Your model", "Your models", "One of the mod's models (static or skinned), with collision.", true, false, true},
        {"model_enemy", "Your enemy", "Your models", "A skinned model that fights (needs flinch, stun, death and idle clips).", true, true, false},
        {"model_carry", "Carried prop", "Your models", "A model Link can lift and carry.", true, false, false},
    };
    return types;
}

std::vector<TypeVariant> type_variants(const std::string& type) {
    std::vector<TypeVariant> out;
    if (type == "resident")
        for (const auto& t : story_resident_types::kTypes) out.push_back({"resident", t.code, t.label, t.help});
    else if (story_enemy_variants::has_variants(type))
        for (const auto& v : story_enemy_variants::kVariants)
            if (type == v.kind) out.push_back({"variant", v.name, v.label, v.help});
    return out;
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
        const std::string who = options.get("resident").type == Value::String ? options.get("resident").text : std::string("uo2");
        if (!story_resident_types::find(who)) {
            if (const char* no = story_resident_types::refused(who)) return why = no, false;
            return why = "options.resident is one of " + story_resident_types::codes(), false;
        }
        if (const char* no = story_resident_types::place_problem(story_resident_types::find(who)->subtype, place.stage, place.position.x, place.position.z))
            return why = no, false;
        params["resident"] = who;
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
        if (options.get("path").type == Value::String) {
            Value probe = a;  // the variant chosen below decides whether it takes a path
            if (options.get("variant").type == Value::String) probe["parameters"]["variant"] = options.get("variant").text;
            if (room_path_use(probe).empty()) return why = "a " + t->label + " takes no path", false;
            a["path"] = options.get("path").text;
        }
        if (options.get("variant").type != Value::Null) {
            const std::string v = options.get("variant").string();
            if (!story_enemy_variants::has_variants(type)) return why = "a " + t->label + " has no variants", false;
            if (v.empty() || !story_enemy_variants::find(type, v)) return why = "options.variant is " + story_enemy_variants::names(type), false;
            const auto* chosen = story_enemy_variants::find(type, v);
            if (chosen != story_enemy_variants::find(type, "")) {  // the default variant is no parameter at all
                Value params;
                params["variant"] = v;
                a["parameters"] = params;
            }
        }
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
    return check_model_bytes(bytes, info, why);
}
bool check_model_bytes(const std::vector<uint8_t>& bytes, Value& info, std::string& why) {
    if (bytes.size() > static_models::kMaxSourceBytes) {
        char mb[32];
        std::snprintf(mb, sizeof mb, "%.1f", double(bytes.size()) / (1024 * 1024));
        return why = std::string("Size: the file is ") + mb + " MB; a model can be up to 8 MB.", false;
    }
    info = Value();
    info["size"] = double(bytes.size());
    static_models::ModelInfo s;
    std::string problem;
    if (static_models::validate_glb(bytes, s, problem)) {
        info["kind"] = "static";
        info["vertices"] = double(s.vertex_count);
        info["triangles"] = double(s.triangle_count);
        info["texture"] = std::to_string(s.texture_width) + "x" + std::to_string(s.texture_height);
        info["materials"] = double(s.material_count);
        info["cutout_materials"] = double(s.cutout_count);
        info["texture_bytes"] = double(s.texture_bytes);
        info["bones"] = 0;
        info["clips"] = array();
        info["minimum"] = point_value({s.minimum[0], s.minimum[1], s.minimum[2]});
        info["maximum"] = point_value({s.maximum[0], s.maximum[1], s.maximum[2]});
        return true;
    }
    if (problem.find("skins, animations") == std::string::npos) return why = model_message(problem), false;
    skinned_models::ModelInfo k;
    if (!skinned_models::validate_glb(bytes, k, problem)) return why = model_message(problem), false;
    info["kind"] = "skinned";
    info["vertices"] = double(k.vertex_count);
    info["triangles"] = double(k.triangle_count);
    info["texture"] = std::to_string(k.texture_width) + "x" + std::to_string(k.texture_height);
    info["materials"] = double(k.material_count);
    info["cutout_materials"] = double(k.cutout_count);
    info["texture_bytes"] = double(k.texture_bytes);
    info["bones"] = double(k.joints);
    info["clips"] = array();
    for (const auto& c : k.clips) info["clips"].array.push_back(Value(c.name));
    info["minimum"] = point_value({k.minimum[0], k.minimum[1], k.minimum[2]});
    info["maximum"] = point_value({k.maximum[0], k.maximum[1], k.maximum[2]});
    return true;
}

float origin_gap(const WorldPoint& lo, const WorldPoint& hi) {
    const double dx = std::max({double(lo.x), 0.0, -double(hi.x)}), dz = std::max({double(lo.z), 0.0, -double(hi.z)});
    return float(std::sqrt(dx * dx + dz * dz));
}
bool off_origin(const WorldPoint& lo, const WorldPoint& hi) {
    const double width = std::max(double(hi.x) - lo.x, double(hi.z) - lo.z);
    const double gap = origin_gap(lo, hi);
    return gap > 1000 && gap > width / 4;
}
void placed_bounds(const WorldPoint& lo, const WorldPoint& hi, const Value& actor, WorldPoint& low, WorldPoint& high) {
    WorldPoint at{}, turn{}, scale{1, 1, 1};
    read_point(actor.get("position"), at);
    read_point(actor.get("rotation"), turn);
    read_point(actor.get("scale"), scale);
    const double k = 3.14159265358979323846 / 180;
    const double cx = std::cos(turn.x * k), sx = std::sin(turn.x * k), cy = std::cos(turn.y * k), sy = std::sin(turn.y * k), cz = std::cos(turn.z * k), sz = std::sin(turn.z * k);
    // RY * RX * RZ (mod_actor's matrix, the game's mDoMtx_ZXYrotS)
    const double m[9] = {cy * cz + sy * sx * sz, -cy * sz + sy * sx * cz, sy * cx, cx * sz, cx * cz, -sx, -sy * cz + cy * sx * sz, sy * sz + cy * sx * cz, cy * cx};
    low = {INFINITY, INFINITY, INFINITY};
    high = {-INFINITY, -INFINITY, -INFINITY};
    for (int corner = 0; corner < 8; ++corner) {
        const double v[3] = {double(corner & 1 ? hi.x : lo.x) * scale.x, double(corner & 2 ? hi.y : lo.y) * scale.y, double(corner & 4 ? hi.z : lo.z) * scale.z};
        const float w[3] = {float(m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + at.x), float(m[3] * v[0] + m[4] * v[1] + m[5] * v[2] + at.y),
                            float(m[6] * v[0] + m[7] * v[1] + m[8] * v[2] + at.z)};
        low = {std::min(low.x, w[0]), std::min(low.y, w[1]), std::min(low.z, w[2])};
        high = {std::max(high.x, w[0]), std::max(high.y, w[1]), std::max(high.z, w[2])};
    }
}
WorldPoint room_origin(const std::string& stage, int room) { return stage == "sea" ? sea_room_centre(room) : WorldPoint{0, 0, 0}; }

bool import_model_file(const std::string& path, std::vector<uint8_t>& bytes, Value& info, std::string& why) {
    std::error_code ec;
    const fs::path file = utf8_path(path);
    if (!fs::is_regular_file(file, ec)) return why = "no file " + path, false;
    std::string ext = file.extension().string();
    for (char& c : ext) c = char(std::tolower((unsigned char)c));
    if (ext != ".glb") return why = "a model is a .glb file (glTF 2.0, binary)", false;
    // larger than a model may be: its textures may be scaled down on import
    const auto size = fs::file_size(file, ec);
    if (ec || size > kMaxImportBytes) {
        char mb[32];
        std::snprintf(mb, sizeof mb, "%.1f", double(size) / (1024 * 1024));
        return why = std::string("Size: the file is ") + mb + " MB; a model to import can be up to 64 MB (the model in the package up to 8 MB).", false;
    }
    std::ifstream f(file, std::ios::binary);
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (raw.size() != size) return why = "cannot read " + path, false;
    // not a GLB at all: the check's own message
    if (raw.size() < 12 || std::memcmp(raw.data(), "glTF", 4)) return check_model_bytes(raw, info, why);
    model_import::Result fixed;
    std::string problem;
    if (!model_import::fix(raw, fixed, problem)) return why = "The model can't be imported: " + problem + ".", false;
    if (!check_model_bytes(fixed.glb, info, why)) {
        if (fixed.changed) why += " (after the import's own fixes)";
        return false;
    }
    bytes = std::move(fixed.glb);
    if (!fixed.warnings.empty()) {
        info["import_warnings"] = array();
        for (const auto& n : fixed.warnings) info["import_warnings"].array.push_back(Value(n));
    }
    if (fixed.changed) {
        info["import_fixes"] = array();
        for (const auto& n : fixed.notes) info["import_fixes"].array.push_back(Value(n));
    }
    return true;
}

// A kit model's file (static_models::validate_kit_glb): its own textures' rules and no game material with a texture; `p`
// holds the declaration's kit, materials and material (the names it may not embed a texture under).
namespace {
bool check_kit_model_bytes(const std::vector<uint8_t>& bytes, const Value& p, Value& info, std::string& why) {
    std::vector<std::string> names;
    if (p.get("material").type == Value::String) names.push_back(p.get("material").text);
    if (p.get("materials").type == Value::Object)
        for (const auto& [from, to] : p.get("materials").object) {
            names.push_back(from);
            names.push_back(to.string());
        }
    static_models::KitInfo k;
    if (!static_models::validate_kit_glb(bytes, names, k, why)) return false;
    info = Value();
    info["kind"] = "kit";
    info["size"] = double(bytes.size());
    info["triangles"] = double(k.triangles);
    info["materials"] = double(k.materials);
    info["own_materials"] = double(k.own_materials);
    info["own_texture_bytes"] = double(k.own_texture_bytes);
    info["clips"] = array();
    return true;
}
bool read_kit_model_file(const std::string& path, const Value& p, std::vector<uint8_t>& bytes, std::string& why) {
    std::error_code ec;
    const fs::path file = utf8_path(path);
    if (!fs::is_regular_file(file, ec)) return why = "no file " + path, false;
    const auto& kit = p.get("kit");
    if (kit.type != Value::Object || kit.get("stage").type != Value::String || kit.get("room").type != Value::Number)
        return why = "kit is a room of the game: {\"stage\": \"sea\", \"room\": 13}", false;
    std::ifstream f(file, std::ios::binary);
    bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}
// a material name without Blender's copy suffix (".001"), as kit_model.py's Names reads it
std::string plain_material_name(std::string n) {
    const auto dot = n.rfind('.');
    if (dot != std::string::npos && n.size() - dot == 4 && std::all_of(n.begin() + long(dot) + 1, n.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
        n.resize(dot);
    return n;
}
}  // namespace

bool check_kit_model_file(const std::string& path, const Value& p, Value& info, std::string& why) {
    std::vector<uint8_t> bytes;
    return read_kit_model_file(path, p, bytes, why) && check_kit_model_bytes(bytes, p, info, why);
}

bool prepare_kit_model_file(const std::string& path, const Value& p, const Value& listed, std::vector<uint8_t>& bytes, Value& info, std::string& why) {
    std::vector<uint8_t> raw;
    if (!read_kit_model_file(path, p, raw, why)) return false;
    bytes = raw;
    // which of the file's materials are game materials (kit_model.py's classify: they may not embed a texture)
    std::vector<bool> strip;
    std::vector<std::string> stripped;
    if (raw.size() >= 20 && !std::memcmp(raw.data(), "glTF", 4)) {
        const uint32_t json_length = static_models::le32(raw.data() + 12);
        Value doc;
        try {
            if (json_length <= raw.size() - 20) doc = mods::json::parse(std::string(reinterpret_cast<const char*>(raw.data() + 20), json_length));
        } catch (...) {
        }
        std::set<std::string> game, game_textures;
        if (p.get("material").type == Value::String) game.insert(p.get("material").text);
        for (const auto& [from, to] : p.get("materials").object) game.insert(from), game.insert(to.string());
        for (const auto& m : listed.get("materials").array) {
            game.insert(m.get("material").string());
            for (const auto& t : m.get("textures").array) game_textures.insert(t.string());
        }
        const auto& images = doc.get("images").array;
        const auto& textures = doc.get("textures").array;
        for (const auto& mat : doc.get("materials").array) {
            const std::string name = mat.get("name").string();
            const auto& colour = mat.get("pbrMetallicRoughness").get("baseColorTexture");
            std::string image;  // the colour texture's image name (an image named after a game texture: a game picture)
            if (colour.get("index").type == Value::Number && colour.get("index").number >= 0 && size_t(colour.get("index").number) < textures.size()) {
                const auto& source = textures[size_t(colour.get("index").number)].get("source");
                if (source.type == Value::Number && source.number >= 0 && size_t(source.number) < images.size()) image = images[size_t(source.number)].get("name").string();
            }
            const bool named = game.count(name) || game.count(plain_material_name(name)) || mat.get("extras").get("wwhd").get("kit_material").type != Value::Null ||
                               (!image.empty() && game_textures.count(image));
            bool textured = colour.type != Value::Null || mat.get("pbrMetallicRoughness").get("metallicRoughnessTexture").type != Value::Null;
            for (const char* slot : {"normalTexture", "occlusionTexture", "emissiveTexture"}) textured |= mat.get(slot).type != Value::Null;
            strip.push_back(named);
            if (named && textured) stripped.push_back(name);
        }
    }
    unsigned removed = 0;
    if (!stripped.empty() && !model_import::strip_material_textures(raw, strip, bytes, removed, why)) return false;
    if (!check_kit_model_bytes(bytes, p, info, why)) return false;
    info["size"] = double(bytes.size());
    if (removed) {
        std::string list;
        info["preview_textures_removed"] = array();
        for (const auto& n : stripped) {
            list += (list.empty() ? "" : ", ") + (n.empty() ? std::string("(unnamed)") : n);
            info["preview_textures_removed"].array.push_back(Value(n));
        }
        info["import_fixes"] = array();
        info["import_fixes"].array.push_back(Value("Removed " + std::to_string(removed) + " preview texture" + (removed > 1 ? "s" : "") + " from kit materials (" + list +
                                                   "): the game draws its own textures for them, and the package keeps none of the game's pictures."));
    }
    return true;
}

Value kit_material_report(const std::vector<uint8_t>& glb, const Value& listed, const Value& declaration) {
    Value out;
    out["materials"] = array();
    for (const char* k : {"game", "own", "unknown"}) out[k] = array();
    if (glb.size() < 20 || std::memcmp(glb.data(), "glTF", 4)) return out;
    const uint32_t json_length = uint32_t(glb[12]) | uint32_t(glb[13]) << 8 | uint32_t(glb[14]) << 16 | uint32_t(glb[15]) << 24;
    if (json_length > glb.size() - 20) return out;
    Value doc;
    try {
        doc = mods::json::parse(std::string(reinterpret_cast<const char*>(glb.data() + 20), json_length));
    } catch (...) {
        return out;
    }
    // the room's names, the mapping and the default, as kit_model.py's Names resolves them (a Blender ".001" ignored)
    std::set<std::string> game;
    for (const auto& m : listed.get("materials").array) game.insert(m.get("material").string());
    const auto plain = [](std::string n) {
        const auto dot = n.rfind('.');
        if (dot != std::string::npos && n.size() - dot == 4 && std::all_of(n.begin() + long(dot) + 1, n.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
            n.resize(dot);
        return n;
    };
    const std::string fallback = declaration.get("material").type == Value::String ? declaration.get("material").text : std::string();
    const auto resolve = [&](const std::string& name) -> std::string {
        for (const auto& n : {name, plain(name)})
            if (declaration.get("materials").get(n).type == Value::String) return declaration.get("materials").get(n).text;
        for (const auto& n : {name, plain(name)})
            if (game.count(n)) return n;
        return {};
    };
    // the visible primitives' materials, in the file's order (a node or mesh named *_col is collision only)
    const auto& materials = doc.get("materials").array;
    std::vector<int> used;
    for (const auto& node : doc.get("nodes").array) {
        if (node.get("mesh").type != Value::Number) continue;
        const size_t mi = size_t(node.get("mesh").number);
        if (mi >= doc.get("meshes").array.size()) continue;
        const auto& mesh = doc.get("meshes").array[mi];
        if (node.get("name").string().ends_with("_col") || mesh.get("name").string().ends_with("_col")) continue;
        for (const auto& p : mesh.get("primitives").array) {
            const int m = p.get("material").type == Value::Number ? int(p.get("material").number) : -1;
            if (std::find(used.begin(), used.end(), m) == used.end()) used.push_back(m);
        }
    }
    std::sort(used.begin(), used.end());
    for (const int m : used) {
        const Value* mat = m >= 0 && size_t(m) < materials.size() ? &materials[size_t(m)] : nullptr;
        const std::string name = mat ? mat->get("name").string() : std::string();
        Value e;
        e["name"] = name;
        if (mat && mat->get("pbrMetallicRoughness").get("baseColorTexture").type != Value::Null) {
            e["kind"] = "own";
        } else if (const std::string named = resolve(name); !named.empty()) {
            e["kind"] = "game", e["game"] = named;
        } else if (name.empty() || name.starts_with("col_") || !fallback.empty()) {
            e["kind"] = "game", e["game"] = fallback.empty() ? listed.get("main").string() : fallback, e["default"] = true;
        } else {
            e["kind"] = "unknown";
        }
        out[e.get("kind").string()].array.push_back(Value(name));
        out["materials"].array.push_back(e);
    }
    return out;
}

std::string kit_report_note(const Value& report, const Value& kit) {
    const auto names = [](const Value& list) {
        std::string s;
        for (const auto& n : list.array) s += (s.empty() ? "" : ", ") + (n.string().empty() ? std::string("(unnamed)") : n.string());
        return s;
    };
    const std::string room = kit.get("stage").string() + " room " + std::to_string(int(kit.get("room").number));
    std::string s;
    if (!report.get("game").array.empty()) s += "Game materials of " + room + ": " + names(report.get("game")) + ".";
    if (!report.get("own").array.empty()) s += std::string(s.empty() ? "" : " ") + "Own textures: " + names(report.get("own")) + ".";
    if (!report.get("unknown").array.empty())
        s += std::string(s.empty() ? "" : " ") + "Not materials of " + room + " and without a texture: " + names(report.get("unknown")) + ".";
    return s;
}

namespace {
// A kit model's file against its room (Host::kit_materials): `report` (kit_material_report; null when the room can't be
// read here) and false with `why` when a material without a texture is none of the room's (its build would fail).
// `listed`: the room's materials (kit_room_materials; null when they can't be read here); `bytes`: the file as the package keeps it.
bool kit_materials_known(const Value& listed, const std::vector<uint8_t>& bytes, const Value& declaration, Value& report, std::string& why) {
    report = Value();
    if (listed.type == Value::Null) return true;
    const Value& kit = declaration.get("kit");
    report = kit_material_report(bytes, listed, declaration);
    const auto& unknown = report.get("unknown").array;
    if (unknown.empty()) return true;
    std::string names, room_names;
    for (const auto& n : unknown) names += (names.empty() ? "\"" : ", \"") + n.string() + "\"";
    for (const auto& m : listed.get("materials").array) room_names += (room_names.empty() ? "" : ", ") + m.get("material").string();
    const bool many = unknown.size() > 1;
    why = "the material" + std::string(many ? "s " : " ") + names + " ha" + (many ? "ve" : "s") + " no texture and " + (many ? "are" : "is") +
          " not a material of " + kit.get("stage").string() + " room " + std::to_string(int(kit.get("room").number)) + ": name it as one of the room's (" +
          room_names + "), map it with \"materials\" or give it a texture of its own";
    return false;
}

// A kit room's materials (Host::kit_materials) for a declaration's "kit"; null (and `why`) when they can't be read here.
Value kit_room_materials(Host* host, const Value& declaration, std::string& why) {
    if (!host) return why = "the game is not running", Value();
    const Value& kit = declaration.get("kit");
    Value listed = host->kit_materials(kit.get("stage").string(), unsigned(std::max(0.0, kit.get("room").number)), why);
    return why.empty() ? listed : Value();
}

// The actors that draw or stand on a model of the manifest (model, distant or shoreline model, collision mesh) and the
// items that hold it: what removing it touches (issue #287).
std::vector<std::string> model_actors(const Value& manifest, const std::string& id) {
    std::vector<std::string> out;
    for (const auto& a : manifest.get("actors").array)
        if (a.get("model").string() == id || a.get("distant").get("model").string() == id || a.get("shoreline").get("model").string() == id ||
            a.get("collision").get("mesh").string() == id)
            out.push_back(a.get("id").string());
    return out;
}
std::vector<std::string> model_items(const Value& manifest, const std::string& id) {
    std::vector<std::string> out;
    for (const auto& i : manifest.get("items").array)
        if (i.get("held_model").string() == id) out.push_back(i.get("id").string());
    return out;
}

// the last part of a path given as UTF-8 text (no conversion through the system's code page)
std::string file_name(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// A whole GLB on disk: its header's length is the file's size (a file still being written is shorter).
bool whole_glb(const fs::path& file, uint64_t size) {
    std::ifstream f(file, std::ios::binary);
    uint8_t h[12] = {};
    if (!f.read(reinterpret_cast<char*>(h), 12)) return false;
    return std::memcmp(h, "glTF", 4) == 0 && static_models::le32(h + 8) == size;
}
}  // namespace

void Session::sync_model_files(const Package& p, const Value& before, bool removing) {
    if (p.dir.empty()) return;
    const fs::path root = utf8_path(p.dir);
    const auto in_models = [](const std::string& source) { return source.rfind("models/", 0) == 0 && source.find("..") == std::string::npos; };
    const auto aside = [&](const std::string& source) { return root / "models" / ".removed" / utf8_path(source.substr(7)); };
    std::set<std::string> now;
    std::error_code ec;
    for (const auto& m : p.manifest.get("models").array) {
        const std::string source = m.get("source").string();
        now.insert(source);
        // back: a model the manifest has again (an undo of its removal) whose file was put aside
        if (in_models(source) && set_aside_.count(p.mod + "/" + source) && !fs::exists(root / utf8_path(source), ec) && fs::is_regular_file(aside(source), ec))
            fs::rename(aside(source), root / utf8_path(source), ec);
    }
    for (const auto& m : before.get("models").array) {
        const std::string source = m.get("source").string();
        if (!in_models(source) || now.count(source) || !fs::is_regular_file(root / utf8_path(source), ec)) continue;
        if (!removing && !set_aside_.count(p.mod + "/" + source)) continue;  // only remove_model's files (an undone import keeps its file)
        set_aside_.insert(p.mod + "/" + source);
        // aside: a model the change dropped; models/.removed is hidden, so an export never takes it along
        fs::create_directories(aside(source).parent_path(), ec);
        fs::remove(aside(source), ec);
        fs::rename(root / utf8_path(source), aside(source), ec);
    }
}

std::vector<Value> Session::watch_due(int64_t now_ms) {
    std::vector<Value> due;
    for (auto& [key, w] : watches_) {
        std::error_code ec;
        const fs::path file = utf8_path(w.file);
        const auto size = fs::file_size(file, ec);
        if (ec) continue;  // gone for now (Blender writes a new file in its place): looked at again next time
        const auto time = fs::last_write_time(file, ec);
        if (ec) continue;
        const int64_t mtime = int64_t(std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count());
        if (mtime != w.seen_mtime || size != w.seen_size) {  // still changing: wait until it has been the same for a while
            w.seen_mtime = mtime, w.seen_size = size, w.since = now_ms;
            continue;
        }
        if ((mtime == w.mtime && size == w.size) || now_ms - w.since < kWatchSettleMs || !whole_glb(file, size)) continue;
        w.mtime = mtime, w.size = size;  // once per change, whether the replace works or not
        const auto slash = key.find('/');
        Value v;
        v["mod"] = key.substr(0, slash);
        v["model"] = key.substr(slash + 1);
        v["file"] = w.file;
        due.push_back(v);
    }
    return due;
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
        else if (m.get("kind").string() == "kit") {
            if (!check_kit_model_file(file.string(), m, info, why)) add({"model " + id + ": " + why, "model", id, "source"});
        } else if (!check_model_file(file.string(), info, why)) add({"model " + id + ": " + why, "model", id, "source"});
        else if (m.get("kind").string("static") != info.get("kind").string())
            add({"model " + id + ": the file is a " + info.get("kind").string() + " model, the package says " + m.get("kind").string("static"), "model", id, "kind"});
    }
    // the items' icons: a PNG in the package each (the game reads them when it starts)
    for (const auto& it : manifest.get("items").array) {
        const std::string id = it.get("id").string(), icon = it.get("icon").string();
        if (dir.empty() || icon.empty() || icon.find("..") != std::string::npos) continue;
        const fs::path file = fs::weakly_canonical(utf8_path(dir) / utf8_path(icon), ec);
        const auto rel = file.lexically_relative(root);
        unsigned w = 0, h = 0;
        if (ec || rel.empty() || rel.begin()->string() == "..") add({"item " + id + ": its icon is outside the package", "item", id, "icon"});
        else if (!fs::is_regular_file(file, ec)) add({"item " + id + ": " + icon + " is not in the package", "item", id, "icon"});
        else if (!check_icon_file(file.string(), w, h, why)) add({"item " + id + ": " + icon + ": " + why, "item", id, "icon"});
    }
    why.clear();
    {  // a stage's room nothing leads to (kept while editing, validate_package)
        std::vector<mod_stages::Stage> stages;
        if (validate_package(mod, manifest, all, why) && !mod_stages::parse(manifest, stages, why)) add({"stages: " + why, "package", "", "stages"});
        why.clear();
    }
    if (validate_package(mod, manifest, all, why)) return out;
    const std::string whole = why;
    const size_t before = out.size();
    // a scene or quest check that repeats an actor's problem is about that actor
    auto known = [&](const std::string& message) {
        return std::any_of(out.begin(), out.end(), [&](const Problem& p) { return p.message == message; });
    };
    // the package is refused: find what it is about. The items, charts and letters each on their own
    // (with the items a chart or letter names), then each actor on its own (with the switch a door
    // names, without scenes), each scene with the actors, each quest on its own.
    for (const char* section : {"items", "charts", "letters"})
        for (const auto& e : manifest.get(section).array) {
            Value one = manifest;
            one[section] = array();
            one[section].array.push_back(e);
            why.clear();
            std::vector<story_notebook::ManifestItem> items;
            std::vector<story_notebook::ManifestChart> charts;
            std::vector<story_notebook::ManifestLetter> letters;
            const bool fine = std::string(section) == "items" ? story_notebook::parse_items(one, items, why)
                              : std::string(section) == "charts" ? story_notebook::parse_charts(one, charts, why)
                                                                 : story_notebook::parse_letters(one, letters, why);
            const std::string kind = std::string(section).substr(0, std::string(section).size() - 1);
            if (!fine && !known(why)) add({why, kind, e.get("id").string(), ""});
        }
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

namespace {
// The bounds of one of the package's models (check_model_file's), false for a kit model or an unusable file.
bool model_box(const std::string& dir, const Value& manifest, const std::string& model, WorldPoint& lo, WorldPoint& hi) {
    const Value* m = by_id(manifest.get("models"), model);
    if (!m || dir.empty() || m->get("kind").string() == "kit") return false;
    Value info;
    std::string w;
    return check_model_file((fs::path(utf8_path(dir)) / utf8_path(m->get("source").string())).string(), info, w) &&
           read_point(info.get("minimum"), lo) && read_point(info.get("maximum"), hi);
}
std::string metres_text(double units) {
    char text[32];
    std::snprintf(text, sizeof text, "%.0f m", units / 100);
    return text;
}
std::string place_text(const WorldPoint& p) {
    char text[64];
    std::snprintf(text, sizeof text, "(%.0f, %.0f, %.0f)", double(p.x), double(p.y), double(p.z));
    return text;
}
}  // namespace

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
        // on the sea nothing goes below the water's surface: a point on the open sea stays at sea level (y 0)
        const bool sea = p.get("stage").string(host_->link().stage) == "sea" && pos.y >= 0;
        if (host_->ground({pos.x, pos.y + 50, pos.z}, h, w)) {
            pos.y = sea ? std::max(h, 0.0f) : h;
            result["snapped"] = true;
        } else if (sea) {
            pos.y = 0;
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
            const auto variants = type_variants(t.type);
            if (!variants.empty()) {
                v["option"] = variants.front().option;
                v["variants"] = array();
                for (const auto& x : variants) {
                    Value e;
                    e["value"] = x.value;
                    e["label"] = x.label;
                    e["help"] = x.help;
                    v["variants"].array.push_back(e);
                }
            }
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
        for (const char* key : {"model", "collision", "stage", "room", "position", "rotation", "scale", "buoyancy", "drift", "anchor", "grapple_points", "distant", "shoreline", "chart_marker", "arrival", "calm_water"})
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
            for (const char* key : {"collision", "buoyancy", "drift", "anchor", "grapple_points", "distant", "shoreline", "chart_marker", "arrival", "calm_water"}) {
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
        // a trial pose (the editor's handle drag): where it would be, before anything changes
        if (p.get("position").type == Value::Object) {
            const Value& q = p.get("position");
            for (const char* k : {"x", "y", "z"})
                if (q.get(k).type != Value::Number || !std::isfinite(q.get(k).number)) return why = "invalid position", r;
            found->position = {float(q.get("x").number), float(q.get("y").number), float(q.get("z").number)};
        }
        if (p.get("yaw").type == Value::Number && std::isfinite(p.get("yaw").number)) found->rotation.y = float(p.get("yaw").number);
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
        const bool game = story_game_objects::is_reference(type);
        const bool piece = story_game_pieces::is_reference(type);  // an island building (commands_game_pieces.cpp)
        const std::string id = p.get("id").type == Value::String ? p.get("id").text : free_id(pkg->manifest.get("actors"), game ? story_game_objects::reference(type) : piece ? story_game_pieces::reference(type) : type == "model" || type == "model_enemy" || type == "model_carry" ? text(p, "model").empty() ? type : text(p, "model") : type);
        Value a;
        if ((game || piece) && !id_ok(id)) return why = "an actor id is 1 to 32 lower-case letters, digits, '-', '_' or '.'", r;
        if (piece ? !new_game_piece(type, id, place, a, why) : game ? !new_game_object(type, id, place, p.get("options"), a, why) : !new_actor(type, id, place, text(p, "model"), pkg->manifest, p.get("options"), a, why)) return r;
        if (by_id(pkg->manifest.get("actors"), id)) return why = "the mod has an actor \"" + id + "\" already", r;
        // "Your model" with collision of its own (its "<name>_col" meshes), or a kit model: a static world object
        // standing on its own collision, so Link walks on it and is stopped by its walls, as on the game's islands.
        // Without that it got a small cylinder at its origin, and a model built on an island export (Link walking on a
        // new hill beside Windfall) had no collision where it is drawn (issue #280).
        bool world_object = false;
        if (type == "model") {
            const Value* m = by_id(pkg->manifest.get("models"), text(p, "model"));
            const std::string kind = m ? m->get("kind").string("static") : std::string();
            world_object = kind == "kit" || (kind == "static" && model_has_collision_mesh(pkg->dir, pkg->manifest, text(p, "model")));
            if (world_object) {
                a["behaviour"] = "static";
                Value collision;
                collision["mesh"] = text(p, "model");
                a["collision"] = collision;
            }
        }
        // its collision is prepared before the change (the model was imported without it, or the game started
        // without an actor on it); the placement is checked when it is ready, and the object appears then
        std::string collision_note;
        if (world_object) {
            const Value* m = by_id(pkg->manifest.get("models"), text(p, "model"));
            if (host_ && m && m->get("kind").string("static") == "static") {
                Value request = *m;
                request["collision"] = true;  // a preparation option, never a manifest field
                std::string prepared, w;
                collision_note = host_->prepare_model(pkg->mod, pkg->dir, request, prepared, w) ? " Its collision is being prepared; it appears when it is ready."
                                                                                                : " Its collision is prepared when the game starts (" + w + ").";
            }
        }
        // A model made away from its origin (on an island export, which keeps the room's coordinates) is drawn that far
        // from where it is placed. Added at Link or the camera, it goes to the room's origin, unturned, so it stands
        // where it was modelled; a given position is kept, and the note says where it is drawn (issue #280).
        std::string model_note;
        if (WorldPoint lo, hi; a.get("kind").string() == "mod" && model_box(pkg->dir, pkg->manifest, text(p, "model"), lo, hi) && off_origin(lo, hi)) {
            const std::string name = text(p, "model");
            const std::string gap = metres_text(origin_gap(lo, hi));
            const std::string room = place.stage == "sea" ? "sea room " + std::to_string(place.room) : place.stage + " room " + std::to_string(place.room);
            if (p.get("at").string("link") != "position") {
                const WorldPoint origin = room_origin(place.stage, place.room);
                a["position"] = rounded(origin);
                a.object.erase("rotation");
                info.object.erase("snapped");  // the origin is where the model was made, not the ground below Link
                info.object.erase("snap_note");
                WorldPoint low, high;
                placed_bounds(lo, hi, a, low, high);
                model_note = name + " was modelled away from its origin (" + gap + " from it, as on an island export, which keeps the room's coordinates). It is placed at the origin of " +
                             room + " " + place_text(origin) + ", unturned, so it stands where you modelled it: around " + place_text({(low.x + high.x) / 2, (low.y + high.y) / 2, (low.z + high.z) / 2}) +
                             ". Its handle is that origin; move or turn it as a whole.";
            } else {
                WorldPoint low, high;
                placed_bounds(lo, hi, a, low, high);
                model_note = name + " was modelled away from its origin (" + gap + " from it): it is drawn around " + place_text({(low.x + high.x) / 2, (low.y + high.y) / 2, (low.z + high.z) / 2}) +
                             ", not at the position given. Modelled on an island export, it stands where you modelled it at the origin of the room " + place_text(room_origin(place.stage, place.room)) + ", unturned.";
            }
        }
        ok = change(*pkg, "Add " + id, [&](Value& m, std::string& w) {
            // an actor that needs one of the mod's room paths (a wall cannon's firing area): the one
            // options.path names, or a new square area of 1600 units in front of it
            if (!room_path_use(a).empty() && a.get("path").type == Value::Null) {
                const std::string base = id + (type == "wall_cannon" ? "-area" : "-route");
                std::string pid = base;
                for (int n = 2; [&] { for (const auto& v : m.get("paths").array) if (v.get("id").string() == pid) return true; return false; }(); n++) pid = base + "-" + std::to_string(n);
                // a cannon's firing area in front of it; a route around the place (a warship's wider)
                const bool cannon = type == "wall_cannon";
                const double yaw = place.angle * 3.14159265358979323846 / 32768.0, ahead = cannon ? 1200 : 0, half = cannon ? 800 : type == "warship" ? 1500 : type == "kargaroc" ? 1000 : type == "wizzrobe" ? 600 : 400;
                const double cx = place.position.x + std::sin(yaw) * ahead, cz = place.position.z + std::cos(yaw) * ahead;
                Value path;
                path["id"] = pid;
                path["stage"] = place.stage;
                path["room"] = double(place.room);
                path["closed"] = true;
                path["points"] = array();
                for (const auto& c : {std::pair{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}) path["points"].array.push_back(rounded({float(cx + c.first * half), place.position.y, float(cz + c.second * half)}));
                // a Kargaroc with a rider lets it go at the third point
                if (!story_enemy_variants::rider_kind(type, a.get("parameters").get("variant").string()).empty()) path["points"].array[2]["release"] = true;
                if (m.get("paths").type != Value::Array) m["paths"] = array();
                // a Wizzrobe summoner summons at the points of its path's next path: a smaller square inside
                if (story_enemy_variants::summoner(type, a.get("parameters").get("variant").string())) {
                    Value spots = path;
                    spots["id"] = pid + "-spots";
                    spots["points"] = array();
                    for (const auto& c : {std::pair{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}) spots["points"].array.push_back(rounded({float(cx + c.first * half / 2), place.position.y, float(cz + c.second * half / 2)}));
                    m["paths"].array.push_back(spots);
                    path["next"] = pid + "-spots";
                }
                m["paths"].array.push_back(path);
                a["path"] = pid;
            }
            (void)w;
            if (game) require_game_objects(m, type);  // "requires": {"game_objects": N}
            if (piece) require_game_piece(m, type);
            if (m.get("actors").type != Value::Array) m["actors"] = array();
            m["actors"].array.push_back(a);
            return true;
        }, r, why);
        if (!ok) return r;
        r["id"] = id;
        r["actor"] = a;
        for (const auto& [k, v] : info.object) r[k] = v;
        if (world_object)
            r["note"] = text(p, "model") + " stands on its own collision (its _col meshes): Link can walk on it." + collision_note +
                        (r.get("note").string().empty() ? std::string() : " " + r.get("note").string());
        if (!model_note.empty()) r["note"] = model_note + (r.get("note").string().empty() ? std::string() : " " + r.get("note").string());
        if (piece && host_) {
            // the piece's set is built from the player's room archive if this mod has no copy yet
            const std::string ref = story_game_pieces::reference(type), state = host_->piece_state(pkg->mod, ref);
            std::string note, w;
            if (state != "ready" && state != "preparing" && !host_->prepare_piece(pkg->mod, ref, note, w)) note = "the building could not be prepared: " + w;
            if (state == "preparing") note = "preparing... the building appears when it is ready";
            if (!note.empty()) r["note"] = note;
            r["state"] = host_->piece_state(pkg->mod, ref);
        }
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
            // a body of water's generated models (the water tool, models/water/) go with it once nothing uses them
            if (m.get("models").type == Value::Array) {
                auto& models = m["models"].array;
                models.erase(std::remove_if(models.begin(), models.end(), [&](const Value& e) {
                                 if (e.get("source").string().rfind("models/water/", 0) != 0) return false;
                                 const std::string mid = e.get("id").string();
                                 return std::none_of(list.begin(), list.end(), [&](const Value& a) {
                                     return a.get("model").string() == mid || a.get("collision").get("mesh").string() == mid || a.get("shoreline").get("model").string() == mid ||
                                            a.get("distant").get("model").string() == mid;
                                 });
                             }),
                             models.end());
            }
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
        const bool ground_hit = host_->ray(from, dir, range, hit, why);
        // on the sea the water's surface is a place too: an island ground or a buoy goes onto the open sea
        const Place here = host_->link();
        const bool sea = here.valid && here.stage == "sea" && sea_pick(from, dir, range, ground_hit, hit);
        if (!ground_hit && !sea) return r;
        why.clear();
        if (sea) r["surface"] = "sea";
        r["position"] = rounded(hit);
        if (const Place link = here; link.valid) {
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
        if (p.get("kit").type != Value::Null) {
            // with a kit room (the import dialog's "Use the game's materials"): checked as import_model with kit checks it,
            // and which of its materials are the room's ("kit_report"; "kit_report_error" when the room can't be read)
            // (the file as the import keeps it: game materials' preview textures removed, "import_fixes" says so)
            std::string w;
            const Value listed = kit_room_materials(host_, p, w);
            std::vector<uint8_t> bytes;
            if (!prepare_kit_model_file(text(p, "file"), p, listed, bytes, r, why)) return r;
            if (host_) {
                if (w.empty()) r["kit_report"] = kit_material_report(bytes, listed, p);
                else r["kit_report_error"] = w;
            }
            ok = true;
            return r;
        }
        std::vector<uint8_t> bytes;  // what import_model would put into the package ("import_fixes": what it changes)
        if (!import_model_file(text(p, "file"), bytes, r, why)) return r;
        ok = true;
        return r;
    }
    if (command == "model_clips") {
        // a model of the open mod: its animation clips (the Inspector's walk and idle pickers)
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const Value* md = by_id(pkg->manifest.get("models"), text(p, "model"));
        if (!md) return why = "no model \"" + text(p, "model") + "\"", r;
        if (pkg->dir.empty()) return why = "the mod has no package folder", r;
        Value info;
        if (!check_model_file((fs::path(utf8_path(pkg->dir)) / utf8_path(md->get("source").string())).string(), info, why)) return r;
        r["clips"] = info.get("clips");
        r["skinned"] = info.get("kind").string() == "skinned";
        ok = true;
        return r;
    }
    if (command == "import_model") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        if (pkg->dir.empty()) return why = "the mod has no package folder", r;
        Value info;
        const bool kit = p.get("kit").type != Value::Null;
        if (!kit && (p.get("materials").type != Value::Null || p.get("material").type != Value::Null)) return why = "materials and material need kit (a room of the game)", r;
        std::vector<uint8_t> fixed;  // the model as the package keeps it (model_import.h; kit: its game materials' preview textures removed)
        std::string unlisted;
        const Value listed = kit ? kit_room_materials(host_, p, unlisted) : Value();
        if (kit ? !prepare_kit_model_file(text(p, "file"), p, listed, fixed, info, why) : !import_model_file(text(p, "file"), fixed, info, why)) return r;
        const fs::path from = utf8_path(text(p, "file"));
        // a kit model: which of its materials are the room's (said after the import); one with no texture that the room
        // doesn't have would make the build fail later, so it is refused now with the names to choose from
        Value kit_report;
        if (kit && !kit_materials_known(listed, fixed, p, kit_report, why)) return r;
        const std::string id = p.get("id").type == Value::String ? p.get("id").text : slug(from.stem().string());
        if (!id_ok(id)) return why = "a model id is 1 to 32 lower-case letters, digits, '-', '_' or '.'", r;
        if (by_id(pkg->manifest.get("models"), id)) return why = "the mod has a model \"" + id + "\" already", r;
        const std::string source = "models/" + id + ".glb";
        const fs::path to = utf8_path(pkg->dir) / utf8_path(source);
        std::error_code ec;
        bool created = false;
        auto write = [&](const fs::path& where) {  // the fixed model
            std::ofstream o(where, std::ios::binary | std::ios::trunc);
            o.write(reinterpret_cast<const char*>(fixed.data()), std::streamsize(fixed.size()));
            if (!o) ec = std::make_error_code(std::errc::io_error);
            return bool(o);
        };
        if (fs::exists(to, ec)) {
            std::ifstream b(to, std::ios::binary);
            const std::string x(fixed.begin(), fixed.end());
            const std::string y((std::istreambuf_iterator<char>(b)), {});
            if (x != y && !p.get("confirm").boolean) return why = "the package has a different " + source + ": \"confirm\": true replaces it", r;
            if (x != y) write(to);
        } else {
            fs::create_directories(to.parent_path(), ec);
            write(to);
            created = !ec;
        }
        if (ec) return why = "cannot copy the model into the package: " + ec.message(), r;
        Value entry;
        entry["id"] = id;
        entry["source"] = source;
        entry["kind"] = info.get("kind");
        if (kit) {
            entry["kit"] = p.get("kit");
            if (p.get("materials").type != Value::Null) entry["materials"] = p.get("materials");
            if (p.get("material").type != Value::Null) entry["material"] = p.get("material");
        }
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
        model_sources_[pkg->mod + "/" + id] = text(p, "file");  // Replace from file and Watch file start here
        if (kit_report.type == Value::Object) r["kit_report"] = kit_report;
        // what the import changed to make the file fit (model_import.h): said first, the file in the package is the fixed one
        std::string fixes;
        for (const auto& n : info.get("import_fixes").array) fixes += (fixes.empty() ? "" : " ") + n.string();
        if (!fixes.empty()) r["fixes"] = info.get("import_fixes");
        if (host_) {
            std::string note, w;
            // a model with collision of its own is prepared with it, so "Your model" can stand on it (issue #280)
            Value request = entry;
            if (!kit && info.get("kind").string() == "static" && model_has_collision_mesh(pkg->dir, pkg->manifest, id)) request["collision"] = true;
            if (removed_models_.count(pkg->mod + "/" + id)) request["replace"] = true;  // the id had another model in this session
            if (host_->prepare_model(pkg->mod, pkg->dir, request, note, w)) r["note"] = note;
            else r["note"] = "the model is in the package; it is prepared when the game starts (" + w + ")";
        }
        if (kit_report.type == Value::Object) r["note"] = kit_report_note(kit_report, p.get("kit")) + (r.get("note").string().empty() ? std::string() : " " + r.get("note").string());
        if (!fixes.empty()) r["note"] = "Imported with changes: " + fixes + (r.get("note").string().empty() ? std::string() : " " + r.get("note").string());
        if (info.get("import_warnings").type == Value::Array) {
            r["warnings"] = info.get("import_warnings");
            for (const auto& n : info.get("import_warnings").array) r["note"] = r.get("note").string() + (r.get("note").string().empty() ? "" : " ") + n.string();
        }
        return r;
    }
    if (command == "set_model_kit" || command == "reload_model") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const std::string id = text(p, "model");
        const Value* md = by_id(pkg->manifest.get("models"), id);
        if (!md) return why = "no model \"" + id + "\"", r;
        if (md->get("kind").string() != "kit") {
            if (command == "set_model_kit") return why = "only a kit model has a kit room", r;
            // a changed file in the package (exported again from Blender over it): the import's fixes applied to it in
            // place; the game takes it at the next start (a static or animated model is prepared at start-up)
            const fs::path file = fs::path(utf8_path(pkg->dir)) / utf8_path(md->get("source").string());
            std::vector<uint8_t> bytes;
            Value info;
            if (!import_model_file(file.string(), bytes, info, why)) return r;
            std::string fixes;
            for (const auto& n : info.get("import_fixes").array) fixes += (fixes.empty() ? "" : " ") + n.string();
            if (!fixes.empty()) {
                std::ofstream o(file, std::ios::binary | std::ios::trunc);
                o.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
                if (!o) return why = "cannot write " + file.string(), r;
                r["fixes"] = info.get("import_fixes");
            }
            r["model"] = *md;
            r["info"] = info;
            r["note"] = (fixes.empty() ? std::string() : "Fixed in the package: " + fixes + " ") + "The game takes the changed model at the next start.";
            ok = true;
            return r;
        }
        Value entry = *md;
        if (command == "set_model_kit") {
            if (p.get("kit").type != Value::Null) entry["kit"] = p.get("kit");
            if (p.object.contains("materials")) { if (p.get("materials").type == Value::Null) entry.object.erase("materials"); else entry["materials"] = p.get("materials"); }
            if (p.object.contains("material")) { if (p.get("material").string().empty()) entry.object.erase("material"); else entry["material"] = p.get("material"); }
        }
        // the package's file (maybe exported over from Blender): its game materials' preview textures removed in place
        Value info, check = entry;
        const fs::path in_package = fs::path(utf8_path(pkg->dir)) / utf8_path(entry.get("source").string());
        std::string unlisted;
        std::vector<uint8_t> kept;
        if (!prepare_kit_model_file(in_package.string(), check, kit_room_materials(host_, entry, unlisted), kept, info, why)) return r;
        if (info.get("import_fixes").type == Value::Array) {
            std::ofstream o(in_package, std::ios::binary | std::ios::trunc);
            o.write(reinterpret_cast<const char*>(kept.data()), std::streamsize(kept.size()));
            if (!o) return why = "cannot write " + in_package.string(), r;
            r["fixes"] = info.get("import_fixes");
        }
        if (command == "set_model_kit") {
            ok = change(*pkg, "Model " + id + " kit", [&](Value& m, std::string&) {
                for (auto& x : m["models"].array)
                    if (x.get("id").string() == id) x = entry;
                return true;
            }, r, why);
            if (!ok) return r;
        }
        r["model"] = entry;
        r["info"] = info;
        if (!host_) return why = "the game is not running: the model is built when it starts", r;
        std::string note;
        ok = host_->prepare_model(pkg->mod, pkg->dir, entry, note, why);
        if (ok) r["note"] = note;
        return r;
    }
    if (command == "replace_model") {
        // a model's file again under the same id (issue #287): its placements keep their places and show the new
        // version, collision included, when it is prepared. The file in the package is replaced; the one before is
        // kept as models/.previous/<file> (hidden, never exported). Not an undo step: replace it again to go back.
        Package* pkg = package(p, why);
        if (!pkg) return r;
        if (pkg->dir.empty()) return why = "the mod has no package folder", r;
        const std::string id = text(p, "model");
        const Value* md = by_id(pkg->manifest.get("models"), id);
        if (!md) return why = "no model \"" + id + "\"", r;
        const std::string key = pkg->mod + "/" + id;
        std::string file = text(p, "file");
        if (file.empty()) file = model_sources_.count(key) ? model_sources_[key] : std::string();
        if (file.empty()) return why = "say which file replaces the model (\"file\")", r;
        const bool kit = md->get("kind").string() == "kit";
        Value info;
        std::vector<uint8_t> fixed;
        std::string unlisted;
        const Value listed = kit ? kit_room_materials(host_, *md, unlisted) : Value();
        if (kit ? !prepare_kit_model_file(file, *md, listed, fixed, info, why) : !import_model_file(file, fixed, info, why)) return r;
        Value kit_report;
        if (kit && !kit_materials_known(listed, fixed, *md, kit_report, why)) return r;
        const std::string kind = info.get("kind").string();
        const auto users = model_actors(pkg->manifest, id);
        const std::string old_kind = md->get("kind").string("static");
        if (!kit && kind != old_kind && !users.empty())
            return why = "the model was " + old_kind + " and the new file is " + kind + "; placements use it (" + [&] {
                std::string s;
                for (const auto& u : users) s += (s.empty() ? "" : ", ") + u;
                return s;
            }() + "): import it as a new model instead", r;
        // placements standing on the model's own collision need it in the new file too
        const fs::path target = utf8_path(pkg->dir) / utf8_path(md->get("source").string());
        const bool had_collision = !kit && model_has_collision_mesh(pkg->dir, pkg->manifest, id);
        std::vector<std::string> standing;
        for (const auto& a : pkg->manifest.get("actors").array)
            if (a.get("collision").get("mesh").string() == id) standing.push_back(a.get("id").string());
        std::error_code ec;
        const fs::path previous = utf8_path(pkg->dir) / "models" / ".previous" / target.filename();
        std::ifstream old_in(target, std::ios::binary);
        const std::vector<uint8_t> old_bytes((std::istreambuf_iterator<char>(old_in)), std::istreambuf_iterator<char>());
        old_in.close();
        auto write = [&](const fs::path& where, const std::vector<uint8_t>& bytes) {
            const fs::path tmp = where.string() + ".editor-tmp";
            fs::create_directories(where.parent_path(), ec);
            {
                std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
                o.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
                if (!o) return false;
            }
            fs::rename(tmp, where, ec);
            return !ec;
        };
        if (!write(target, fixed)) return why = "cannot write " + md->get("source").string() + " in the package", r;
        // a kit model's collision is its own _col meshes too (story_kit_models), so it is looked for in its file as well
        const bool file_collision = (kit || kind == "static") && model_has_collision_mesh(pkg->dir, pkg->manifest, id);
        const bool has_collision = !kit && file_collision;
        if (!standing.empty() && !file_collision) {
            write(target, old_bytes);  // put back: the placements would lose their ground
            std::string s;
            for (const auto& u : standing) s += (s.empty() ? "" : ", ") + u;
            return why = "the new file has no collision mesh (\"<name>_col\"), which " + s + (standing.size() > 1 ? " stand" : " stands") +
                         " on: add it in Blender, or remove those placements first", r;
        }
        if (!old_bytes.empty() && old_bytes != fixed) write(previous, old_bytes);
        if (kind != old_kind) {
            // a model no placement uses yet may change between static and skinned
            ok = change(*pkg, "Model " + id + " is " + kind, [&](Value& m, std::string&) {
                for (auto& x : m["models"].array)
                    if (x.get("id").string() == id) x["kind"] = kind;
                return true;
            }, r, why);
            if (!ok) {
                write(target, old_bytes);
                return r;
            }
        }
        model_sources_[key] = file;
        if (auto w = watches_.find(key); w != watches_.end() && w->second.file == file) {  // the watch's own version now
            const auto size = fs::file_size(utf8_path(file), ec);
            const auto time = fs::last_write_time(utf8_path(file), ec);
            if (!ec) w->second.size = size, w->second.mtime = int64_t(std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count());
        }
        Value entry = *by_id(pkg->manifest.get("models"), id);
        r["model"] = entry;
        r["info"] = info;
        r["placements"] = array();
        for (const auto& u : users) r["placements"].array.push_back(Value(u));
        if (kit_report.type == Value::Object) r["kit_report"] = kit_report;
        if (info.get("import_fixes").type == Value::Array) r["fixes"] = info.get("import_fixes");
        std::string note = old_bytes == fixed ? "The file is the same as the model's: nothing changed." : "Replaced " + id + " from " + file_name(file) + ".";
        if (old_bytes != fixed) {
            if (users.empty()) note += " No placement uses it yet.";
            else note += " " + std::to_string(users.size()) + (users.size() > 1 ? " placements show" : " placement shows") + " the new version when it is ready.";
            if (had_collision != has_collision && has_collision) note += " It has collision of its own now: placements made from now on stand on it.";
            if (host_) {
                std::string w, prepared;
                Value request = entry;
                request["replace"] = true;
                if (has_collision) request["collision"] = true;
                if (!host_->prepare_model(pkg->mod, pkg->dir, request, prepared, w)) note += " It is prepared when the game starts (" + w + ").";
                else if (!prepared.empty()) note += " " + prepared;
            }
        }
        if (kit_report.type == Value::Object) note = kit_report_note(kit_report, md->get("kit")) + " " + note;
        if (kit && old_bytes != fixed && info.get("import_fixes").type == Value::Array) {  // what was changed to make the file fit, said first
            std::string fixes;
            for (const auto& n : info.get("import_fixes").array) fixes += (fixes.empty() ? "" : " ") + n.string();
            note = "Replaced with changes: " + fixes + " " + note;
        }
        r["note"] = note;
        r["changed"] = old_bytes != fixed;
        ok = true;
        return r;
    }
    if (command == "remove_model") {
        // a model out of the mod (issue #287), one undo step: refused while placements use it unless "actors": true
        // removes them with it; an item holding it is always said (change its held model first). The file goes to
        // models/.removed (hidden, never exported) and comes back with an undo.
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const std::string id = text(p, "model");
        if (!by_id(pkg->manifest.get("models"), id)) return why = "no model \"" + id + "\"", r;
        const auto join = [](const std::vector<std::string>& v) {
            std::string s;
            for (const auto& x : v) s += (s.empty() ? "" : ", ") + x;
            return s;
        };
        const auto items = model_items(pkg->manifest, id);
        if (!items.empty()) return why = "the item" + std::string(items.size() > 1 ? "s " : " ") + join(items) + " hold" + (items.size() > 1 ? "" : "s") +
                                         " model " + id + " (held_model): give " + (items.size() > 1 ? "them" : "it") + " another model first", r;
        const auto users = model_actors(pkg->manifest, id);
        const bool with_actors = p.get("actors").boolean;
        if (!users.empty() && !with_actors)
            return why = "placements use model " + id + ": " + join(users) + ". \"actors\": true removes them too", r;
        const Value before = pkg->manifest;
        ok = change(*pkg, "Remove model " + id + (users.empty() ? std::string() : " and " + std::to_string(users.size()) + (users.size() > 1 ? " placements" : " placement")),
                    [&](Value& m, std::string&) {
                        auto& models = m["models"].array;
                        models.erase(std::remove_if(models.begin(), models.end(), [&](const Value& x) { return x.get("id").string() == id; }), models.end());
                        if (models.empty()) m.object.erase("models");
                        if (!users.empty()) {
                            auto& actors = m["actors"].array;
                            actors.erase(std::remove_if(actors.begin(), actors.end(),
                                                        [&](const Value& a) { return std::find(users.begin(), users.end(), a.get("id").string()) != users.end(); }),
                                         actors.end());
                            if (actors.empty()) m.object.erase("actors");
                        }
                        return true;
                    },
                    r, why);
        if (!ok) return r;
        sync_model_files(*pkg, before, true);
        watches_.erase(pkg->mod + "/" + id);
        removed_models_.insert(pkg->mod + "/" + id);
        r["model"] = id;
        r["removed_actors"] = array();
        for (const auto& u : users) r["removed_actors"].array.push_back(Value(u));
        r["note"] = (r.get("note").string().empty() ? std::string() : r.get("note").string() + " ") + "Removed model " + id +
                    (users.empty() ? std::string() : " and its placements " + join(users)) + ". Undo brings it back.";
        return r;
    }
    if (command == "watch_model") {
        // a model's file watched (issue #287): when it changes on disk (and has settled), it replaces the model by
        // itself. For this editor session only; off by default.
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const std::string id = text(p, "model");
        if (!by_id(pkg->manifest.get("models"), id)) return why = "no model \"" + id + "\"", r;
        const std::string key = pkg->mod + "/" + id;
        if (!p.get("on").boolean) {
            watches_.erase(key);
            r["watching"] = false;
            r["note"] = "Model " + id + ": its file is no longer watched.";
            ok = true;
            return r;
        }
        std::string file = text(p, "file");
        if (file.empty()) file = model_sources_.count(key) ? model_sources_[key] : std::string();
        if (file.empty()) return why = "say which file to watch (\"file\"): the editor doesn't know where model " + id + " came from", r;
        std::error_code ec;
        if (!fs::is_regular_file(utf8_path(file), ec)) return why = "no file " + file, r;
        Watch w;
        w.file = file;
        // the version on disk now counts as imported unless it differs from the package's (then the first change brings it)
        w.size = fs::file_size(utf8_path(file), ec);
        const auto time = fs::last_write_time(utf8_path(file), ec);
        w.mtime = int64_t(std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count());
        w.seen_mtime = w.mtime, w.seen_size = w.size;
        watches_[key] = w;
        model_sources_[key] = file;
        r["watching"] = true;
        r["file"] = file;
        r["note"] = "Model " + id + ": " + file_name(file) + " is watched; saving it again (from Blender) replaces the model.";
        ok = true;
        return r;
    }
    if (command == "list_model_files") {
        // the open mod's models with the file each came from in this session and whether it is watched (the panel)
        Package* pkg = package(p, why);
        if (!pkg) return r;
        r["models"] = array();
        for (const auto& m : pkg->manifest.get("models").array) {
            const std::string key = pkg->mod + "/" + m.get("id").string();
            Value v;
            v["model"] = m.get("id");
            v["file"] = model_sources_.count(key) ? model_sources_[key] : std::string();
            v["watching"] = watches_.count(key) > 0;
            v["placements"] = array();
            for (const auto& u : model_actors(pkg->manifest, m.get("id").string())) v["placements"].array.push_back(Value(u));
            v["items"] = array();
            for (const auto& u : model_items(pkg->manifest, m.get("id").string())) v["items"].array.push_back(Value(u));
            r["models"].array.push_back(v);
        }
        ok = true;
        return r;
    }
    if (command == "check_package") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        r["problems"] = array();
        auto problems = package_problems(pkg->mod, pkg->manifest, all_manifests(), pkg->dir);
        for (auto& pr : placement_problems(host_, pkg->mod, pkg->manifest)) problems.push_back(std::move(pr));
        for (const auto& pr : problems) {
            Value v;
            v["message"] = pr.message;
            v["kind"] = pr.kind;
            if (!pr.id.empty()) v["id"] = pr.id;
            if (!pr.field.empty()) v["field"] = pr.field;
            r["problems"].array.push_back(v);
        }
        r["valid"] = r["problems"].array.empty();
        // models this computer could not prepare (the converter's reason): their actors are not in the game. A warning,
        // since another computer may prepare them; never "after a restart", a restart fails the same way (issue #280)
        if (host_)
            for (const auto& m : pkg->manifest.get("models").array) {
                const std::string failed = host_->model_failure(pkg->mod, m.get("id").string());
                if (failed.empty()) continue;
                Value v;
                v["message"] = "model " + m.get("id").string() + " could not be prepared for the game: " + failed + ". Its actors are not in the game; a restart won't change that until the cause is fixed";
                v["kind"] = "model";
                v["id"] = m.get("id").string();
                v["severity"] = "warning";
                r["problems"].array.push_back(v);
            }
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
        auto problems = package_problems(pkg->mod, pkg->manifest, all_manifests(), pkg->dir);
        for (auto& pr : placement_problems(host_, pkg->mod, pkg->manifest)) problems.push_back(std::move(pr));
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
        const bool place = p.get("id").type == Value::Null && read_point(p.get("position"), at);  // a place instead of an actor
        if (!place && (!a || !read_point(a->get("position"), at))) return why = "no placed actor \"" + text(p, "id") + "\"", r;
        if (!host_) return why = "the game is not running", r;
        // a world object (a building, a pond, a river): its whole extent in the picture, from its middle
        float distance = 700;
        bool framed = false;
        std::vector<story_notebook::ManifestActor> actors;
        std::string w;
        if (story_notebook::parse_actors(pkg->manifest, actors, w))
            for (const auto& actor : actors)
                if (actor.id == text(p, "id") && actor.world_object()) {
                    Value q;
                    if (!host_->world_object_query(pkg->mod, actor, false, q, w)) break;
                    WorldPoint lo, hi;
                    if (!read_point(q.get("minimum"), lo) || !read_point(q.get("maximum"), hi)) break;
                    at = {(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2};
                    distance = std::clamp(std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z}) * 0.9f, 700.0f, 90000.0f);  // up to a square-wide one
                    framed = true;
                }
        // an actor drawing one of the mod's models: the model where it is drawn, which for a model made away from its
        // origin is not the actor's place (issue #280)
        if (WorldPoint lo, hi; !place && !framed && a && a->get("kind").string() == "mod" && model_box(pkg->dir, pkg->manifest, a->get("model").string(), lo, hi)) {
            WorldPoint low, high;
            placed_bounds(lo, hi, *a, low, high);
            at = {(low.x + high.x) / 2, (low.y + high.y) / 2, (low.z + high.z) / 2};
            distance = std::clamp(std::max({high.x - low.x, high.y - low.y, high.z - low.z}) * 0.9f, 700.0f, 90000.0f);
        }
        if (WorldPoint eye; read_point(p.get("eye"), eye)) {  // from a given place (below an island, say)
            if (!host_->look(eye, at, why)) return r;
            r["requested"] = true;
            ok = true;
            return r;
        }
        if (p.get("distance").type == Value::Number) distance = float(std::clamp(p.get("distance").number, 200.0, 100000.0));
        const float yaw = p.get("yaw").type == Value::Number ? float(p.get("yaw").number) : std::nanf("");
        if (!host_->focus_view(at, distance, yaw, why)) return r;
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

// Whether a model's GLB has collision of its own: a mesh, or the node drawing it, named "<name>_col" (static_models::
// validate_glb: those meshes are collision, not drawn; docs/world-objects.md "Author collision in the GLB").
bool model_has_collision_mesh(const std::string& dir, const Value& manifest, const std::string& model) {
    const Value* m = by_id(manifest.get("models"), model);
    if (!m || dir.empty()) return false;
    std::ifstream f(fs::path(utf8_path(dir)) / utf8_path(m->get("source").string()), std::ios::binary);
    const std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 28 || std::memcmp(b.data(), "glTF", 4) || static_models::le32(b.data() + 16) != 0x4E4F534A) return false;
    const size_t length = static_models::le32(b.data() + 12);
    if (length > b.size() - 20) return false;
    Value doc;
    try {
        doc = mods::json::parse(std::string(reinterpret_cast<const char*>(b.data() + 20), length));
    } catch (...) {
        return false;
    }
    const auto named = [](const Value& v) { return v.get("name").type == Value::String && v.get("name").text.ends_with("_col"); };
    const auto& meshes = doc.get("meshes").array;
    for (const auto& node : doc.get("nodes").array) {
        const auto& mesh = node.get("mesh");
        if (mesh.type != Value::Number || mesh.number < 0 || size_t(mesh.number) >= meshes.size()) continue;
        if (named(node) || named(meshes[size_t(mesh.number)])) return true;
    }
    return false;
}

}  // namespace editor
