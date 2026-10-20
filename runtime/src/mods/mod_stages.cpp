// SPDX-License-Identifier: MPL-2.0
// Mod stages in the running game: see mod_stages.h.
#include "mod_stages.h"

#include <algorithm>
#include <deque>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>

#include "../editor/stage_files.h"
#include "../platform/process.h"
#include "../savestate.h"
#include "guest_addr.h"
#include "runtime.h"
#include "story_store.h"
#include "mod_hash.h"
#include "../platform/host.h"
#include "guest_build.h"
#include "guest_hud.h"
#include "guest_png.h"
#include "../input.h"
#include "story_world_placement.h"
#include "../true60.h"

extern "C" void f_020277AC_orig(Cpu* c);
extern "C" void f_025217F8_orig(Cpu* c);
extern "C" void f_02522198_orig(Cpu* c);
extern "C" void f_0273AA24_orig(Cpu* c);
extern "C" void f_025B9C9C_orig(Cpu* c);
extern "C" void f_025B9D24_orig(Cpu* c);
extern "C" void f_025B9174_orig(Cpu* c);

namespace mod_stages {
void draw_map();
namespace {
namespace fs = std::filesystem;

struct Built {
    Info info;
    Stage stage;
    std::string dir;                          // the stage cache folder
    std::map<std::string, std::string> files;  // "<name>_Stage.szs", "<name>_Room<n>.szs" -> path
    uint16_t key = 0;                          // the mod save area key (save_key)
    uint16_t group_key = 0;                    // its save group's key (save_key of the group)
    unsigned group_index = 0;                  // the save group's number this session (its dungeon switches)
    std::map<std::string, std::string> names;  // the package's stage ids -> game names (its warps)
    // the dungeon map: each room's picture (the builder's, seen from above) and where it lies
    struct MapRoom {
        unsigned room = 0;
        int floor = 1;
        double x = 0, z = 0, units = 1;
        uint32_t width = 0, height = 0;
        std::shared_ptr<std::vector<uint8_t>> rgba;
        uint32_t image = 0;  // the HUD image while the stage is shown (0: none yet)
    };
    std::vector<MapRoom> map_rooms;
    std::vector<std::string> notes;  // the builder's remarks
    std::string music_like;
    int music_id = -1;                         // spotNameToId's answer for the stage (-1: not looked up yet)
};
std::mutex g_mutex;
// The built stages. Changed only before the game runs (start-up) and then only on the game thread
// (apply_pending: the editor's rebuilds), always under g_mutex; the game thread's own readers (hooks, logic) read
// without it. A deque: an element keeps its address when stages are added, and a rebuilt stage replaces its
// element in place.
std::deque<Built> g_stages;
std::vector<Built> g_pending;  // rebuilt stages waiting for the game thread (under g_mutex)
std::string g_game_dir;

// the game's play state (savestate.cpp kPlay): the start stage (name[8], point s16, room s8, layer s8) and the
// next stage (the same and enabled u8, wipe u8)
uint32_t play() { return GD(0x1046F0B0); }
uint32_t start_stage() { return play() + 0x5134; }
uint32_t next_stage() { return play() + 0x5140; }
const uint32_t kSavePtr = GD(0x101F84DC);  // the save area; dSv_player_return_place_c at +0x50 (name[8], room s8, point u8)
constexpr uint32_t kReturnPlace = 0x50;
// JAIZelBasic::spotNameToId's table (120 stage names; a stage's spot id is its index + 1) and the spot ids whose
// scene reads a dungeon's save bits (the boss rooms: mod_stages_manifest.h music_allowed)
const uint32_t kSpotNames = GD(0x1018E6D4);
constexpr uint32_t kSpots = 120;
constexpr uint32_t kStoryScenes[] = {0x02, 0x07, 0x18, 0x25, 0x2D};

std::string guest_text(uint32_t ea, size_t max) {
    if (ea < mem::kMem2Start || ea >= mem::kMem2End) return {};
    std::string s;
    for (size_t i = 0; i < max; i++) {
        const char ch = char(ld8(ea + uint32_t(i)));
        if (!ch) break;
        s.push_back(ch);
    }
    return s;
}
std::string current_stage() { return guest_text(start_stage(), 8); }

const Built* by_name(const std::string& name) {
    for (const auto& b : g_stages)
        if (b.info.name == name) return &b;
    return nullptr;
}

// the game's stages (room archives in Stage/ and the packs) and its Stage archive names, read once
const std::set<std::string>& game_stages() {
    static std::set<std::string> names;
    static bool read = false;
    if (!read) {
        read = true;
        for (const auto& [stage, rooms] : editor::game_stage_rooms(g_game_dir)) names.insert(stage);
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(fs::path(g_game_dir) / "content" / "Common" / "Stage", ec)) {
            const std::string f = e.path().filename().string();
            const size_t at = f.find('_');
            if (at != std::string::npos && at > 0) names.insert(f.substr(0, at));
        }
    }
    return names;
}
bool game_room(const std::string& stage, unsigned room) {
    for (const auto& [s, rooms] : editor::game_stage_rooms(g_game_dir))
        if (s == stage) return std::find(rooms.begin(), rooms.end(), int(room)) != rooms.end();
    return false;
}

std::string read_file(const fs::path& p, size_t limit) {
    std::ifstream f(p, std::ios::binary);
    std::string s(limit + 1, '\0');
    f.read(s.data(), std::streamsize(s.size()));
    s.resize(size_t(f.gcount()));
    return s.size() > limit ? std::string() : s;
}
bool write_file(const fs::path& p, const std::string& body) {
    const fs::path temp = p.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(body.data(), std::streamsize(body.size()));
        if (!out) return false;
    }
    std::error_code ec;
    fs::rename(temp, p, ec);
    return !ec;
}

// one stage: its recipe, the builder, the result checked and registered; false with a reason
bool build(const std::string& mod, const Stage& s, const std::string& name, const std::map<std::string, std::string>& names,
           const std::string& package_dir, const std::string& data_dir, const std::vector<std::string>& python, const std::string& tool,
           std::string& why, Built& b) {
    // what the player's game files must have: the template's archives, every kit room, the entrance and the
    // game rooms the exits and entries name (a missing archive stops the game)
    if (!game_stages().count(s.template_stage)) return why = "the game has no stage " + s.template_stage, false;
    for (const auto& r : s.rooms)
        if (!game_room(r.kit_stage.empty() ? s.template_stage : r.kit_stage, r.kit))
            return why = "the kit room " + (r.kit_stage.empty() ? s.template_stage : r.kit_stage) + " room " + std::to_string(r.kit) + " is not in the game", false;
    if (!game_room(s.entrance.stage, s.entrance.room))
        return why = "the entrance " + s.entrance.stage + " room " + std::to_string(s.entrance.room) + " is not in the game", false;
    for (const auto& r : s.rooms)
        for (const auto& e : r.exits)
            if (!e.to.own && !game_room(e.to.stage, e.to.room))
                return why = "exit " + e.id + " leads to " + e.to.stage + " room " + std::to_string(e.to.room) + ", which is not in the game", false;
    for (const auto& w : s.warps)
        if (!w.to.own && !game_room(w.to.stage, w.to.room))
            return why = "warp " + w.id + " leads to " + w.to.stage + " room " + std::to_string(w.to.room) + ", which is not in the game", false;
    for (const auto& e : s.entries)
        if (!game_room(e.stage, e.room)) return why = "entry " + e.id + " is in " + e.stage + " room " + std::to_string(e.room) + ", which is not in the game", false;
    if (python.empty() || tool.empty() || data_dir.empty()) return why = "the stage builder is unavailable", false;
    const fs::path dir = fs::path(data_dir).parent_path() / ".mod-stages" / name;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec || fs::is_symlink(dir) || fs::is_symlink(dir.parent_path())) return why = "the stage cache folder is unavailable", false;
    std::string ground_error;
    auto ground = [&](const std::string& path) {
        fs::path p = package_dir;
        for (const auto& part : fs::path(path)) {
            p /= part;
            if (fs::is_symlink(p)) ground_error = "the ground model's path uses a symlink";
        }
        if (!fs::is_regular_file(p)) ground_error = "the ground model " + path + " is missing";
        return p.string();
    };
    const Value recipe_value = recipe(s, name, names, ground);
    if (!ground_error.empty()) return why = ground_error, false;
    const fs::path recipe_path = dir / "recipe.json";
    if (!write_file(recipe_path, mods::json::dump(recipe_value))) return why = "the stage recipe could not be written", false;
    auto command = python;
    command.insert(command.end(), {tool, "--game", g_game_dir, "--recipe", recipe_path.string(), "--out", dir.string()});
    const auto process = host::run_process(command);
    const auto end = process.output.find_last_not_of("\r\n");
    Value result;
    if (end != std::string::npos) {
        const auto line = process.output.rfind('\n', end);
        const auto begin = line == std::string::npos ? 0 : line + 1;
        try {
            result = mods::json::parse(process.output.substr(begin, end - begin + 1));
        } catch (...) {
        }
    }
    if (!process.error.empty() || result.get("ok").type != Value::Bool || !result.get("ok").boolean || process.code)
        return why = "the stage builder failed: " + result.get("error").string(process.error.empty() ? "no result" : process.error), false;
    b = Built{};
    b.info.mod = mod;
    b.info.id = s.id;
    b.info.name = name;
    b.info.entrance = s.entrance;
    b.stage = s;
    b.dir = dir.string();
    b.key = save_key(mod, s.id);
    b.group_key = save_key(mod, "group:" + s.save_group);
    b.names = names;
    {
        std::lock_guard lock(g_mutex);
        std::map<uint16_t, unsigned> seen;
        for (const auto* list : {&g_stages})
            for (const auto& o : *list) seen.emplace(o.group_key, o.group_index);
        for (const auto& o : g_pending) seen.emplace(o.group_key, o.group_index);
        auto it = seen.find(b.group_key);
        b.group_index = it != seen.end() ? it->second : unsigned(seen.size());
    }
    b.music_like = s.music_like;
    for (const auto& [file, digest] : result.get("files").object) {
        if (file.rfind(name + "_map", 0) == 0 && file.size() > 4 && file.substr(file.size() - 4) == ".png") continue;  // the map's pictures (below)
        const bool ours = file == name + "_Stage.szs" || (file.rfind(name + "_Room", 0) == 0 && file.size() > 4 && file.substr(file.size() - 4) == ".szs");
        if (!ours || !fs::is_regular_file(dir / file) || fs::is_symlink(dir / file)) return why = "the stage builder returned an unexpected file " + file, false;
        b.files[file] = (dir / file).string();
        register_file(file, b.files[file]);
    }
    if (!b.files.count(name + "_Stage.szs")) return why = "the stage builder wrote no stage archive", false;
    // the stage's shader template (docs/mod-stages.md, "Shaders"): prepared with the game's own by setup; a
    // shader cache prepared before the mod came compiles them during play until "Rebuild shader cache"
    if (!s.shaders.empty()) {
        const fs::path t = fs::path(package_dir) / s.shaders;
        std::error_code e;
        if (!fs::is_regular_file(t, e) || fs::is_symlink(t, e))
            LOG("[mod-stages] %s: its shader template %s is missing", s.id.c_str(), s.shaders.c_str());
        else {
            const std::string digest = mods::hash::sha256_file(t);
            bool prepared = false;
            try {
                std::ifstream f(fs::path(host::config_dir()) / "shader-prebuild" / "active.json");
                const Value active = mods::json::parse(std::string(std::istreambuf_iterator<char>(f), {}));
                for (const auto& h : active.get("identity").get("mod_templates").array) prepared |= h.string() == digest;
            } catch (...) {
            }
            LOG("[mod-stages] %s: its shader template %s %s", s.id.c_str(), s.shaders.c_str(),
                prepared ? "is in the prepared shader cache" : "is not prepared yet: setup's Repair prepares it (until then its shaders compile during play)");
        }
    }
    for (const auto& r : s.rooms) {
        if (!b.files.count(name + "_Room" + std::to_string(r.room) + ".szs")) return why = "the stage builder wrote no archive of room " + std::to_string(r.room), false;
        b.info.rooms.push_back(r.room);
        for (const auto& sp : r.spawns) b.info.spawns.push_back({r.room, sp.point, sp.position, sp.angle});
    }
    if (s.map)
        for (const auto& room : result.get("rooms").array) {
            const Value& m = room.get("map");
            if (m.type != Value::Object) continue;
            Built::MapRoom mr;
            mr.room = unsigned(room.get("room").number);
            mr.floor = int(m.get("floor").number);
            mr.x = m.get("x").number, mr.z = m.get("z").number, mr.units = m.get("units").number;
            const fs::path file = dir / m.get("file").string();
            if (fs::is_symlink(file) || !fs::is_regular_file(file) || fs::file_size(file) > (4u << 20)) continue;
            try {
                const std::string bytes = read_file(file, 4u << 20);
                auto pixels = guestmods::hud::decode_png(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()));
                mr.width = pixels.width, mr.height = pixels.height;
                mr.rgba = std::make_shared<std::vector<uint8_t>>(std::move(pixels.rgba));
            } catch (const std::exception& e) {
                LOG("[mod-stages] %s: the map picture of room %u is unreadable (%s)", s.id.c_str(), mr.room, e.what());
                continue;
            }
            b.map_rooms.push_back(std::move(mr));
        }
    LOG("[mod-stages] %s: stage %s is %s (%zu rooms, %s)", mod.c_str(), s.id.c_str(), name.c_str(), s.rooms.size(),
        result.get("cached").boolean ? "from the stage cache" : "built from the game files");
    for (const auto& room : result.get("rooms").array) {
        LOG("[mod-stages] %s room %d: %d visible and %d collision triangles", name.c_str(), int(room.get("room").number),
            int(room.get("visible_triangles").number), int(room.get("collision_triangles").number));
        for (const auto& note : room.get("spawn_notes").array) {
            LOG("[mod-stages] %s: %s", s.id.c_str(), note.string().c_str());
            b.notes.push_back(note.string());
        }
    }
    return true;
}

// the mod save area records of the stage a Quest Log was saved in (story_mail_book.h)
void clear_resume(story_mail::Book& book) {
    for (unsigned r = 0; r < story_block::kAreaRecords; ++r) {
        const uint8_t kind = book.record_kind(r);
        if (kind == story_mail::kRecordStageResume || kind == story_mail::kRecordStageResumePoint) book.erase(kind, book.record_key(r));
    }
}

void write_next_stage(const std::string& name, unsigned room, unsigned point) {
    char text[8] = {};
    memcpy(text, name.data(), std::min<size_t>(name.size(), 7));
    for (int i = 0; i < 8; i++) st8(next_stage() + i, uint8_t(text[i]));
    st16(next_stage() + 8, uint16_t(point));
    st8(next_stage() + 10, uint8_t(room));
}
}  // namespace

namespace {
// a package's stages named and built; `live` (the editor's rebuild): queued for the game thread, else (start-up)
// added at once
std::vector<BuildResult> declare_stages(const std::string& mod, const std::vector<Stage>& list, const std::string& package_dir,
                                        const std::string& data_dir, const std::vector<std::string>& python, const std::string& tool, bool live) {
    std::vector<BuildResult> results;
    static bool sources = false;
    if (!sources) {  // the editor's spawn points and the placement index read the stage cache too
        sources = true;
        editor::set_stage_archive_source(cached_file);
        story_world_placement::set_stage_archive_source(cached_file);
    }
    std::map<std::string, std::string> names;
    for (const auto& s : list) {
        const std::string name = allocate_name(mod, s.id, [&](const std::string& n) {
            if (game_stages().count(n)) return true;
            for (const auto& [id, other] : names)
                if (other == n) return true;
            std::lock_guard lock(g_mutex);
            const Built* other = by_name(n);
            return other != nullptr && !(other->info.mod == mod && other->info.id == s.id);  // its own earlier build is no other
        });
        if (name.empty()) exception_report::raise("Stages " + mod + ": no free stage name");
        names[s.id] = name;
    }
    for (const auto& s : list) {
        BuildResult r;
        r.id = s.id, r.name = names[s.id];
        Built b;
        try {
            r.ok = build(mod, s, names[s.id], names, package_dir, data_dir, python, tool, r.error, b);
            if (!r.ok && r.error.empty()) r.error = "unknown";
        } catch (const std::exception& e) {
            r.ok = false, r.error = e.what();
        }
        if (!r.ok) {
            LOG("[mod-stages] %s: stage %s is left out: %s", mod.c_str(), s.id.c_str(), r.error.c_str());
            results.push_back(r);
            continue;
        }
        r.rooms = unsigned(b.info.rooms.size());
        r.notes = b.notes;
        std::lock_guard lock(g_mutex);
        if (live) g_pending.push_back(std::move(b));
        else g_stages.push_back(std::move(b));
        results.push_back(r);
    }
    return results;
}
}  // namespace

void declare(const std::string& mod, const Value& manifest, const std::string& package_dir, const std::string& data_dir,
             const std::vector<std::string>& python, const std::string& tool, const std::string& game_dir) {
    std::vector<Stage> list;
    std::string why;
    if (!parse(manifest, list, why)) exception_report::raise("Stages " + mod + ": " + why);
    if (list.empty()) return;
    g_game_dir = game_dir;
    declare_stages(mod, list, package_dir, data_dir, python, tool, false);
}

std::vector<BuildResult> rebuild(const std::string& mod, const Value& manifest, const std::string& package_dir, const std::string& data_dir,
                                 const std::string& game_dir, const std::string& only) {
    std::vector<Stage> list;
    std::string why;
    if (!parse(manifest, list, why)) return {BuildResult{"", "", why}};
    if (!only.empty()) {
        std::vector<Stage> one;
        for (const auto& s : list)
            if (s.id == only) one.push_back(s);
        if (one.empty()) return {BuildResult{only, "", "the package declares no stage \"" + only + "\""}};
        // its exits and warps may name the package's other stages: their names come with them
        for (const auto& s : list)
            if (s.id != only) one.push_back(s);
        list.swap(one);
    }
    if (g_game_dir.empty()) g_game_dir = game_dir;
    std::vector<std::string> python;
    std::string tool;
    try {
        const auto bridge = guestmods::BuildBridge::installed(std::string());
        python = bridge.python;
        tool = (std::filesystem::path(bridge.builder).parent_path().parent_path() / "story_mods" / "stage_builder.py").string();
    } catch (const std::exception& e) {
        return {BuildResult{only, "", std::string("the stage builder is unavailable: ") + e.what()}};
    }
    auto results = declare_stages(mod, list, package_dir, data_dir, python, tool, true);
    if (!only.empty()) {
        // only the asked one is applied; the others were built (or taken from the cache) for their names only
        std::lock_guard lock(g_mutex);
        std::erase_if(g_pending, [&](const Built& b) { return b.info.mod == mod && b.info.id != only && by_name(b.info.name); });
        std::erase_if(results, [&](const BuildResult& r) { return r.id != only; });
    }
    return results;
}

bool declared() {
    std::lock_guard lock(g_mutex);
    return !g_stages.empty();
}

std::vector<Info> stages() {
    std::lock_guard lock(g_mutex);
    std::vector<Info> out;
    for (const auto& b : g_stages) out.push_back(b.info);
    for (const auto& b : g_pending)  // rebuilt, about to take effect: a warp may name them already
        if (std::none_of(out.begin(), out.end(), [&](const Info& i) { return i.name == b.info.name; })) out.push_back(b.info);
    return out;
}

bool find(const std::string& name, Info& out) {
    std::lock_guard lock(g_mutex);
    const Built* b = by_name(name);
    if (!b) return false;
    out = b->info;
    return true;
}

std::string resolve(const std::string& reference) {
    std::lock_guard lock(g_mutex);
    if (reference.rfind("stage:", 0) == 0) {
        const std::string rest = reference.substr(6);
        const size_t slash = rest.find('/');
        if (slash == std::string::npos) return {};
        for (const auto& b : g_stages)
            if (b.info.mod == rest.substr(0, slash) && b.info.id == rest.substr(slash + 1)) return b.info.name;
        return {};
    }
    return by_name(reference) ? reference : std::string();
}

bool missing(const std::string& stage) {
    if (!allocated_name(stage)) return false;
    {
        std::lock_guard lock(g_mutex);
        if (by_name(stage)) return false;
    }
    std::error_code ec;
    return g_game_dir.empty() || !fs::is_regular_file(fs::path(g_game_dir) / "content" / "Common" / "Stage" / (stage + "_Stage.szs"), ec);
}

void hide_map_for_rebuild();
// the editor's rebuilt stages take effect here, on the game thread; the stage Link is in loads again
void apply_pending() {
    std::vector<Built> pending;
    {
        std::lock_guard lock(g_mutex);
        if (g_pending.empty()) return;
        pending.swap(g_pending);
    }
    hide_map_for_rebuild();
    const std::string here = current_stage();
    bool reload = false;
    for (auto& b : pending) {
        LOG("[mod-stages] %s: stage %s (%s) rebuilt", b.info.mod.c_str(), b.info.id.c_str(), b.info.name.c_str());
        reload |= b.info.name == here;
        std::lock_guard lock(g_mutex);
        Built* old = nullptr;
        for (auto& o : g_stages)
            if (o.info.name == b.info.name) old = &o;
        if (old) *old = std::move(b);
        else g_stages.push_back(std::move(b));
    }
    (void)reload;  // the editor loads the stage again where Link stands (its reload_room, which waits for him to be free)
}

// An updraft entry: the wind lifts Link (the game's own fall, held up as the moon jump cheat does) until he is
// `height` above where he stepped in, then the stage takes over with a fade.
struct Lift {
    bool on = false;
    std::string name, id, room_key;  // the stage, the entry, the game room it is in
    unsigned room = 0, point = 0;
    float start_y = 0, height = 0;
    unsigned steps = 0;  // 30 Hz steps since it began
} g_lift;
constexpr uint32_t kLinkProc = 0x65F0, kLinkSpeedY = 0x340, kLinkPosY = 0x318, kLinkGroundFlags = 0x834, kLinkFallHeight = 0x6A48;  // cheats.cpp's
constexpr uint32_t kProcFall = 0x27;
constexpr float kLiftSpeed = 18.f;      // units a 30 Hz step (the moon jump's is 25)
constexpr unsigned kLiftMaxSteps = 300;  // ten seconds: a lift that can't rise (a ceiling) still ends

void enter(const std::string& name, unsigned room, unsigned point, uint8_t wipe) {
    write_next_stage(name, room, point);
    st8(next_stage() + 11, 0xFF);  // layer: the game's choice
    st8(next_stage() + 13, wipe);
    st8(next_stage() + 12, 1);     // enabled
}

void logic(Cpu*) {
    apply_pending();
    if (g_stages.empty()) return;
    draw_map();
    static std::string last_room;
    static std::set<std::string> armed;  // entries Link was outside of since he came into the room
    const auto p = ss::link_place();
    if (!p.valid) return;
    const std::string room_key = p.stage + "/" + std::to_string(p.room);
    if (room_key != last_room) {
        last_room = room_key;
        armed.clear();  // arriving inside a box (back from the stage, say) takes nobody anywhere
        g_lift.on = false;
    }
    if (g_lift.on) {
        if (p.pos[1] >= g_lift.start_y + g_lift.height || g_lift.steps >= kLiftMaxSteps) {
            g_lift.on = false;
            enter(g_lift.name, g_lift.room, g_lift.point, 0);
            LOG("[mod-stages] updraft %s: Link rose %.0f units in %u steps; into %s room %u point %u", g_lift.id.c_str(), p.pos[1] - g_lift.start_y,
                g_lift.steps, g_lift.name.c_str(), g_lift.room, g_lift.point);
        }
        return;
    }
    for (const auto& b : g_stages)
        for (const auto& e : b.stage.entries) {
            if (e.stage != p.stage || int(e.room) != p.room) continue;
            const std::string key = b.info.name + "/" + e.id;
            if (!inside(e, p.pos[0], p.pos[1], p.pos[2])) {
                armed.insert(key);
                continue;
            }
            if (!armed.count(key) || !ss::control_problem().empty()) continue;
            armed.erase(key);
            if (e.style == "updraft") {
                g_lift = Lift{true, b.info.name, e.id, room_key, e.to_room, e.to_point, p.pos[1], float(e.height), 0};
                LOG("[mod-stages] updraft %s/%s: the wind lifts Link (%.0f units)", b.info.mod.c_str(), e.id.c_str(), e.height);
                return;
            }
            enter(b.info.name, e.to_room, e.to_point, 0);  // wipe: fade
            LOG("[mod-stages] entry %s/%s: Link goes into %s (%s room %u point %u)", b.info.mod.c_str(), e.id.c_str(), b.info.id.c_str(),
                b.info.name.c_str(), e.to_room, e.to_point);
            return;
        }
    // the return warps of the stage Link is in
    for (const auto& b : g_stages) {
        if (b.info.name != p.stage) continue;
        for (const auto& w : b.stage.warps) {
            if (int(w.room) != p.room) continue;
            const std::string key = b.info.name + "/warp/" + w.id;
            // a warp that waits for a chest (the boss room's heart container): closed counts as outside, so it
            // takes Link once he steps into it with the chest open
            const bool in = inside(w, p.pos[0], p.pos[1], p.pos[2]);
            static std::set<std::string> told;  // a warp that waits: said once each time Link stands in it
            if (in && w.after_flag >= 0 && !chest_open(unsigned(w.after_flag))) {
                if (told.insert(key).second) LOG("[mod-stages] warp %s/%s waits for chest %s to be opened", b.info.id.c_str(), w.id.c_str(), w.after.c_str());
                armed.insert(key);
                continue;
            }
            if (!in) {
                told.erase(key);
                armed.insert(key);
                continue;
            }
            if (!armed.count(key) || !ss::control_problem().empty()) continue;
            armed.erase(key);
            const std::string to = !w.to.own ? w.to.stage : w.to.stage.empty() ? b.info.name : b.names.at(w.to.stage);
            enter(to, w.to.room, w.to.point, 9);  // wipe: the warp light (the game's own cave exits and its turn restart use it)
            LOG("[mod-stages] warp %s/%s: Link goes to %s room %u point %u", b.info.id.c_str(), w.id.c_str(), to.c_str(), w.to.room, w.to.point);
            return;
        }
    }
}

Value templates(const std::string& game_dir, std::string& why) {
    static Value cached;
    static std::mutex mu;
    std::lock_guard lock(mu);
    if (cached.type == Value::Array) return cached;
    std::vector<std::string> command;
    try {
        const auto bridge = guestmods::BuildBridge::installed(std::string());
        command = bridge.python;
        command.push_back((std::filesystem::path(bridge.builder).parent_path().parent_path() / "story_mods" / "stage_builder.py").string());
    } catch (const std::exception& e) {
        return why = std::string("the stage builder is unavailable: ") + e.what(), Value();
    }
    command.insert(command.end(), {"--game", g_game_dir.empty() ? game_dir : g_game_dir, "--templates"});
    const auto process = host::run_process(command);
    Value result;
    try {
        result = mods::json::parse(process.output);
    } catch (...) {
    }
    if (!process.error.empty() || process.code || !result.get("ok").boolean || result.get("templates").type != Value::Array)
        return why = "the stage builder could not list the templates: " + result.get("error").string(process.error.empty() ? "no result" : process.error), Value();
    cached = result.get("templates");
    for (auto& t : cached.array) t["music"] = music_allowed(t.get("stage").string());  // its music can be borrowed (music_like)
    return cached;
}

bool enter_stage(const std::string& mod, const std::string& id, unsigned room, unsigned point) {
    for (const auto& b : g_stages) {
        if (b.info.mod != mod || b.info.id != id) continue;
        bool spawn = false;
        for (const auto& sp : b.info.spawns) spawn |= sp.room == room && sp.point == point;
        if (!spawn) {
            LOG("[mod-stages] %s: the stage action names room %u point %u, which stage %s has no spawn at", mod.c_str(), room, point, id.c_str());
            return false;
        }
        g_lift.on = false;
        enter(b.info.name, room, point, 0);
        LOG("[mod-stages] %s: a stage action takes Link into %s (%s room %u point %u)", mod.c_str(), id.c_str(), b.info.name.c_str(), room, point);
        return true;
    }
    LOG("[mod-stages] %s: the stage action names stage %s, which is not built", mod.c_str(), id.c_str());
    return false;
}

void link_step(Cpu* c, uint32_t link, bool before) {
    if (!g_lift.on || !before || link < 0x10000 || link > 0xFFFF9000) return;
    if (true60::dt() < 1.f) return;  // an interpolated preview frame: the 30 Hz steps lift him
    if (ld32(link + kLinkProc) != kProcFall) {
        const Cpu saved = *c;
        c->f[1].ps0 = c->f[1].ps1 = 5.;
        guest_call(c, GC(0x023F6564), {link, 0});  // daPy_lk_c::procFall_init
        *c = saved;
    }
    st32(link + kLinkGroundFlags, ld32(link + kLinkGroundFlags) & ~0xE0u);
    stf32(link + kLinkSpeedY, kLiftSpeed);
    st32(link + kLinkFallHeight, ld32(link + kLinkPosY));  // no fall damage from the top
    ++g_lift.steps;
}

}  // namespace mod_stages

using namespace mod_stages;

// JAIZelBasic::spotNameToId(self, name) -> the stage's spot id (1..120), 0 for an unknown stage. A mod stage
// answers as its music_like stage, unless that stage's scene reads a dungeon's save bits.
extern "C" void hook_020277AC(Cpu* c) {
    const uint32_t name_ea = c->r[4];
    f_020277AC_orig(c);
    if (g_stages.empty() || c->r[3] != 0) return;
    const std::string name = guest_text(name_ea, 8);
    for (auto& b : g_stages) {
        if (b.info.name != name) continue;
        if (b.music_id < 0) {
            b.music_id = 0;
            for (uint32_t i = 0; i < kSpots && !b.music_like.empty(); i++)
                if (guest_text(ld32(kSpotNames + i * 4), 16) == b.music_like) b.music_id = int(i + 1);
            if (std::find(std::begin(kStoryScenes), std::end(kStoryScenes), uint32_t(b.music_id)) != std::end(kStoryScenes)) {
                LOG("[mod-stages] %s: music_like %s follows a dungeon's state; the stage has no music", b.info.id.c_str(), b.music_like.c_str());
                b.music_id = 0;
            } else if (!b.music_like.empty() && !b.music_id) {
                LOG("[mod-stages] %s: music_like %s has no music of its own", b.info.id.c_str(), b.music_like.c_str());
            }
            LOG("[mod-stages] %s (%s): music and scene sounds of %s (spot %d)", b.info.id.c_str(), name.c_str(),
                b.music_like.empty() ? "nothing" : b.music_like.c_str(), b.music_id);
        }
        c->r[3] = uint32_t(b.music_id);
        return;
    }
}

// dComIfGs_setGameStartStage: where the save file continues. In a mod stage: the stage's entrance (a place of
// the game, so the file loads without the package), and the mod save area keeps where in the stage the save
// was made. Anywhere else: the game's own choice, and no record of a mod stage.
extern "C" void hook_025217F8(Cpu* c) {
    const std::string stage = current_stage();
    const Built* b = g_stages.empty() ? nullptr : by_name(stage);
    if (!b) {
        f_025217F8_orig(c);
        if (story_store::ready()) {
            auto book = story_store::book();
            clear_resume(book);
        }
        return;
    }
    const uint32_t save = ld32(kSavePtr);
    if (save) {
        char text[8] = {};
        memcpy(text, b->info.entrance.stage.data(), std::min<size_t>(b->info.entrance.stage.size(), 7));
        for (int i = 0; i < 8; i++) st8(save + kReturnPlace + i, uint8_t(text[i]));
        st8(save + kReturnPlace + 8, uint8_t(b->info.entrance.room));
        st8(save + kReturnPlace + 9, uint8_t(b->info.entrance.point));
    }
    // where Link came into the stage: a spawn of it, else the stage's first
    unsigned room = ld8(start_stage() + 10) & 0x3F, point = ld16(start_stage() + 8);
    const bool spawn = std::any_of(b->info.spawns.begin(), b->info.spawns.end(), [&](const Info::SpawnPoint& s) { return s.room == room && s.point == point; });
    if (!spawn) room = b->info.spawns.front().room, point = b->info.spawns.front().point;
    if (story_store::ready()) {
        auto book = story_store::book();
        clear_resume(book);
        const bool kept = book.set(story_mail::kRecordStageResume, b->key, uint8_t(room)) && book.set(story_mail::kRecordStageResumePoint, b->key, uint8_t(point));
        // the game sets it again on many frames (a Game Over's menus): said when it changes
        static std::string said;
        const std::string line = stage + "/" + std::to_string(room) + "/" + std::to_string(point) + (kept ? "" : "/full");
        if (said != line) {
            said = line;
            LOG("[mod-stages] return place in %s (%s): the save file continues at %s room %u point %u; the mod save area %s room %u point %u", b->info.id.c_str(),
                stage.c_str(), b->info.entrance.stage.c_str(), b->info.entrance.room, b->info.entrance.point, kept ? "continues in the stage at" : "is full; not kept:", room,
                point);
        }
    } else {
        LOG("[mod-stages] return place in %s (%s): the save file continues at %s room %u point %u", b->info.id.c_str(), stage.c_str(), b->info.entrance.stage.c_str(),
            b->info.entrance.room, b->info.entrance.point);
    }
}

// dComIfGs_gameStart: a loaded Quest Log starts at the save's return place. When the mod save area says it was
// saved in a mod stage that is installed, it continues there.
extern "C" void hook_02522198(Cpu* c) {
    f_02522198_orig(c);
    if (!story_store::ready()) return;
    auto book = story_store::book();
    for (unsigned r = 0; r < story_block::kAreaRecords; ++r) {
        if (book.record_kind(r) != story_mail::kRecordStageResume) continue;
        const uint16_t key = book.record_key(r);
        const unsigned room = book.record_value(r), point = book.get(story_mail::kRecordStageResumePoint, key);
        for (const auto& b : g_stages) {
            if (b.key != key) continue;
            const bool spawn = std::any_of(b.info.spawns.begin(), b.info.spawns.end(), [&](const Info::SpawnPoint& s) { return s.room == room && s.point == point; });
            if (!spawn) break;
            write_next_stage(b.info.name, room, point);
            static std::string logged;  // the game asks again on following frames while the stage loads
            const std::string line = b.info.name + "/" + std::to_string(room) + "/" + std::to_string(point);
            if (logged == line) return;
            logged = line;
            // a loaded Quest Log saved there, or the game's own restart (Continue after a Game Over)
            LOG("[mod-stages] the return place is in %s/%s: Link continues there (%s room %u point %u)", b.info.mod.c_str(), b.info.id.c_str(), b.info.name.c_str(),
                room, point);
            return;
        }
        LOG("[mod-stages] the Quest Log was saved in a mod stage that is not installed: it continues at the stage's entrance");
        return;
    }
}

// JUTAssertion (file, line, message): logged with the stage while a mod stage is loaded
extern "C" void hook_0273AA24(Cpu* c) {
    if (!g_stages.empty()) {
        const std::string stage = current_stage();
        if (by_name(stage))
            LOG("[mod-stages] game assertion in %s: %s:%u %s", stage.c_str(), guest_text(c->r[3], 96).c_str(), c->r[4], guest_text(c->r[5], 96).c_str());
    }
    f_0273AA24_orig(c);
}

// ---- the stage record ---------------------------------------------------------------------------------------
// A stage's save record (dSv_memBit_c, 0x24 bytes: chests, switches, items, visited rooms, keys, dungeon items)
// lives in dSv_info_c's working copy (+0x778) while the stage is loaded; the game copies it from and back to the
// save's table of 16 (getSave, putSave). A mod stage's is its save group's own, kept in the mod save area
// (story_store; without one, for this session only), and the save file's slot 15 is never written.
namespace {
constexpr uint32_t kWorkingRecord = 0x778, kRecordBytes = 0x24;
std::map<uint16_t, std::array<uint8_t, kRecordBytes>> g_session_records;  // without a mod save area
const Built* g_loaded_record = nullptr;  // the mod stage whose record is in the working copy

// the stage being set up: the start stage once the game has taken the next stage over, else the next stage
const Built* loading_stage() {
    if (const Built* b = by_name(current_stage())) return b;
    return by_name(guest_text(next_stage(), 8));
}
unsigned group_slot(story_mail::Book& book, uint16_t group, bool make) {
    if (const uint8_t slot = book.get(story_mail::kRecordStageGroup, group)) return slot;
    if (!make) return 0;
    std::set<uint8_t> used;
    for (unsigned r = 0; r < story_block::kAreaRecords; ++r)
        if (book.record_kind(r) == story_mail::kRecordStageGroup) used.insert(book.record_value(r));
    for (unsigned slot = 1; slot <= 250; ++slot)
        if (!used.count(uint8_t(slot))) return book.set(story_mail::kRecordStageGroup, group, uint8_t(slot)) ? slot : 0;
    return 0;
}
std::array<uint8_t, kRecordBytes> read_record(const Built& b) {
    std::array<uint8_t, kRecordBytes> out{};
    if (!story_store::ready()) {
        auto it = g_session_records.find(b.group_key);
        if (it != g_session_records.end()) out = it->second;
        return out;
    }
    auto book = story_store::book();
    const unsigned slot = group_slot(book, b.group_key, false);
    if (slot)
        for (unsigned i = 0; i < kRecordBytes; ++i) out[i] = book.get(story_mail::kRecordStageMemory, uint16_t(slot << 8 | i));
    return out;
}
bool write_record(const Built& b, const std::array<uint8_t, kRecordBytes>& bytes) {
    if (!story_store::ready()) {
        g_session_records[b.group_key] = bytes;
        return true;
    }
    auto book = story_store::book();
    const bool any = std::any_of(bytes.begin(), bytes.end(), [](uint8_t v) { return v != 0; });
    const unsigned slot = group_slot(book, b.group_key, any);
    if (!slot) return !any;
    bool ok = true;
    for (unsigned i = 0; i < kRecordBytes; ++i) {
        const uint16_t key = uint16_t(slot << 8 | i);
        if (bytes[i]) ok &= book.set(story_mail::kRecordStageMemory, key, bytes[i]);
        else book.erase(story_mail::kRecordStageMemory, key);
    }
    return ok;
}
}  // namespace

namespace mod_stages {
bool chest_open(unsigned flag) {
    const uint32_t save = ld32(kSavePtr);
    if (!save || flag > 31 || !by_name(current_stage())) return false;
    return (ld32(save + 0x20 + kWorkingRecord) >> flag) & 1u;  // dSv_memBit_c's chests (u32, bit = flag)
}
bool reset_save(const std::string& mod, const std::string& group, std::string& why) {
    const uint16_t key = save_key(mod, "group:" + group);
    g_session_records.erase(key);
    if (story_store::ready()) {
        auto book = story_store::book();
        if (const unsigned slot = group_slot(book, key, false)) {
            for (unsigned i = 0; i < kRecordBytes; ++i) book.erase(story_mail::kRecordStageMemory, uint16_t(slot << 8 | i));
            book.erase(story_mail::kRecordStageGroup, key);
        }
    }
    const Built* here = by_name(current_stage());
    const uint32_t save = ld32(kSavePtr);
    if (here && here->group_key == key && save)
        for (unsigned i = 0; i < kRecordBytes; ++i) st8(save + 0x20 + kWorkingRecord + i, 0);
    LOG("[mod-stages] %s: the save group %s's record is reset", mod.c_str(), group.c_str());
    (void)why;
    return true;
}
}  // namespace mod_stages

// dSv_info_c::getSave(info, stage number): the stage's record into the working copy
extern "C" void hook_025B9C9C(Cpu* c) {
    const uint32_t info = c->r[3], number = c->r[4];
    f_025B9C9C_orig(c);
    if (g_stages.empty() || number != 15) return;
    const Built* b = loading_stage();
    g_loaded_record = b;
    if (!b) return;
    const auto bytes = read_record(*b);
    for (unsigned i = 0; i < kRecordBytes; ++i) st8(info + kWorkingRecord + i, bytes[i]);
    LOG("[mod-stages] %s: record of save group %s loaded (chests %02X%02X%02X%02X, keys %u, dungeon items %02X)%s", b->info.id.c_str(), b->stage.save_group.c_str(),
        bytes[0], bytes[1], bytes[2], bytes[3], bytes[0x20], bytes[0x21], story_store::ready() ? "" : " (no mod save area: kept for this session only)");
}

// dSv_info_c::putSave(info, stage number): the working copy back into the stage's record
extern "C" void hook_025B9D24(Cpu* c) {
    const uint32_t info = c->r[3], number = c->r[4];
    const Built* b = g_stages.empty() || number != 15 ? nullptr : by_name(current_stage());
    if (!b || b != g_loaded_record) {
        if (b && g_loaded_record && b->group_key == g_loaded_record->group_key) b = g_loaded_record;
        else if (b) {
            // the working copy isn't this stage's (a save state loaded elsewhere, say): nothing to keep, and the save
            // file's slot 15 stays as it is
            LOG("[mod-stages] %s: the working record is not the stage's; not kept", b->info.id.c_str());
            return;
        } else {
            f_025B9D24_orig(c);
            return;
        }
    }
    std::array<uint8_t, kRecordBytes> bytes{};
    for (unsigned i = 0; i < kRecordBytes; ++i) bytes[i] = ld8(info + kWorkingRecord + i);
    if (!write_record(*b, bytes)) LOG("[mod-stages] %s: the mod save area is full; the stage's record is not kept", b->info.id.c_str());
    else
        LOG("[mod-stages] %s: record of save group %s kept (chests %02X%02X%02X%02X, keys %u, dungeon items %02X)", b->info.id.c_str(), b->stage.save_group.c_str(),
            bytes[0], bytes[1], bytes[2], bytes[3], bytes[0x20], bytes[0x21]);
}

// dSv_danBit_c::init(dan, stage number): the dungeon switches (0x80..0xBF) start fresh when the stage number
// changes; each save group of mod stages counts as a stage number of its own (0x20 and up)
extern "C" void hook_025B9174(Cpu* c) {
    if (!g_stages.empty() && c->r[4] == 15)
        if (const Built* b = loading_stage()) c->r[4] = 0x20 + b->group_index;
    f_025B9174_orig(c);
}

// ---- the dungeon map ------------------------------------------------------------------------------------------
// In a stage with a map (STAG map mode 1) the game's map screen shows its dungeon view: a title banner, five
// floor buttons and an empty parchment (the game's maps are its own pictures per dungeon). The port draws the
// stage's map over it, on the screen the menus are on (the GamePad, or the TV with the Pro Controller): the
// rooms Link has been in (and, with the dungeon map, the others, lighter), the room he is in, his place and
// heading, and with the compass the chests (closed, open) and the boss door; the title; each floor's name on
// its button, the floor Link is on marked. Places in the GamePad's 854 x 480 units (the TV's are 1.5 times).
namespace mod_stages_map {
extern uint32_t g_coordinator;
}
namespace mod_stages {
namespace {
constexpr char kMapOwner[] = "mod-stages map";
constexpr float kAreaX = 398, kAreaY = 88, kAreaW = 360, kAreaH = 346;  // the parchment
constexpr float kButtonX = 155, kButtonH = 32;
constexpr float kButtonY[5] = {329, 283, 237, 191, 144};  // the floor buttons' tops, from the bottom
std::string floor_name(int f) { return f > 0 ? std::to_string(f) + "F" : "B" + std::to_string(-f); }
const Built* g_shown = nullptr;
uint32_t g_shown_screen = 2;
void release_images() {
    for (auto& b : g_stages)
        for (auto& r : b.map_rooms)
            if (r.image) {
                guestmods::hud::store().release(kMapOwner, r.image);
                r.image = 0;
            }
}
void hide_map() {
    if (!g_shown) return;
    guestmods::hud::store().drop(kMapOwner);
    release_images();
    g_shown = nullptr;
}
}  // namespace

void hide_map_for_rebuild() { hide_map(); }

void draw_map() {
    namespace hud = guestmods::hud;
    const uint32_t coord = mod_stages_map::g_coordinator;
    const Built* b = by_name(current_stage());
    const bool shown = b && b->stage.map && !b->map_rooms.empty() && coord && ld32(coord + 0x60) == 1 && ld8(coord + 0x69);
    if (!shown) return hide_map();
    const uint32_t screen = input::pro_controller() ? 0 : 1;
    if (b != g_shown || screen != g_shown_screen) {
        hide_map();
        g_shown = b, g_shown_screen = screen;
    }
    Built& stage = const_cast<Built&>(*b);
    const auto p = ss::link_place();
    const uint32_t save = ld32(kSavePtr);
    if (!save || !p.valid) return;
    const uint32_t rec = save + 0x20 + 0x778;  // the working stage record
    auto visited = [&](unsigned room) { return room < 64 && ((ld32(rec + 0x18 + 4 * (room >> 5)) >> (room & 31)) & 1); };
    const uint8_t items = ld8(rec + 0x21);
    const bool have_map = items & 1, have_compass = items & 2;
    const uint32_t chests = ld32(rec);
    int floor = 1;  // the floor Link is on: his room's
    for (const auto& r : stage.map_rooms)
        if (int(r.room) == p.room) floor = r.floor;
    std::vector<int> floors;
    for (const auto& r : stage.map_rooms)
        if ((have_map || visited(r.room) || int(r.room) == p.room) && std::find(floors.begin(), floors.end(), r.floor) == floors.end()) floors.push_back(r.floor);
    std::sort(floors.begin(), floors.end());
    if (floors.size() > 5) floors.erase(floors.begin(), floors.end() - 5);
    double x0 = 1e30, z0 = 1e30, x1 = -1e30, z1 = -1e30;  // every room of the floor: the map doesn't move as rooms are found
    for (const auto& r : stage.map_rooms)
        if (r.floor == floor) {
            x0 = std::min(x0, r.x), z0 = std::min(z0, r.z);
            x1 = std::max(x1, r.x + r.width * r.units), z1 = std::max(z1, r.z + r.height * r.units);
        }
    if (x1 <= x0 || z1 <= z0) return;
    const float k = screen == 0 ? 1.5f : 1.0f;
    const float margin = 12;
    const float scale = float(std::min((kAreaW - 2 * margin) / (x1 - x0), (kAreaH - 2 * margin) / (z1 - z0)));
    const float ox = kAreaX + (kAreaW - float(x1 - x0) * scale) / 2, oz = kAreaY + (kAreaH - float(z1 - z0) * scale) / 2;
    auto mx = [&](double x) { return (ox + float(x - x0) * scale) * k; };
    auto mz = [&](double z) { return (oz + float(z - z0) * scale) * k; };
    hud::Store& store = hud::store();
    const hud::Handle list = store.begin(kMapOwner, screen);
    if (!list) return;
    auto add = [&](hud::Command c) {
        c.anchor = hud::Anchor::TopLeft;
        store.append(kMapOwner, list, std::move(c));
    };
    hud::Command cover;
    cover.kind = hud::Command::Rect, cover.x = kAreaX * k, cover.y = kAreaY * k, cover.w = kAreaW * k, cover.h = kAreaH * k, cover.rgba = 0xF4E09AFF;
    add(cover);
    hud::Command face = cover;  // the game's Link face at the lowest button: the port marks Link's floor instead
    face.x = 94 * k, face.y = 326 * k, face.w = 50 * k, face.h = 46 * k, face.rgba = 0xF5E7BFFF;
    add(face);
    for (auto& r : stage.map_rooms) {
        if (r.floor != floor) continue;
        const bool here = int(r.room) == p.room, seen = visited(r.room);
        if (!here && !seen && !have_map) continue;
        if (!r.image) r.image = store.create_image(kMapOwner, r.width, r.height, r.width * 4, r.rgba->data(), r.rgba->size());
        if (!r.image) continue;
        hud::Command pic;
        pic.x = mx(r.x), pic.y = mz(r.z), pic.w = float(r.width * r.units) * scale * k, pic.h = float(r.height * r.units) * scale * k;
        pic.rgba = here ? 0xD9773CFF : seen ? 0xB98A4EFF : 0xD8C49A90;  // the room Link is in, rooms he has seen, the rest
        pic.anchor = hud::Anchor::TopLeft;
        store.picture(kMapOwner, list, r.image, pic);
    }
    if (have_compass) {
        for (const auto& room : stage.stage.rooms) {
            bool on_floor = false;
            for (const auto& r : stage.map_rooms)
                if (r.room == room.room && r.floor == floor) on_floor = true;
            if (!on_floor) continue;
            for (const auto& a : room.actors) {
                if (a.actor != "chest") continue;
                const bool open = (chests >> a.flag) & 1;
                hud::Command c;
                c.kind = hud::Command::Rect, c.x = mx(a.position.x) - 4 * k, c.y = mz(a.position.z) - 3 * k, c.w = 8 * k, c.h = 6 * k;
                c.rgba = open ? 0x8A7A64FF : 0xE0A82EFF;
                add(c);
                c.kind = hud::Command::RectOutline, c.rgba = 0x4A3010FF, c.thickness = 1;
                add(c);
            }
        }
        for (const auto& d : stage.stage.doors) {
            if (d.lock != "boss") continue;
            hud::Command c;
            c.kind = hud::Command::Circle, c.x = mx(d.position.x), c.y = mz(d.position.z), c.size = 5 * k, c.rgba = 0xC0302AFF;
            add(c);
        }
    }
    {  // Link: a red mark and his heading (angle 0 faces +z: down on the map)
        const float lx = mx(p.pos[0]), lz = mz(p.pos[2]);
        const double a = double(p.angle) * 3.14159265358979323846 / 32768.0;
        hud::Command line;
        line.kind = hud::Command::Line, line.x = lx, line.y = lz, line.w = float(std::sin(a) * 12 * k), line.h = float(std::cos(a) * 12 * k);
        line.thickness = 3, line.rgba = 0xC8281EFF;
        add(line);
        hud::Command dot;
        dot.kind = hud::Command::Circle, dot.x = lx, dot.y = lz, dot.size = 5 * k, dot.rgba = 0xC8281EFF;
        add(dot);
        dot.kind = hud::Command::CircleOutline, dot.rgba = 0xFFFFFFFF, dot.thickness = 1.5f;
        add(dot);
    }
    if (!stage.stage.map_title.empty()) {
        hud::Command t;
        t.kind = hud::Command::Text, t.x = 92 * k, t.y = 98 * k, t.size = 20 * k, t.rgba = 0x5A3A16FF, t.text = stage.stage.map_title;
        add(t);
    }
    for (size_t i = 0; i < floors.size(); ++i) {
        hud::Command t;
        t.kind = hud::Command::Text, t.x = (kButtonX + 32) * k, t.y = (kButtonY[i] + 6) * k, t.size = 20 * k;
        t.rgba = floors[i] == floor ? 0xB0301CFF : 0x6A4A20FF, t.text = floor_name(floors[i]);
        add(t);
        if (floors[i] == floor) {
            hud::Command m;
            m.kind = hud::Command::Circle, m.x = (kButtonX - 22) * k, m.y = (kButtonY[i] + kButtonH / 2) * k, m.size = 7 * k, m.rgba = 0xC8281EFF;
            add(m);
        }
    }
    store.commit(kMapOwner, list);
}

}  // namespace mod_stages
