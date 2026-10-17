// World editor: the game's side (editor_game.h).
#include "editor_game.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

#include "../gfx/renderer.h"
#include "../hle/save_root.h"
#include "../input.h"
#include "../mods/story_actors.h"
#include "../mods/story_mail_book.h"
#include "../mods/story_scenes.h"
#include "../mods/story_store.h"
#include "../overlay/hostui.h"
#include "../overlay/overlay.h"
#include "../runtime.h"
#include "../savestate.h"
#include "../true60.h"
#include "guest_addr.h"
#include "stage_files.h"

namespace editor {
namespace {
namespace fs = std::filesystem;
constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------- enabled
std::atomic<int> g_enabled{-1};  // -1: not read yet
bool env_on() {
    for (const char* name : {"WWHD_EDITOR_MODE", "WWHD_EDITOR"})
        if (const char* e = std::getenv(name); e && *e == '1') return true;
    return false;
}

// ---------------------------------------------------------------- status and game-thread jobs
std::mutex g_status_mu;
std::string g_status;
void set_status(const std::string& s) {
    LOG("[editor] %s", s.c_str());
    std::lock_guard<std::mutex> lk(g_status_mu);
    g_status = s;
}
std::mutex g_jobs_mu;
std::vector<std::function<void(Cpu*)>> g_jobs;
void post(std::function<void(Cpu*)> job) {
    std::lock_guard<std::mutex> lk(g_jobs_mu);
    g_jobs.push_back(std::move(job));
}

// ---------------------------------------------------------------- game addresses (as story_scenes.cpp uses them)
const uint32_t kPlayFn = GC(0x025200D4), kCameraFn = GC(0x024F8044), kStopCamera = GC(0x02514F2C), kStartCamera = GC(0x02514F38),
               kSetCamera = GC(0x02514F50), kResetCamera = GC(0x02515048), kOrder = GC(0x0253EC0C), kCheckStart = GC(0x0253FD1C),
               kEnd = GC(0x0253EFA8), kHideHud = GC(0x025916C4), kShowHud = GC(0x0259169C);
constexpr uint32_t kControl = 0x51D0, kMode = 0x5292, kOrderCount = 0x5290, kStage = 0x5134, kPlayer = 0x5B34, kHudShown = 0x5C22;
constexpr uint32_t kCenter = 0x10, kEye = 0x1C, kActive = 0x4;  // dCamera_c (d_camera.h)
constexpr uint32_t kPos = 0x314, kRoom = 0x326, kAngleY = 0x32A;  // fopAc_ac_c

uint32_t call(Cpu* c, uint32_t fn, std::initializer_list<uint32_t> args = {}) {
    Cpu k = *c;
    return guest_call(&k, fn, args);
}
std::string stage_of(uint32_t play) {
    char s[9] = {};
    std::memcpy(s, mem::ptr(play + kStage), 8);
    return s;
}

// ---------------------------------------------------------------- fly camera
struct FlyInput {
    float lx = 0, ly = 0, rx = 0, ry = 0;
    uint32_t buttons = 0;
    float mdx = 0, mdy = 0, wheel = 0;
};
std::mutex g_fly_mu;
FlyInput g_in;                 // under g_fly_mu
std::atomic<int> g_fly_want{0};  // 1 on, 2 off, 3 off with Link at the camera
std::atomic<bool> g_flying{false};
FlyView g_view;                // under g_fly_mu

struct Fly {
    bool active = false;
    uint32_t play = 0, camera = 0, scratch = 0;
    std::string stage;
    uint8_t saved[24] = {};
    bool camera_was_active = false, hud = false;
    double eye[3] = {}, yaw = 0, pitch = 0;  // yaw 0: +z (the game's angle 0), pitch up positive
    float speed = 1000;                     // units per second
    uint32_t held = ~0u;                    // controller buttons held at the last step
} g_fly;

void publish_view(Cpu* c) {
    FlyView v;
    v.on = g_fly.active;
    if (g_fly.active) {
        for (int i = 0; i < 3; i++) v.eye[i] = float(g_fly.eye[i]);
        const double cp = std::cos(g_fly.pitch);
        const double f[3] = {std::sin(g_fly.yaw) * cp, std::sin(g_fly.pitch), std::cos(g_fly.yaw) * cp};
        for (int i = 0; i < 3; i++) v.target[i] = float(g_fly.eye[i] + f[i] * 500);
        v.speed = g_fly.speed;
        v.stage = g_fly.stage;
        const uint32_t link = ld32(g_fly.play + kPlayer);
        v.room = link ? int8_t(ld8(link + kRoom)) : -1;
    }
    (void)c;
    std::lock_guard<std::mutex> lk(g_fly_mu);
    g_view = v;
}

void fly_submit(Cpu* c) {
    const double cp = std::cos(g_fly.pitch);
    const double f[3] = {std::sin(g_fly.yaw) * cp, std::sin(g_fly.pitch), std::cos(g_fly.yaw) * cp};
    for (int i = 0; i < 3; i++) {
        stf32(g_fly.scratch + 4 * i, g_fly.eye[i] + f[i] * 500);  // center
        stf32(g_fly.scratch + 12 + 4 * i, g_fly.eye[i]);         // eye
    }
    call(c, kSetCamera, {g_fly.camera, g_fly.scratch, g_fly.scratch + 12});
}

bool fly_start(Cpu* c, std::string& why) {
    if (!(why = ss::control_problem()).empty()) return false;
    if (story_scenes::active()) return why = "a mod scene is running", false;
    const uint32_t play = call(c, kPlayFn), camera = call(c, kCameraFn);
    if (!play || !camera || !ld32(play + kPlayer)) return why = "Link is not in the scene", false;
    if (ld8(play + kMode) || ld8(play + kOrderCount)) return why = "an event is running", false;
    // a compulsory event without an event script, as a mod scene starts it (story_scenes.cpp): Link
    // stands still, no stage or save flags are involved
    uint8_t previous[0xF4];
    std::memcpy(previous, mem::ptr(play + kControl), sizeof previous);
    if (!call(c, kOrder, {play + kControl, 3, 1, 0, 0, 0, 0, 0xFFFFFFFFu, 0xFF})) return why = "the game refused the event", false;
    if (!call(c, kCheckStart, {play + kControl}) || ld8(play + kMode) != 3) {
        std::memcpy(mem::ptr(play + kControl), previous, sizeof previous);
        return why = "the game refused the event", false;
    }
    if (!g_fly.scratch) g_fly.scratch = mem::host_alloc(32, 16);
    g_fly.active = true;
    g_fly.play = play;
    g_fly.camera = camera;
    g_fly.stage = stage_of(play);
    std::memcpy(g_fly.saved, mem::ptr(camera + kCenter), 24);
    g_fly.camera_was_active = ld8(camera + kActive) != 0;
    g_fly.hud = ld8(play + kHudShown) != 0;
    g_fly.held = ~0u;
    double center[3];
    for (int i = 0; i < 3; i++) {
        center[i] = ldf32(camera + kCenter + 4 * i);
        g_fly.eye[i] = ldf32(camera + kEye + 4 * i);
    }
    const double d[3] = {center[0] - g_fly.eye[0], center[1] - g_fly.eye[1], center[2] - g_fly.eye[2]};
    g_fly.yaw = std::atan2(d[0], d[2]);
    g_fly.pitch = std::atan2(d[1], std::hypot(d[0], d[2]));
    call(c, kStopCamera, {camera});
    call(c, kHideHud);
    {
        std::lock_guard<std::mutex> lk(g_fly_mu);
        g_in = FlyInput{};
    }
    g_flying = true;
    fly_submit(c);
    LOG("[editor] fly camera on at %.1f %.1f %.1f (yaw %.1f, pitch %.1f degrees)", g_fly.eye[0], g_fly.eye[1], g_fly.eye[2],
        g_fly.yaw * 180 / kPi, g_fly.pitch * 180 / kPi);
    return true;
}

// `still_ours`: the event and camera are the ones the fly camera took (not after a stage change or a load)
void fly_stop(Cpu* c, bool place_link, bool still_ours) {
    if (!g_fly.active) return;
    if (still_ours) {
        const uint32_t link = ld32(g_fly.play + kPlayer);
        if (place_link && link) {
            // Link to the camera's place, facing its way (as WWHD_LINK_WARP moves him: the HD retained
            // copies of his position and heading, 0240D130, are moved too); he falls to the ground
            const float x = float(g_fly.eye[0]), y = float(g_fly.eye[1]), z = float(g_fly.eye[2]);
            const uint16_t angle = uint16_t(int32_t(std::lround(g_fly.yaw * 32768 / kPi)));
            for (uint32_t off : {0x2ECu, 0x300u, 0x314u}) {
                stf32(link + off, x);
                stf32(link + off + 4, y);
                stf32(link + off + 8, z);
            }
            st16(link + 0x322, angle);
            st16(link + kAngleY, angle);
            stf32(GD(0x1046CD48), x);
            stf32(GD(0x1046CD4C), y);
            stf32(GD(0x1046CD50), z);
            st16(GD(0x1046CD12), angle);
            st16(GD(0x1046CD0A), angle);
            LOG("[editor] fly camera: Link placed at %.1f %.1f %.1f", x, y, z);
        } else
            std::memcpy(mem::ptr(g_fly.camera + kCenter), g_fly.saved, 24);
        call(c, kResetCamera, {g_fly.camera});
        if (g_fly.camera_was_active) call(c, kStartCamera, {g_fly.camera});
        call(c, g_fly.hud ? kShowHud : kHideHud);
        call(c, kEnd, {g_fly.play + kControl});
    }
    g_fly.active = false;
    g_flying = false;
    LOG("[editor] fly camera off%s", still_ours ? "" : " (the stage changed or a state was loaded)");
}

void fly_tick(Cpu* c, double dt) {
    const int want = g_fly_want.exchange(0);
    std::string why;
    if (want == 1 && !g_fly.active) {
        if (!fly_start(c, why)) set_status("Fly camera: " + why);
        else set_status("Fly camera on");
    }
    if (!g_fly.active) return publish_view(c);
    const uint32_t play = call(c, kPlayFn);
    const bool ours = play == g_fly.play && call(c, kCameraFn) == g_fly.camera && stage_of(play) == g_fly.stage && ld8(play + kMode) == 3;
    if (!ours || want >= 2) {
        fly_stop(c, want == 3, ours);
        set_status(want == 3 ? "Fly camera off: Link is at the camera's place" : "Fly camera off");
        return publish_view(c);
    }
    FlyInput in;
    {
        std::lock_guard<std::mutex> lk(g_fly_mu);
        in = g_in;
        g_in.mdx = g_in.mdy = g_in.wheel = 0;
    }
    // B (J): back to Link; A (K, Space): Link to the camera's place. On a press, not a held button.
    const uint32_t pressed = in.buttons & ~g_fly.held;  // buttons held when the camera started do nothing until released
    g_fly.held = in.buttons;
    if (pressed & (input::kA | input::kB)) {
        const bool place = (pressed & input::kA) != 0;
        fly_stop(c, place, true);
        set_status(place ? "Fly camera off: Link is at the camera's place" : "Fly camera off");
        return publish_view(c);
    }
    const double look = 2.4;  // radians per second at full stick
    g_fly.yaw -= (double(in.rx) * look * dt + double(in.mdx) * 0.0035);
    g_fly.pitch += (double(in.ry) * look * dt - double(in.mdy) * 0.0035);
    g_fly.pitch = std::clamp(g_fly.pitch, -1.5, 1.5);
    if (in.wheel != 0) g_fly.speed = std::clamp(g_fly.speed * std::pow(1.25f, in.wheel), 50.0f, 40000.0f);
    double speed = g_fly.speed;
    if (in.buttons & input::kZL) speed *= 4;   // Shift / left trigger: faster
    if (in.buttons & input::kZR) speed *= 0.25;  // C / right trigger: slower
    const double cp = std::cos(g_fly.pitch);
    const double fwd[3] = {std::sin(g_fly.yaw) * cp, std::sin(g_fly.pitch), std::cos(g_fly.yaw) * cp};
    const double right[3] = {-std::cos(g_fly.yaw), 0, std::sin(g_fly.yaw)};
    const double up = ((in.buttons & input::kR) ? 1.0 : 0.0) - ((in.buttons & input::kL) ? 1.0 : 0.0);  // E / Q
    for (int i = 0; i < 3; i++) {
        const double v = fwd[i] * in.ly + right[i] * in.lx + (i == 1 ? up : 0);
        g_fly.eye[i] = std::clamp(g_fly.eye[i] + v * speed * dt, -400000.0, 400000.0);
    }
    fly_submit(c);
    publish_view(c);
    // test aid: WWHD_EDITOR_TRACE=1 logs the camera every logic step (frame-mode checks)
    static const bool trace = [] { const char* e = std::getenv("WWHD_EDITOR_TRACE"); return e && *e == '1'; }();
    if (trace)
        LOG("[editor] fly step dt %.4f frame %llu eye %.2f %.2f %.2f yaw %.4f pitch %.4f", dt, (unsigned long long)render::frame_count(),
            g_fly.eye[0], g_fly.eye[1], g_fly.eye[2], g_fly.yaw, g_fly.pitch);
}

// ---------------------------------------------------------------- test mode
std::atomic<bool> g_test{false};
std::vector<uint8_t> g_story_before;  // game thread
fs::path editor_dir() { return fs::path(ss::states_dir()) / "editor-test"; }
std::string safe(const std::string& id) {
    std::string s;
    for (char ch : id) s += (std::isalnum((unsigned char)ch) || ch == '-' || ch == '_') ? ch : '_';
    return s;
}

// ---------------------------------------------------------------- the host
class GameHost final : public Host {
public:
    struct Startup {
        std::set<std::string> ids, mod_ids, prepared;  // actors, mod-model actors, prepared models
        std::set<std::string> restart_only;               // actors whose changes need a restart (story_actors::editor_restart_only)
    };
    std::map<std::string, Startup> startup;

    bool apply_actors(const std::string& mod, const std::vector<story_notebook::ManifestActor>& actors, const std::vector<std::string>& changed,
                      std::string& note, std::string& why) override {
        // what can change live: the actors declared at start-up, and only prepared models
        const auto& s = startup[mod];
        std::set<std::string> ids;
        for (const auto& a : actors) {
            ids.insert(a.id);
            if (!s.ids.contains(a.id)) return why = "actor " + a.id + " is new: it appears when the game starts again (save the package, then restart)", false;
            if ((a.kind == "mod") != s.mod_ids.contains(a.id)) return why = "actor " + a.id + ": switching between a game actor and a mod model takes effect when the game starts again", false;
            if (a.kind == "mod" && !s.prepared.contains(a.model))
                return why = "model " + a.model + " is prepared when the game starts: save the package and restart to see it", false;
        }
        if (ids.size() != s.ids.size()) return why = "removing actors takes effect when the game starts again", false;
        for (const auto& a : actors)
            if (std::find(changed.begin(), changed.end(), a.id) != changed.end() && (s.restart_only.contains(a.id) || story_actors::editor_restart_only(a)))
                return why = "actor " + a.id + ": enemies, pots, barrels, crates, switches, doors and carried props take their changes after a restart (save the package, then restart)", false;
        post([mod, actors, changed](Cpu*) {
            std::string w;
            if (!story_actors::editor_update(mod, actors, changed, w)) set_status("Live update refused: " + w);
            else if (!changed.empty()) set_status("Updated " + std::to_string(changed.size()) + " actor(s) in the game");
        });
        note = changed.empty() ? "" : "Changed actors are created again in their room.";
        return true;
    }
    bool reload_room(std::string& why) override {
        const auto p = ss::link_place();
        if (!p.valid) return why = "Link is not in the scene", false;
        ss::Warp w;
        w.stage = p.stage;
        w.room = p.room;
        w.at_position = true;
        w.angle = p.angle;
        std::memcpy(w.pos, p.pos, sizeof w.pos);
        ss::request_warp(w, [](bool ok, const std::string& why) { set_status(ok ? "Reloading the room" : "Reload room: " + why); });
        return true;
    }
    std::vector<std::pair<std::string, std::vector<int>>> stage_rooms() override { return game_stage_rooms(config::game_dir); }
    bool warp(const Location& to, std::string& why) override {
        ss::Warp w;
        w.stage = to.stage;
        w.room = to.room;
        if (to.point == kAtPosition) {
            w.at_position = true;
            w.pos[0] = to.position.x, w.pos[1] = to.position.y, w.pos[2] = to.position.z;
            w.angle = to.angle;
        } else if (to.point >= 0)
            w.point = to.point;
        else {
            // the room's own spawn points (its PLYR list): point 0 when it has one, else the lowest one on
            // foot (100 and above are the boat's), else Link stands at the location's position
            const auto spawns = room_spawns(config::game_dir, to.stage, to.room);
            const Spawn* pick = nullptr;
            for (const auto& s : spawns)
                if (s.room == to.room && (!pick || (s.point < 100 && (pick->point >= 100 || s.point < pick->point)))) pick = &s;
            if (pick) w.point = pick->point;
            else if (to.stage == "sea") {
                w.at_position = true;
                w.pos[0] = to.position.x, w.pos[1] = to.position.y, w.pos[2] = to.position.z;
            } else
                return why = "no spawn point found for " + to.stage + " room " + std::to_string(to.room) + " in the game's files", false;
        }
        ss::request_warp(w, [label = to.label.empty() ? to.stage : to.label](bool ok, const std::string& why) {
            set_status(ok ? "Warping to " + label : "Warp: " + why);
        });
        return true;
    }
    Place link() override {
        const auto p = ss::link_place();
        Place out;
        out.valid = p.valid;
        out.stage = p.stage;
        out.room = p.room;
        out.position = {p.pos[0], p.pos[1], p.pos[2]};
        out.angle = p.angle;
        return out;
    }
    bool busy(std::string& why) override {
        why = g_flying ? "the fly camera is on" : story_scenes::active() ? "a mod scene is running" : ss::control_problem();
        return !why.empty();
    }
    bool fly(bool on, bool place_link, std::string& why) override {
        if (on == g_flying.load()) return why = on ? "the fly camera is already on" : "the fly camera is off", false;
        g_fly_want = on ? 1 : place_link ? 3 : 2;
        if (on) overlay::set_open(false);  // the picture is the camera's
        return true;
    }
    Value fly_state() override {
        const FlyView v = fly_view();
        Value r;
        r["on"] = v.on;
        if (v.on) {
            r["position"] = point_value({v.eye[0], v.eye[1], v.eye[2]});
            r["target"] = point_value({v.target[0], v.target[1], v.target[2]});
            r["speed"] = double(v.speed);
            r["stage"] = v.stage;
            r["room"] = v.room;
        }
        return r;
    }
    bool set_test_mode(bool on, std::string& why) override {
        if (on == g_test.load()) return why = on ? "test mode is already on" : "test mode is off", false;
        if (on) {
            if (!(why = ss::control_problem()).empty()) return false;
            // the test save: a copy of the save folder as it is now; in-game saves write there
            std::error_code ec;
            const fs::path save = editor_dir() / "save";
            fs::remove_all(save, ec);
            fs::create_directories(save, ec);
            fs::copy(config::save_dir, save, fs::copy_options::recursive, ec);
            if (ec) return why = "cannot copy the save folder for the test save: " + ec.message(), false;
            const std::string start = (editor_dir() / "start.wwstate").string();
            post([save, start](Cpu*) {
                if (uint8_t* block = story_store::memory_block()) g_story_before.assign(block, block + story_store::kMemoryBytes);
                else g_story_before.clear();
                hle_fs::set_save_root(save.string());
                g_test = true;
                ss::request_save_portable_file(start, [](bool ok, const std::string& why) {
                    set_status(ok ? "Test mode on: in-game saves go to the test save" : "Test mode on (no return state: " + why + ")");
                });
            });
            return true;
        }
        const std::string start = (editor_dir() / "start.wwstate").string();
        post([start](Cpu*) {
            hle_fs::set_save_root("");
            if (uint8_t* block = story_store::memory_block(); block && g_story_before.size() == story_store::kMemoryBytes)
                std::memcpy(block, g_story_before.data(), g_story_before.size());
            g_test = false;
            std::error_code ec;
            if (fs::exists(start, ec)) {
                ss::request_load_portable_file(start);
                set_status("Test mode off: back to the progress and place from before the test");
            } else
                set_status("Test mode off: story progress restored (no return state)");
        });
        return true;
    }
    bool test_mode() override { return g_test; }
    bool reset_records(const std::string& mod, const std::vector<Record>& own, const std::vector<Record>& shared, unsigned& erased,
                       std::string& why) override {
        if (!story_store::ready()) return why = "no story progress in this game (no story mod is loaded)", false;
        erased = 0;
        post([mod, own, shared](Cpu*) {
            story_mail::Book book = story_store::book();
            unsigned skipped = 0;
            const unsigned n = editor::reset_records(book, own, shared, skipped);
            set_status("Reset " + mod + ": " + std::to_string(n) + " record(s) erased" +
                       (skipped ? ", " + std::to_string(skipped) + " shared with another mod kept" : ""));
        });
        return true;
    }
    bool test_state(bool save, int slot, const std::string& mod, const std::vector<Record>& own, std::string& why) override {
        std::error_code ec;
        const fs::path dir = editor_dir() / safe(mod);
        fs::create_directories(dir, ec);
        const std::string state = (dir / ("test" + std::to_string(slot) + ".wwstate")).string();
        const std::string records = (dir / ("test" + std::to_string(slot) + ".records")).string();
        if (save) {
            if (!(why = ss::control_problem()).empty()) return false;
            post([state, records, own, slot](Cpu*) {
                // the mod's own records with the state: a portable state holds no story progress
                story_mail::Book book = story_store::book();
                Value list;
                list.type = Value::Array;
                for (const auto& r : own)
                    if (book.has(r.kind, r.key)) {
                        Value v;
                        v["kind"] = int(r.kind);
                        v["key"] = int(r.key);
                        v["value"] = int(book.get(r.kind, r.key));
                        list.array.push_back(v);
                    }
                std::ofstream(records, std::ios::binary | std::ios::trunc) << mods::json::dump(list);
                ss::request_save_portable_file(state, [slot](bool ok, const std::string& why) {
                    set_status(ok ? "Test state " + std::to_string(slot) + " saved" : "Test state not saved: " + why);
                });
            });
            return true;
        }
        if (!fs::exists(state, ec)) return why = "test slot " + std::to_string(slot) + " is empty", false;
        post([state, records, own, slot](Cpu*) {
            std::ifstream f(records, std::ios::binary);
            std::stringstream text;
            text << f.rdbuf();
            story_mail::Book book = story_store::book();
            for (const auto& r : own) book.erase(r.kind, r.key);
            try {
                for (const auto& v : mods::json::parse(text.str()).array) {
                    const Record r{uint8_t(v.get("kind").number), uint16_t(v.get("key").number)};
                    if (std::find(own.begin(), own.end(), r) != own.end()) book.set(r.kind, r.key, uint8_t(v.get("value").number));
                }
            } catch (const std::exception&) {
            }
            ss::request_load_portable_file(state);
            set_status("Loading test state " + std::to_string(slot));
        });
        return true;
    }
};

std::recursive_mutex g_mu;  // the session (recursive: a script runs on the game thread inside logic_service)
GameHost g_host;
Session g_session(&g_host);

// ---------------------------------------------------------------- test script (WWHD_EDITOR_SCRIPT)
// "frame:{json request};frame:{...}" or "@file" with one "frame {json}" per line: each request runs at
// that TV frame; the results go to the log ([editor] script ...).
struct Scripted {
    uint64_t frame;
    std::string request;
};
std::vector<Scripted> parse_script() {
    std::vector<Scripted> out;
    const char* e = std::getenv("WWHD_EDITOR_SCRIPT");
    if (!e || !*e) return out;
    std::string text = e;
    if (text[0] == '@') {
        std::ifstream f(text.substr(1));
        std::stringstream s;
        s << f.rdbuf();
        text = s.str();
        for (char& ch : text)
            if (ch == '\n') ch = ';';
    }
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find(';', at);
        if (end == std::string::npos) end = text.size();
        const std::string item = text.substr(at, end - at);
        at = end + 1;
        const size_t split = item.find_first_of(": ");
        if (split == std::string::npos) continue;
        out.push_back({std::strtoull(item.c_str(), nullptr, 10), item.substr(split + 1)});
    }
    return out;
}
void run_script() {
    static const std::vector<Scripted> script = parse_script();
    static size_t next = 0;
    if (next >= script.size()) return;
    const uint64_t frame = render::frame_count();
    while (next < script.size() && script[next].frame <= frame) {
        const std::string out = run_json(script[next].request);
        LOG("[editor] script %s -> %s", script[next].request.c_str(), out.substr(0, 2000).c_str());
        next++;
    }
}
}  // namespace

bool launch_mode() {
    int v = g_enabled.load();
    if (v < 0) {
        v = env_on() ? 1 : 0;
        g_enabled = v;
    }
    return v == 1;
}
void set_launch_mode(bool on) {
    if (on) g_enabled = 1;
}

void register_package(const std::string& mod, const std::string& name, const std::string& dir, const Value& manifest) {
    if (manifest.get("actors").type == Value::Null && manifest.get("models").type == Value::Null && manifest.get("scenes").type == Value::Null &&
        manifest.get("quests").type == Value::Null)
        return;
    std::lock_guard<std::recursive_mutex> lk(g_mu);
    auto& s = g_host.startup[mod];
    std::vector<story_notebook::ManifestActor> parsed;
    std::string why;
    if (story_notebook::parse_actors(manifest, parsed, why))
        for (const auto& a : parsed)
            if (story_actors::editor_restart_only(a)) s.restart_only.insert(a.id);
    for (const auto& a : manifest.get("actors").array) {
        s.ids.insert(a.get("id").string());
        if (a.get("kind").string() == "mod") {
            s.mod_ids.insert(a.get("id").string());
            s.prepared.insert(a.get("model").string());  // load_world prepares every model an actor uses
        }
    }
    g_session.add_package(mod, name, dir, manifest);
    LOG("[editor] package %s registered (%zu actors)", mod.c_str(), manifest.get("actors").array.size());
}

Value run(const std::string& command, const Value& params) {
    std::lock_guard<std::recursive_mutex> lk(g_mu);
    return g_session.run(command, params);
}
Value query(const std::string& command, const Value& params) {
    std::lock_guard<std::recursive_mutex> lk(g_mu);
    return g_session.run(command, params, false);
}
std::string run_json(const std::string& request) {
    std::lock_guard<std::recursive_mutex> lk(g_mu);
    return g_session.run_json(request);
}
void with_session(const std::function<void(Session&)>& f) {
    std::lock_guard<std::recursive_mutex> lk(g_mu);
    f(g_session);
}
std::string status() {
    std::lock_guard<std::mutex> lk(g_status_mu);
    return g_status;
}

void logic_service(Cpu* c) {
    if (!enabled()) return;
    run_script();
    std::vector<std::function<void(Cpu*)>> jobs;
    {
        std::lock_guard<std::mutex> lk(g_jobs_mu);
        jobs.swap(g_jobs);
    }
    for (auto& job : jobs) job(c);
}

void camera_service(Cpu* c, bool logic_pass) {
    if (!enabled()) return;
    const bool t60 = true60::enabled();
    if (!logic_pass && !t60) return;  // an interpolation-only pass draws between logic steps: no state change
    fly_tick(c, t60 ? 1.0 / 60 : 1.0 / 30);
}

void filter_pad(input::PadState& s) {
    if (!g_flying.load(std::memory_order_relaxed)) return;
    {
        std::lock_guard<std::mutex> lk(g_fly_mu);
        g_in.lx = s.lx, g_in.ly = s.ly, g_in.rx = s.rx, g_in.ry = s.ry;
        g_in.buttons = s.buttons;
    }
    s = input::PadState{};  // the game gets nothing while the camera flies
}
bool take_mouse(float dx, float dy) {
    if (!g_flying.load(std::memory_order_relaxed)) return false;
    std::lock_guard<std::mutex> lk(g_fly_mu);
    g_in.mdx += dx;
    g_in.mdy += dy;
    return true;
}
bool take_wheel(float dy) {
    if (!g_flying.load(std::memory_order_relaxed)) return false;
    std::lock_guard<std::mutex> lk(g_fly_mu);
    g_in.wheel += dy;
    return true;
}
FlyView fly_view() {
    std::lock_guard<std::mutex> lk(g_fly_mu);
    return g_view;
}

}  // namespace editor
