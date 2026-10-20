// World editor: the game's side (editor_game.h).
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>
#include "editor_game.h"
#include "ray_ground.h"


#include "../gfx/renderer.h"
#include "../hle/save_root.h"
#include "../input.h"
#include "../mods/guest_mods.h"
#include "../mods/mod_actor.h"
#include "../mods/actor_model_cache.h"
#include "../mods/packages.h"
#include "../mods/story_actors.h"
#include "../mods/story_game_pieces.h"
#include "../mods/story_npc_catalogue.h"
#include "../mods/story_demo.h"
#include "../mods/story_items.h"
#include "../mods/story_mail.h"
#include "../mods/story_mail_book.h"
#include "../mods/story_manifest.h"
#include "../mods/story_notebook.h"
#include "../mods/story_notebook_list.h"
#include "../mods/story_reload.h"
#include "../mods/story_room_paths.h"
#include "../mods/story_chart.h"
#include "../mods/story_treasure.h"
#include "../mods/story_scenes.h"
#include "../mods/story_store.h"
#include "../overlay/hostui.h"
#include "../overlay/overlay.h"
#include "../runtime.h"
#include "../savestate.h"
#include "../true60.h"
#include "guest_addr.h"
#include "../mods/mod_stages.h"
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

// ---------------------------------------------------------------- the story records, for any thread
// read_state, the quest graph, the status bar and the Flags panel read story records from any thread (the UI
// every frame): the game thread keeps a copy of the loaded Quest Log's story area, refreshed a few times a
// second and right after the editor writes records (records_tick).
std::mutex g_records_mu;
std::vector<uint8_t> g_records_copy;  // under g_records_mu; empty: no story progress seen
void records_tick(bool now = false);   // game thread

// ---------------------------------------------------------------- status and game-thread jobs
std::mutex g_status_mu;
std::string g_status;
// island buildings being prepared for a mod (prepare_piece): "preparing", "ready", "failed: ..."
std::mutex g_piece_mutex;
std::map<std::pair<std::string, std::string>, std::string> g_piece_states;
// "a villager of the island (Grandma)": the character's name when the NPC catalogue knows the code
void name_layout_conflicts(const story_notebook::ManifestActor& actor, Value& result) {
    for (auto& c : result["conflicts"].array) {
        const std::string id = c.get("id").string();
        const auto first = id.find(':'), second = id.find(':', first + 1);
        if (first == std::string::npos || second == std::string::npos) continue;
        const std::string code = id.substr(first + 1, second - first - 1);
        auto same = [](std::string a, std::string b) {
            for (char& ch : a) ch = char(std::tolower((unsigned char)ch));
            for (char& ch : b) ch = char(std::tolower((unsigned char)ch));
            return a == b;
        };
        std::string name;
        for (const auto& e : story_notebook::npc_catalogue().entries)
            for (const auto& a : e.actors)
                if (name.empty() && same(a.code, code) && a.stage == actor.stage && (a.room < 0 || a.room == int(actor.room))) name = e.display();
        if (name.empty())
            for (const auto& e : story_game_objects::catalogue().entries)
                if (name.empty() && same(e.native_name, code)) name = e.label;
        if (name.empty()) continue;
        c["message"] = c.get("message").string() + " (" + name + ")";
        c["name"] = name;
    }
}
uint64_t g_test_load_serial=0;
Value g_test_load_result; // protected by g_status_mu; read_state/MCP may poll it
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
// The game thread's Cpu while it runs the editor's services (a script's commands run there too).
thread_local Cpu* t_game = nullptr;
// Runs `f` on the game thread and waits for it (at most `ms`): directly when this is the game thread.
bool on_game(const std::function<void(Cpu*)>& f, std::string& why, int ms = 1500) {
    if (t_game) {
        f(t_game);
        return true;
    }
    struct Wait {
        std::mutex mu;
        std::condition_variable cv;
        bool done = false;
    };
    auto w = std::make_shared<Wait>();
    post([w, f](Cpu* c) {
        f(c);
        std::lock_guard<std::mutex> lk(w->mu);
        w->done = true;
        w->cv.notify_all();
    });
    std::unique_lock<std::mutex> lk(w->mu);
    if (!w->cv.wait_for(lk, std::chrono::milliseconds(ms), [&] { return w->done; })) return why = "the game did not answer (paused or loading)", false;
    return true;
}

// ---------------------------------------------------------------- game addresses (as story_scenes.cpp uses them)
const uint32_t kPlayFn = GC(0x025200D4), kCameraFn = GC(0x024F8044), kStopCamera = GC(0x02514F2C), kStartCamera = GC(0x02514F38),
               kSetCamera = GC(0x02514F50), kResetCamera = GC(0x02515048), kOrder = GC(0x0253EC0C), kCheckStart = GC(0x0253FD1C),
               kEnd = GC(0x0253EFA8), kHideHud = GC(0x025916C4), kShowHud = GC(0x0259169C);
const uint32_t kSearchName = GC(0x025C109C);      // dStage_searchName: the object name table entry of a stage actor name
const uint32_t kSeStartSystem = GC(0x025E1988);   // mDoAud_seStartSystem(id): a sound effect without a place
constexpr uint32_t kControl = 0x51D0, kMode = 0x5292, kOrderCount = 0x5290, kStage = 0x5134, kPlayer = 0x5B34, kHudShown = 0x5C22;
constexpr uint32_t kCenter = 0x10, kEye = 0x1C, kActive = 0x4;  // dCamera_c (d_camera.h)
constexpr uint32_t kPos = 0x314, kRoom = 0x326, kAngleY = 0x32A;  // fopAc_ac_c

uint32_t call(Cpu* c, uint32_t fn, std::initializer_list<uint32_t> args = {}) {
    Cpu k = *c;
    return guest_call(&k, fn, args);
}
double call_f1(Cpu* c, uint32_t fn, std::initializer_list<uint32_t> args) {
    Cpu k = *c;
    guest_call(&k, fn, args);
    return k.f[1].ps0;
}
std::string stage_of(uint32_t play) {
    char s[9] = {};
    std::memcpy(s, mem::ptr(play + kStage), 8);
    return s;
}

// ---------------------------------------------------------------- fly camera
bool request_play(bool from_camera, bool restore, std::string& why);  // Play (below)
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
// A view the fly camera takes when it starts next (Stop, a resumed editor view) and a point to look at.
struct ResumeView {
    bool set = false;
    double eye[3] = {}, yaw = 0, pitch = 0;
    float speed = 1000;
};
ResumeView g_resume;                       // game thread
std::mutex g_focus_mu;
bool g_focus_set = false;                  // under g_focus_mu
double g_focus[3] = {};
double g_focus_back = 700, g_focus_yaw = 0;  // how far back the camera goes; the direction it looks (radians)
bool g_focus_turn = false;                  // false: it keeps its own direction
int g_retry = 0;                           // logic steps a wanted fly start keeps trying (after a load)
ResumeView current_view() {
    ResumeView v;
    v.set = true;
    for (int i = 0; i < 3; i++) v.eye[i] = g_fly.eye[i];
    v.yaw = g_fly.yaw, v.pitch = g_fly.pitch, v.speed = g_fly.speed;
    return v;
}

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
    if (g_resume.set) {
        for (int i = 0; i < 3; i++) g_fly.eye[i] = g_resume.eye[i];
        g_fly.yaw = g_resume.yaw, g_fly.pitch = g_resume.pitch, g_fly.speed = g_resume.speed;
        g_resume.set = false;
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
        if (fly_start(c, why)) {
            g_retry = 0;
            set_status("Fly camera on");
        } else if (g_retry > 0) {
            --g_retry;  // after a state load or a stage change: Link is under control again soon
            g_fly_want = 1;
        } else
            set_status("Fly camera: " + why);
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
        if (launch_mode()) return publish_view(c);  // the editor's view stays: Play is Tab / the View button
        fly_stop(c, place, true);
        set_status(place ? "Fly camera off: Link is at the camera's place" : "Fly camera off");
        return publish_view(c);
    }
    {
        std::lock_guard<std::mutex> lk(g_focus_mu);
        if (g_focus_set) {  // look at the point from a short way back, slightly above
            g_focus_set = false;
            const double back = g_focus_back, up = g_focus_back / 2;
            if (g_focus_turn) g_fly.yaw = g_focus_yaw;
            g_fly.eye[0] = g_focus[0] - std::sin(g_fly.yaw) * back;
            g_fly.eye[1] = g_focus[1] + up;
            g_fly.eye[2] = g_focus[2] - std::cos(g_fly.yaw) * back;
            const double d[3] = {g_focus[0] - g_fly.eye[0], g_focus[1] + 60 - g_fly.eye[1], g_focus[2] - g_fly.eye[2]};
            g_fly.yaw = std::atan2(d[0], d[2]);
            g_fly.pitch = std::atan2(d[1], std::hypot(d[0], d[2]));
        }
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

// ---------------------------------------------------------------- jobs that need Link under control
// Warps, room reloads, test mode and test states need Link free: while the fly camera holds him, it
// stops first and comes back at the same view afterwards (the editor's view in the launch mode).
std::vector<std::function<void(Cpu*)>> g_free_jobs;  // game thread
std::atomic<bool> g_manual_off{false};                 // fly_camera off in the launch mode: no automatic view
void when_free(std::function<void(Cpu*)> job) {
    post([job = std::move(job)](Cpu*) mutable {
        g_free_jobs.push_back(std::move(job));
        if (g_fly.active) {
            g_resume = current_view();
            g_fly_want = 2;
        }
    });
}
void run_free_jobs(Cpu* c) {
    if (g_free_jobs.empty() || g_fly.active || !ss::control_problem().empty() || story_scenes::active()) return;
    auto jobs = std::move(g_free_jobs);
    g_free_jobs.clear();
    for (auto& job : jobs) job(c);
}

// ---------------------------------------------------------------- the game's collision
uint32_t g_gnd = 0;  // a dBgS_ObjGndChk in guest memory, for the editor's ground checks
const uint32_t kGndCtor = GC(0x0237AABC), kGroundCross = GC(0x02008974);
constexpr uint32_t kBgs = 0x12A0;  // dComIfG_Bgsp() in the play
// the ground height below `p` (the game's own check, as mod actors use it); false: none
bool game_ground_below(Cpu* c, double x, double y, double z, float& h) {
    const uint32_t play = call(c, kPlayFn);
    if (!play || !ld32(play + kPlayer)) return false;
    if (!g_gnd) {
        g_gnd = mem::host_alloc(0x80, 16);
        std::memset(mem::ptr(g_gnd), 0, 0x80);
        call(c, kGndCtor, {g_gnd});
    }
    stf32(g_gnd + 0x24, float(x));
    stf32(g_gnd + 0x28, float(y));
    stf32(g_gnd + 0x2C, float(z));
    st32(g_gnd + 8, 0xFFFFFFFFu);
    const double r = call_f1(c, kGroundCross, {play + kBgs, g_gnd});
    if (!std::isfinite(r) || r < -1e8) return false;
    h = float(r);
    return true;
}
// the ground below each of `above`: the game's own check, and the mod world objects' own collision (an island ground,
// a terrace, a pier: what the game finds once they stand in the loaded room, also before then or while it is being
// rebuilt); the higher of the two where both have one
void grounds_below(Cpu* c, const std::vector<WorldPoint>& above, std::vector<float>& heights, std::vector<bool>& found) {
    heights.assign(above.size(), 0);
    found.assign(above.size(), false);
    for (size_t i = 0; i < above.size(); i++) {
        float h = 0;
        if (game_ground_below(c, above[i].x, above[i].y, above[i].z, h)) heights[i] = h, found[i] = true;
    }
    const auto here = ss::link_place();
    if (!here.valid) return;
    std::vector<float> own;
    std::vector<bool> has;
    if (!mod_actor::object_grounds(here.stage, above, own, has)) return;
    for (size_t i = 0; i < above.size(); i++)
        if (has[i] && (!found[i] || own[i] > heights[i])) heights[i] = own[i], found[i] = true;
}
bool ground_below(Cpu* c, double x, double y, double z, float& h) {
    std::vector<float> heights;
    std::vector<bool> found;
    grounds_below(c, {WorldPoint{float(x), float(y), float(z)}}, heights, found);
    if (!found[0]) return false;
    h = heights[0];
    return true;
}
// the first ground a ray meets (ray_ground.h: steps along it, looks up the ground below each step's top)
bool ray_hit(Cpu* c, const WorldPoint& from, const WorldPoint& d, float range, WorldPoint& hit) {
    return ray_ground(from, d, range, [c](double x, double y, double z, float& h) { return ground_below(c, x, y, z, h); }, hit);
}

// ---------------------------------------------------------------- dialogue preview (the game's message box)
const uint32_t kMessageManager = GD(0x101F4B5C), kMessage = GC(0x025F7DB0), kMessageStatus = GC(0x025F795C),
               kSetMessageStatus = GC(0x025F74D0), kResetMessage = GC(0x025F78C4);
constexpr uint32_t kCarrier = 0x2CF3;
struct Preview {
    int phase = 0;  // 0 none, 1 opening, 2 shown, 3 closing
    std::u16string text;
    bool close = false;
    int frames = 0, closing = 0;
} g_preview;  // game thread
// ends the shown text: the way a mod scene closes its message (FINISH, then CLOSED), or, when that does
// not come (an aborted preview), the message core's own reset
void preview_end(Cpu* c, bool immediate) {
    const uint32_t manager = ld32(kMessageManager);
    if (manager && g_preview.phase >= 2) {
        const uint32_t status = call(c, kMessageStatus, {manager});
        if (status == 18) call(c, kSetMessageStatus, {manager, 19});
        else if (status != 0 && status != 19) {
            if (!immediate && g_preview.closing == 0) {
                call(c, kSetMessageStatus, {manager, 16});
                g_preview.closing = 1;
                g_preview.phase = 3;
                return;
            }
            if (!immediate && ++g_preview.closing < 60) return;
            call(c, kResetMessage, {manager});
        }
    }
    story_mail::unsay();
    g_preview = {};
}
void preview_tick(Cpu* c) {
    if (!g_preview.phase) return;
    const uint32_t manager = ld32(kMessageManager);
    if (!manager || !g_fly.active) return preview_end(c, true);  // the box belongs to the editor's view
    ++g_preview.frames;
    if (g_preview.phase == 1) {
        if (call(c, kMessage, {manager, kCarrier, 0}) != 0xFFFFFFFFu) g_preview.phase = 2;
        else if (g_preview.frames > 60) {
            story_mail::unsay();
            g_preview = {};
            set_status("Preview: the game did not open its message box");
        }
        return;
    }
    if (g_preview.close || g_preview.frames > 9000) preview_end(c, false);  // five minutes at most
}

// ---------------------------------------------------------------- Play
std::atomic<bool> g_playing{false};
std::atomic<int> g_play_after{0};  // > 0: start at a step asked for Play once the editor's view is back (logic steps counted)
struct PlayState {
    bool restore = true, turned_test = false, snapshot = false;
    ResumeView view;
    std::vector<uint8_t> story;  // the story area at Play
} g_play;                        // game thread
std::string play_state_file() { return (fs::path(ss::states_dir()) / "editor-test" / "play.wwstate").string(); }

// ---------------------------------------------------------------- scene preview
// preview_scene: the fly camera stops (Link free), test mode's start and the world as it is are kept (one
// state request at a time), the scene is requested, plays, and when it ends (or is skipped or stopped)
// the world goes back to how it was and the editor's view returns.
std::atomic<bool> g_previewing{false};
struct ScenePreview {
    int phase = 0;  // 1 getting ready, 2 requested, 3 playing, 4 done
    std::string mod;
    story_notebook::ManifestScene scene;
    bool declared = false, restore = true, snapshot = false, stop = false, skip = false, native = false;
    int frames = 0;
    ResumeView view;
    std::vector<uint8_t> story;
} g_scene;  // game thread
std::mutex g_scene_mu;
Value g_scene_state;  // under g_scene_mu: scene_state for any thread
std::string preview_file() { return (fs::path(ss::states_dir()) / "editor-test" / "preview.wwstate").string(); }
void publish_scene(const std::string& phase, size_t step) {
    Value v;
    v["playing"] = g_previewing.load();
    if (g_previewing) {
        v["phase"] = phase;
        v["scene"] = g_scene.scene.id == "editor-preview" ? std::string("editor-preview") : g_scene.scene.id;
        v["mod"] = g_scene.mod;
        if (step) v["step"] = int(step);
        v["native"] = g_scene.native;
    }
    std::lock_guard<std::mutex> lk(g_scene_mu);
    g_scene_state = v;
}
void scene_preview_end(const std::string& status);


// ---------------------------------------------------------------- test mode
std::atomic<bool> g_test{false};
std::vector<uint8_t> g_story_before;  // game thread
fs::path editor_dir() { return fs::path(ss::states_dir()) / "editor-test"; }
std::string safe(const std::string& id) {
    std::string s;
    for (char ch : id) s += (std::isalnum((unsigned char)ch) || ch == '-' || ch == '_') ? ch : '_';
    return s;
}

// ---------------------------------------------------------------- test mode on
// the test save: a copy of the save folder as it is now; in-game saves write there
bool test_save(std::string& why) {
    std::error_code ec;
    const fs::path save = editor_dir() / "save";
    fs::remove_all(save, ec);
    fs::create_directories(save, ec);
    fs::copy(config::save_dir, save, fs::copy_options::recursive, ec);
    if (ec) return why = "cannot copy the save folder for the test save: " + ec.message(), false;
    g_test = true;
    return true;
}
// game thread, Link free: the story area and the place to come back to (`then` when the state is written)
void test_start(std::function<void(bool ok)> then) {
    if (uint8_t* block = story_store::memory_block()) g_story_before.assign(block, block + story_store::kMemoryBytes);
    else g_story_before.clear();
    hle_fs::set_save_root((editor_dir() / "save").string());
    ss::request_save_portable_file((editor_dir() / "start.wwstate").string(), [then](bool ok, const std::string& why) {
        set_status(ok ? "Test mode on: in-game saves go to the test save" : "Test mode on (no return state: " + why + ")");
        if (then) then(ok);
    });
}

// ---------------------------------------------------------------- the story tables while the game runs
// apply_story (any thread) keeps the latest manifest of each mod whose quests, letters, charts or items
// changed; the game thread takes it at a calm moment (story_tick): the mod's tables again
// (story_notebook::redeclare), its items' texts (story_items::define), never a record of the save file.
struct StoryChange {
    std::string name, dir;
    Value started, manifest;
};
std::mutex g_story_mu;
std::map<std::string, StoryChange> g_story_pending;  // under g_story_mu
std::map<std::string, Value> g_story_applied;        // game thread: the manifest whose tables the game has now
std::string g_story_wait;                            // game thread: why the pending change waits (said once)

std::string joined(const std::vector<std::string>& list) {
    std::string s;
    for (const auto& x : list) s += (s.empty() ? "" : ", ") + x;
    return s;
}
// What of a story change waits for a restart (the note of apply_story)
std::vector<std::string> story_later(const Value& started, const Value& manifest) {
    std::vector<std::string> later;
    const bool items_changed = !(started.get("items") == manifest.get("items"));
    if (items_changed && !story_items::installed_hooks()) later.push_back("the items (the game had no mod items at start-up)");
    else
        for (const auto& i : story_reload::restart_items(started, manifest)) later.push_back("item " + i);
    for (const auto& id : story_reload::gone(
             [&] {
                 std::vector<std::string> ids;
                 for (const auto& c : started.get("charts").array) ids.push_back(c.get("id").string());
                 return ids;
             }(),
             [&] {
                 std::vector<std::string> ids;
                 for (const auto& c : manifest.get("charts").array) ids.push_back(c.get("id").string());
                 return ids;
             }()))
        later.push_back("chart " + id + " (removed)");
    if (!manifest.get("charts").array.empty() && !story_treasure::installed() && !(started.get("charts") == manifest.get("charts")))
        later.push_back("salvaging the treasure of a chart (the game had no chart at start-up)");
    bool sail = false;
    for (const auto& q : manifest.get("quests").array)
        for (const auto& st : q.get("steps").array) sail |= st.get("sail_line").type != Value::Null;
    if (sail && !story_notebook::sail_installed()) later.push_back("the King of Red Lions' sail lines (no quest had one at start-up)");
    return later;
}
// game thread: no story talk, letter, scene, menu or stage change running
bool story_calm(std::string& why) {
    if (!story_notebook::story_idle(why)) return false;
    if (story_scenes::active() || g_previewing) return why = "a scene is playing", false;
    if (g_flying) return true;  // the editor view: the fly camera holds Link, nothing else runs
    const std::string problem = ss::control_problem();
    if (problem.empty() || problem == "no game in progress") return true;
    return why = problem, false;
}
void story_tick() {
    {
        std::lock_guard<std::mutex> lk(g_story_mu);
        if (g_story_pending.empty()) return;
    }
    std::string why;
    if (!story_calm(why)) {
        if (why != g_story_wait) set_status("The story change waits for a calm moment (" + why + ")");
        g_story_wait = why;
        return;
    }
    g_story_wait.clear();
    std::map<std::string, StoryChange> pending;
    {
        std::lock_guard<std::mutex> lk(g_story_mu);
        pending.swap(g_story_pending);
    }
    for (auto& [mod, ch] : pending) {
        story_notebook::Redeclared r;
        if (!story_notebook::redeclare(mod, ch.name, ch.manifest, r, why)) {
            set_status("The story of " + mod + " is not changed in the game: " + why + " (it is in the package)");
            continue;
        }
        // the items that change at once: their name, description, stack and use
        unsigned items = 0;
        if (story_items::installed_hooks()) {
            std::vector<story_notebook::ManifestItem> list;
            std::string w;
            if (story_notebook::parse_items(ch.manifest, list, w)) {
                const auto live = story_reload::live_items(ch.started, ch.manifest);
                for (const auto& item : list)
                    if (std::find(live.begin(), live.end(), item.id) != live.end() && story_items::define(mod, ch.dir, item, w)) items++;
            }
        }
        // the loaded Quest Log keeps its step numbers: say where one now names another step
        const auto applied = g_story_applied.find(mod);
        const Value& before = applied != g_story_applied.end() ? applied->second : ch.started;
        std::vector<std::string> moved;
        if (story_store::ready()) {
            story_mail::Book b = story_store::book();
            for (const auto& m : story_reload::step_moves(before, ch.manifest))
                if (b.usable() && story_notebook::saved_step(b, story_notebook::quest_key(mod, m.quest)) == m.saved)
                    moved.push_back("quest " + m.quest + " is at step " + std::to_string(m.saved) +
                                    (m.now ? ", which is step " + std::to_string(m.now) + " now" : ", which was removed"));
        }
        g_story_applied[mod] = ch.manifest;
        std::string s = "Story of " + mod + " updated in the game: " + std::to_string(r.quests) + " quest(s), " + std::to_string(r.talks) +
                        " quest talk(s), " + std::to_string(r.letters) + " letter(s), " + std::to_string(r.charts) + " chart(s), " +
                        std::to_string(items) + " item(s)";
        if (!moved.empty()) s += ". The loaded Quest Log keeps its steps: " + joined(moved);
        if (!r.charts_kept.empty()) s += ". Until a restart the game keeps chart(s) " + joined(r.charts_kept);
        set_status(s);
    }
    records_tick(true);
}

// ---------------------------------------------------------------- the host
void reapply_mod(const std::string& mod);  // below: the live actors again (a model became ready)
std::string refused_pieces(const std::string& mod, const std::string& piece);  // below: placements of a building that can't stand there
// the game's villagers the NPC catalogue knows (list_villagers): read from the actor list on the game
// thread (villagers_tick), for the host on any thread
std::mutex g_villagers_mu;
std::vector<story_notebook::LiveVillager> g_villagers;
class GameHost final : public Host {
public:
    bool apply_actors(const std::string& mod, const std::vector<story_notebook::ManifestActor>& actors, const std::vector<std::string>& changed,
                      std::string& note, std::string& why) override {
        (void)why;
        // what waits for a restart: start-up-only kinds, models not prepared yet (said at once; the game's
        // update below says it again with the exact reason)
        std::vector<std::string> later;
        for (const auto& a : actors)
            if (std::find(changed.begin(), changed.end(), a.id) != changed.end() && story_actors::editor_restart_only(a)) later.push_back(a.id);
        post([mod, actors, changed](Cpu* c) {
            std::string w;
            std::vector<std::string> pending;
            if (!story_actors::editor_update(mod, actors, changed, w, &pending, c)) set_status("Live update refused: " + w);
            else if (!pending.empty()) {
                // grass, flowers and small trees (game objects in the stage's shared batch) wait for the room, not a restart
                // a world object where it can't stand: not in the game until it is moved (said as such, not as a restart)
                std::string list, room, held;
                for (const auto& p : pending) {
                    std::string& to = p.find("(Reload room)") != std::string::npos ? room : p.find(": can't stand here") != std::string::npos ? held : list;
                    to += (to.empty() ? "" : "; ") + p;
                }
                std::string status = held.empty() ? "" : "Not in the game: " + held;
                if (!list.empty() || !room.empty())
                    status += (status.empty() ? "" : ". ") + (list.empty() ? "Saved in the package; " + room : "Saved in the package; after a restart: " + list + (room.empty() ? "" : "; " + room));
                set_status(status);
            } else if (!changed.empty())
                set_status("Updated " + std::to_string(changed.size()) + " actor(s) in the game");
        });
        if (!later.empty()) {
            note = "Saved in the package. These take effect after a restart (save, then start the game again):";
            for (const auto& id : later) note += " " + id;
        } else
            note = changed.empty() ? "" : "Changed actors are created again in their room.";
        return true;
    }
    // a drag step, on the game thread (drag_tick): the actors that exist move in place
    bool pose_actors(const std::string& mod, const Value& poses, std::vector<std::string>& moved, std::string& why) override {
        if (!t_game) return why = "not on the game thread", false;
        for (const auto& pose : poses.array) {
            story_notebook::WorldPoint p, r;
            const Value &pv = pose.get("position"), &rv = pose.get("rotation");
            auto num = [](const Value& v, const char* k) { return float(v.get(k).number); };
            if (pv.type != Value::Object) continue;
            p = {num(pv, "x"), num(pv, "y"), num(pv, "z")};
            if (rv.type == Value::Object) r = {num(rv, "x"), num(rv, "y"), num(rv, "z")};
            if (story_actors::editor_pose(t_game, mod, pose.get("id").string(), p, r)) moved.push_back(pose.get("id").string());
        }
        return true;
    }
    bool preview_actors(const std::string& mod, const std::vector<story_notebook::ManifestActor>& actors, const std::vector<std::string>& changed,
                        std::vector<std::string>& later, std::string& why) override {
        // restart-only kinds keep their place until the change is saved and the game starts again
        std::vector<std::string> now;
        for (const auto& id : changed) {
            const auto it = std::find_if(actors.begin(), actors.end(), [&](const story_notebook::ManifestActor& a) { return a.id == id; });
            if (it != actors.end() && story_actors::editor_restart_only(*it)) later.push_back(id);
            else now.push_back(id);
        }
        if (now.empty()) return true;
        auto update = [mod, actors, now](Cpu*) {
            std::string w;
            std::vector<std::string> pending;
            if (!story_actors::editor_update(mod, actors, now, w, &pending)) set_status("Live update refused: " + w);
        };
        if (t_game) update(t_game);  // the drag's own step (drag_tick): at once
        else post(update);
        (void)why;
        return true;
    }
    bool apply_story(const std::string& mod, const std::string& name, const std::string& dir, const Value& started, const Value& manifest,
                     std::string& note, std::string& why) override {
        if (!story_notebook::installed())
            return why = "the story system was not running at start-up (it runs with the Story Notebook package on)", false;
        {
            std::lock_guard<std::mutex> lk(g_story_mu);
            g_story_pending[mod] = {name, dir, started, manifest};
        }
        const auto later = story_later(started, manifest);
        note = "Quests, quest talks, letters and charts change in the game at the next calm moment (after a talk, a letter or a scene).";
        if (!later.empty()) note += " After a restart (save, then start the game again): " + joined(later) + ".";
        return true;
    }
    bool live_kind(const std::string& type, bool fallback) override { return story_actors::editor_kind_live(type, fallback); }
    // the game's residents the NPC catalogue knows, as the story notebook sees them in their process step
    bool villagers(Value& out, std::string& why) override {
        if (!story_notebook::installed()) return why = "the story notebook is not loaded (the Story Notebook package is needed for quest talks)", false;
        out = Value();
        out.type = Value::Array;
        const story_notebook::NpcCatalogue& catalogue = story_notebook::npc_catalogue();
        std::vector<story_notebook::LiveVillager> seen;
        {
            std::lock_guard<std::mutex> lk(g_villagers_mu);
            seen = g_villagers;
        }
        for (const auto& v : seen) {
            const story_notebook::NpcEntry* e = catalogue.entry(v.resident);
            if (!e) continue;
            Value x, pos;
            x["id"] = e->name;
            x["name"] = e->display();
            x["description"] = e->description;
            x["where"] = e->where;
            x["stage"] = v.stage;
            x["room"] = v.room;
            pos["x"] = double(v.x), pos["y"] = double(v.y), pos["z"] = double(v.z);
            x["position"] = pos;
            x["talks"] = e->talks();
            out.array.push_back(x);
        }
        return true;
    }
    // new_mod: the package goes through the mod manager's own install (checked as any package is, refused
    // for an installed ID) from a staging folder of its own, then is turned on in the current profile
    bool create_package(const Value& manifest, std::string& dir, std::string& why) override {
        const std::string mods_dir = mods::packages::directory();
        if (mods_dir.empty()) return why = "the mod manager's storage is unavailable", false;
        const std::string id = manifest.get("id").string();
        for (const auto& v : mods::packages::list())
            if (v.id == id) return why = "a mod with the id \"" + id + "\" is installed already", false;
        std::error_code ec;
        const fs::path stage = fs::temp_directory_path(ec) /
                               ("wwhd-new-mod-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (ec || !fs::create_directory(stage, ec)) return why = "cannot create a staging folder for the new mod", false;
        std::string staged, error;
        bool ok = write_package_folder(stage.string(), manifest, staged, why);
        if (ok && !mods::packages::install_new(staged, error)) ok = false, why = "the mod manager refused the new mod: " + error;
        fs::remove_all(stage, ec);  // the staging copy only
        if (!ok) return false;
        if (!mods::packages::enable(id, true, error)) {
            // the package this call just installed (still empty, never turned on) goes again
            std::string ignored;
            mods::packages::remove(id, ignored);
            return why = "the new mod could not be turned on: " + error, false;
        }
        dir = (fs::path(mods_dir) / id).string();
        set_status("New mod " + id + " created and turned on");
        return true;
    }
    std::string game_archive(const std::string& archive) override { return editor::game_archive(config::game_dir, archive); }
    // ---- the demo step's pickers
    bool stage_actor_names(std::vector<std::string>& out, std::string& why) override {
        // The game's object name table (dStage_searchName's): entries of {name[8], process u16, subtype u8}
        // (12 bytes). Found through a name the game surely has, then read both ways while entries look
        // like names. Read once; it is static game data.
        static std::vector<std::string> cached;
        static std::mutex mu;
        {
            std::lock_guard<std::mutex> lk(mu);
            if (!cached.empty()) return out = cached, true;
        }
        static const uint32_t scratch = mem::host_alloc(16, 16);
        uint32_t entry = 0;
        if (!on_game([&](Cpu* c) {
                for (const char* probe : {"Link", "Oyashi", "bk", "Bk", "kamome"}) {
                    std::memset(mem::ptr(scratch), 0, 16);
                    std::memcpy(mem::ptr(scratch), probe, std::strlen(probe));
                    if ((entry = call(c, kSearchName, {scratch})) != 0) break;
                }
            }, why))
            return false;
        if (!entry) return why = "the game's object name table was not found", false;
        auto name_at = [](uint32_t e, std::string& n) {
            n.clear();
            for (uint32_t i = 0; i < 8; i++) {
                const char ch = char(ld8(e + i));
                if (!ch) {
                    for (uint32_t k = i; k < 8; k++)
                        if (ld8(e + k)) return false;
                    return !n.empty();
                }
                if (ch < '!' || ch > '~') return false;
                n += ch;
            }
            return false;  // eight characters and no end
        };
        uint32_t first = entry;
        std::string n;
        for (int i = 0; i < 4096 && name_at(first - 12, n); i++) first -= 12;
        std::set<std::string> names;
        for (uint32_t e = first; names.size() < 4096 && name_at(e, n); e += 12) names.insert(n);
        std::lock_guard<std::mutex> lk(mu);
        cached.assign(names.begin(), names.end());
        LOG("[editor] the game's object name table: %zu stage actor names", cached.size());
        return out = cached, true;
    }
    bool archive_files(const std::string& archive, std::vector<std::string>& out, std::string& why) override {
        out = story_demo::resource_names(archive);
        if (out.empty()) return why = "the game has no object archive called " + archive, false;
        return true;
    }
    bool archive_joints(const std::string& archive, std::vector<std::string>& out, std::string& why) override {
        out = editor::archive_bones(config::game_dir, archive);
        if (out.empty()) return why = "no joints found in the archive " + archive, false;
        return true;
    }
    std::vector<std::string> game_archives() override {
        static std::vector<std::string> cached = editor::game_archive_names(config::game_dir);
        return cached;
    }
    std::vector<NamedActor> more_actor_names() override {
        std::vector<NamedActor> out;
        for (const auto& e : story_notebook::npc_catalogue().entries)
            for (const auto& a : e.actors)
                if (!a.code.empty()) out.push_back({a.code, e.display(), "", e.name, "residents"});
        for (const auto& e : story_game_objects::catalogue().entries)
            if (!e.native_name.empty()) out.push_back({e.native_name, e.label, e.archive, e.id, "game objects"});
        return out;
    }
    bool play_sound(uint32_t id, std::string& why) override {
        if (!id || id > 0xFFFF) return why = "not a sound id", false;
        post([id](Cpu* c) { call(c, kSeStartSystem, {id}); });
        return true;
    }
    bool reload_room(std::string& why) override {
        (void)why;
        when_free([](Cpu*) {
            const auto p = ss::link_place();
            if (!p.valid) return set_status("Reload room: Link is not in the scene");
            ss::Warp w;
            w.stage = p.stage;
            w.room = p.room;
            w.at_position = true;
            w.angle = p.angle;
            std::memcpy(w.pos, p.pos, sizeof w.pos);
            ss::request_warp(w, [](bool ok, const std::string& why) { set_status(ok ? "Reloading the room" : "Reload room: " + why); });
        });
        return true;
    }
    std::vector<std::pair<std::string, std::vector<int>>> stage_rooms() override { return game_stage_rooms(config::game_dir); }
    Value stage_templates(std::string& why) override { return ::mod_stages::templates(config::game_dir, why); }
    Value rebuild_stages(const std::string& mod, const Value& manifest, const std::string& dir, const std::string& only, std::string& why) override {
        std::string data = mods::packages::package_data_directory(mod);
        if (data.empty() && !mods::packages::directory().empty()) data = (std::filesystem::path(mods::packages::directory()).parent_path() / "Data" / mod).string();
        if (data.empty()) return why = "the mod manager's folder is unknown", Value();
        Value out;
        out.type = Value::Array;
        const std::string here = ss::link_place().stage;
        bool in_rebuilt = false;
        for (const auto& r : ::mod_stages::rebuild(mod, manifest, dir, data, config::game_dir, only)) {
            in_rebuilt |= r.ok && r.name == here;
            Value v;
            v["id"] = r.id;
            v["ok"] = r.ok;
            if (!r.name.empty()) v["game_name"] = r.name;
            if (!r.error.empty()) v["problem"] = r.error;
            v["rooms"] = double(r.rooms);
            if (!r.notes.empty()) {
                Value& notes = v["notes"];
                notes.type = Value::Array;
                for (const auto& n : r.notes) notes.array.push_back(Value(n));
            }
            out.array.push_back(v);
        }
        // Link is in a rebuilt stage: it loads again where he stands (after the rebuilt files take effect)
        if (in_rebuilt) {
            std::string ignored;
            reload_room(ignored);
        }
        return out;
    }
    bool reset_stage_save(const std::string& mod, const std::string& group, std::string& why) override {
        struct State {
            std::mutex m;
            std::condition_variable cv;
            bool finished = false, done = false;
            std::string failure;
        };
        auto state = std::make_shared<State>();
        post([state, mod, group](Cpu*) {  // the game thread: the save area and the working record are the game's
            std::string failure;
            const bool done = ::mod_stages::reset_save(mod, group, failure);
            std::lock_guard l(state->m);
            state->done = done, state->failure = failure, state->finished = true;
            state->cv.notify_all();
        });
        std::unique_lock l(state->m);
        if (!state->cv.wait_for(l, std::chrono::seconds(5), [&] { return state->finished; })) return why = "the game didn't answer", false;
        if (!state->done) why = state->failure;
        return state->done;
    }
    std::vector<ModStage> mod_stages() override {
        std::vector<ModStage> out;
        for (const auto& s : ::mod_stages::stages()) {
            ModStage m{s.mod, s.id, s.name, {}};
            for (unsigned r : s.rooms) m.rooms.push_back(int(r));
            out.push_back(std::move(m));
        }
        return out;
    }
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
        const std::string label = to.label.empty() ? to.stage : to.label;
        when_free([w, label](Cpu*) {
            // the editor's view comes back at the destination, as the game's camera shows it
            g_resume.set = false;
            ss::request_warp(w, [label](bool ok, const std::string& why) { set_status(ok ? "Warping to " + label : "Warp: " + why); });
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
        // the fly camera holds Link in an event of its own: what needs him free waits for it to stop
        why = story_scenes::active() ? "a mod scene is running" : g_flying ? "" : ss::control_problem();
        return !why.empty();
    }
    bool fly(bool on, bool place_link, std::string& why) override {
        if (on == g_flying.load()) return why = on ? "the fly camera is already on" : "the fly camera is off", false;
        g_manual_off = !on;
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
            if (!test_save(why)) return false;
            when_free([](Cpu*) { test_start(nullptr); });
            return true;
        }
        g_test = false;
        const std::string start = (editor_dir() / "start.wwstate").string();
        when_free([start](Cpu*) {
            hle_fs::set_save_root("");
            if (uint8_t* block = story_store::memory_block(); block && g_story_before.size() == story_store::kMemoryBytes)
                std::memcpy(block, g_story_before.data(), g_story_before.size());
            std::error_code ec;
            if (fs::exists(start, ec)) {
                set_status("Restoring the world...");
                ss::request_load_portable_file(start,[](bool ok,const std::string&why){
                    set_status(ok?"Test mode off: the world was restored":"The world could not be restored: "+why);
                });
                g_retry = 450;
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
            // the mod's items first, as taking them does (an item that waited for a place gets the freed one)
            for (const auto& item : story_items::installed()) {
                const Record count{story_items::kRecordItemCount, item.key};
                if (item.mod != mod || std::find(own.begin(), own.end(), count) == own.end() ||
                    std::find(shared.begin(), shared.end(), count) != shared.end())
                    continue;
                if (const int32_t n = story_items::count(mod, item.id); n > 0) story_items::take(mod, item.id, uint32_t(n));
            }
            story_mail::Book book = story_store::book();
            unsigned skipped = 0;
            const unsigned n = editor::reset_records(book, own, shared, skipped);
            set_status("Reset " + mod + ": " + std::to_string(n) + " record(s) erased" +
                       (skipped ? ", " + std::to_string(skipped) + " shared with another mod kept" : ""));
        });
        return true;
    }
    bool test_state(bool save,int slot,const std::string&mod,const std::vector<Record>&own,std::string&why) override {
        return test_state_format(save,slot,mod,own,false,why);
    }
    bool test_state_format(bool save,int slot,const std::string&mod,const std::vector<Record>&own,bool full,std::string&why) override {
        std::error_code ec;
        const fs::path dir=editor_dir()/safe(mod);
        fs::create_directories(dir,ec);
        const std::string state=(dir/("test"+std::to_string(slot)+(full?".bin":".wwstate"))).string();
        const std::string records=(dir/("test"+std::to_string(slot)+".records")).string();
        if(save){
            when_free([state,records,own,slot,full](Cpu*){
                if(!full){
                    story_mail::Book book=story_store::book();
                    Value list;list.type=Value::Array;
                    for(const auto&r:own)if(book.has(r.kind,r.key)){
                        Value v;v["kind"]=int(r.kind);v["key"]=int(r.key);v["value"]=int(book.get(r.kind,r.key));
                        list.array.push_back(v);
                    }
                    std::ofstream(records,std::ios::binary|std::ios::trunc)<<mods::json::dump(list);
                }
                auto done=[slot](bool ok,const std::string&why){set_status(ok?"Test state "+std::to_string(slot)+" saved":"Test state not saved: "+why);};
                if(full)ss::request_save_full_file(state,done);else ss::request_save_portable_file(state,done);
            });
            return true;
        }
        if(!fs::exists(state,ec))return why="test slot "+std::to_string(slot)+" is empty",false;
        uint64_t request=0;
        {
            std::lock_guard lock(g_status_mu);
            if(g_test_load_serial==UINT64_MAX)return why="state request IDs exhausted",false;
            request=++g_test_load_serial;
            g_test_load_result=Value();g_test_load_result["request"]=std::to_string(request);
            g_test_load_result["slot"]=slot;g_test_load_result["pending"]=true;
            g_test_load_result["kind"]=full?"full":"portable";
        }
        when_free([state,records,own,slot,request,full](Cpu*){
            set_status("Loading test state "+std::to_string(slot));
                if(!full){
                    std::ifstream file(records,std::ios::binary);std::stringstream text;text<<file.rdbuf();
                    try{
                        const auto list=mods::json::parse(text.str());
                        story_mail::Book book=story_store::book();
                        for(const auto&r:own)book.erase(r.kind,r.key);
                        for(const auto&v:list.array){
                            const Record r{uint8_t(v.get("kind").number),uint16_t(v.get("key").number)};
                            if(std::find(own.begin(),own.end(),r)!=own.end())book.set(r.kind,r.key,uint8_t(v.get("value").number));
                        }
                    }catch(const std::exception&){}
                }
            auto done=[slot,request](bool ok,const std::string&why){
                {
                    std::lock_guard lock(g_status_mu);
                    if(request!=g_test_load_serial)return;
                    g_test_load_result["pending"]=false;g_test_load_result["ok"]=ok;g_test_load_result["reason"]=why;
                }
                set_status(ok?"Test state "+std::to_string(slot)+" loaded":"The world could not be restored: "+why);
            };
            if(full)ss::request_load_full_file(state,done);else ss::request_load_portable_file(state,done);
            g_retry=450;
        });
        return true;
    }

    Value load_state_result() override {
        std::lock_guard lock(g_status_mu);
        return g_test_load_result;
    }

    // ---- building new content ----
    bool ground(const WorldPoint& above, float& height, std::string& why) override {
        bool found = false;
        if (!on_game([&](Cpu* c) { found = ground_below(c, above.x, above.y, above.z, height); }, why)) return false;
        return found || (why = "no ground below (outside the loaded rooms?)", false);
    }
    bool grounds(const std::vector<WorldPoint>& above, std::vector<float>& heights, std::vector<bool>& found, std::string& why) override {
        heights.assign(above.size(), 0);
        found.assign(above.size(), false);
        return on_game([&](Cpu* c) { grounds_below(c, above, heights, found); }, why, 3000);
    }
    // the room's own collision height range, from the player's stage files (cached by the placement index)
    bool room_height(const std::string& stage, int room, float& low, float& high, std::string& why) override {
        return mod_actor::room_height_range(stage, room, low, high, why);
    }
    Value route_state(const std::string& mod, const std::string& id) override {
        Value v;
        std::string why;
        story_actors::ResidentRoute route;
        bool known = false;
        if (!on_game([&](Cpu*) { known = story_actors::resident_route(mod, id, route); }, why) || !known || !route.declared) return v;
        v["declared"] = true;
        v["live"] = route.live;
        v["status"] = route.live ? route.status : std::string("not in the game (another room, or it appears after a reload)");
        return v;
    }
    bool apply_paths(const std::string& mod, const std::vector<story_room_path::Path>& paths, std::string& why) override {
        if (!story_room_paths::memory()) return why = "the game has no memory for mod paths in this session", false;
        post([mod, paths](Cpu*) {
            std::string w;
            if (!story_room_paths::declare(mod, paths, w)) set_status("Paths not updated in the game: " + w);
        });
        return true;
    }
    Value path_state() override {
        Value v;
        v.type = Value::Array;
        for (const auto& i : story_room_paths::list()) {
            Value e;
            e["mod"] = i.mod;
            e["id"] = i.id;
            e["stage"] = i.stage;
            e["room"] = double(i.room);
            e["number"] = double(i.number);
            e["points"] = double(i.points);
            e["closed"] = i.closed;
            e["valid"] = i.valid;
            v.array.push_back(e);
        }
        return v;
    }
    bool ray(const WorldPoint& from, const WorldPoint& direction, float range, WorldPoint& hit, std::string& why) override {
        bool found = false;
        if (!on_game([&](Cpu* c) { found = ray_hit(c, from, direction, range, hit); }, why, 3000)) return false;
        return found || (why = "the ray meets no ground in the loaded rooms", false);
    }
    Place camera() override {
        const FlyView v = fly_view();
        Place p;
        if (!v.on) return p;
        p.valid = true;
        p.stage = v.stage;
        p.position = {v.eye[0], v.eye[1], v.eye[2]};
        p.room = v.stage == "sea" ? sea_room(v.eye[0], v.eye[2]) : v.room;
        if (p.room < 0) p.room = 0;
        p.angle = int(std::lround(std::atan2(double(v.target[0]) - v.eye[0], double(v.target[2]) - v.eye[2]) * 32768 / kPi)) & 0xFFFF;
        return p;
    }
    bool focus(const WorldPoint& target, std::string& why) override {
        (void)why;
        {
            std::lock_guard<std::mutex> lk(g_focus_mu);
            g_focus_set = true;
            g_focus[0] = target.x, g_focus[1] = target.y, g_focus[2] = target.z;
            g_focus_back = 700, g_focus_turn = false;
        }
        if (!g_flying) {
            g_manual_off = false;
            g_fly_want = 1;
        }
        return true;
    }
    bool focus_view(const WorldPoint& target, float distance, float yaw_degrees, std::string& why) override {
        if (!focus(target, why)) return false;
        std::lock_guard<std::mutex> lk(g_focus_mu);
        g_focus_back = std::clamp(double(distance), 200.0, 20000.0);
        g_focus_turn = std::isfinite(yaw_degrees);
        if (g_focus_turn) g_focus_yaw = double(yaw_degrees) * 3.14159265358979323846 / 180;
        return true;
    }
    bool prepare_model(const std::string& mod, const std::string& dir, const Value& model, std::string& note, std::string& why) override {
        (void)why;
        // the model tool runs on a thread of its own; the game takes the result at its next pass
        std::thread([mod, dir, model] {
            std::string archive, w;
            if (!guestmods::editor_prepare_model(mod, dir, mods::packages::package_data_directory(mod), model, archive, w)) return set_status("Model " + model.get("id").string() + ": " + w);
            post([mod, model, archive](Cpu*) {
                std::string w2;
                if (model.get("collision").boolean && !mod_actor::register_collision(mod, model.get("id").string(), actor_model_cache::collision_for(archive)))
                    return set_status("Model " + model.get("id").string() + ": prepared collision could not be registered");
                if (!story_actors::editor_register_model(mod, model.get("id").string(), archive, model.get("kind").string() == "skinned", w2))
                    return set_status("Model " + model.get("id").string() + ": " + w2 + " (it is prepared when the game starts)");
                set_status("Model " + model.get("id").string() + " is ready: actors can use it now");
                reapply_mod(mod);
            });
        }).detach();
        note = "The model is being prepared for the game; actors using it appear when it is ready.";
        return true;
    }
    bool prepare_piece(const std::string& mod, const std::string& id, std::string& note, std::string& why) override {
        if (!story_game_pieces::catalogue().find(id)) return why = "no such building", false;
        {
            std::lock_guard guard(g_piece_mutex);
            auto& state = g_piece_states[{mod, id}];
            if (state == "preparing" || state == "ready") return true;
            state = "preparing";
        }
        const std::string cache = story_game_pieces::cache_directory(mods::packages::package_data_directory(mod));
        // the converter runs on a thread of its own; the game takes the result at its next pass
        std::thread([mod, id, cache] {
            auto finish = [mod, id](const std::string& state) {
                std::lock_guard guard(g_piece_mutex);
                g_piece_states[{mod, id}] = state;
            };
            std::shared_ptr<const story_game_pieces::BuiltSet> set;
            try {
                set = guestmods::editor_build_piece(id, cache);
            } catch (const std::exception& e) {
                finish(std::string("failed: ") + e.what());
                return set_status("Building " + id + ": " + e.what());
            }
            post([mod, id, set, finish](Cpu*) {
                std::string w;
                if (!story_game_pieces::register_for_mod(mod, *set, id, true, w)) {
                    finish("failed: " + w);
                    return set_status("Building " + id + ": " + w);
                }
                finish("ready");
                // a building placed while it was being made: its place is checked now (as the game does when
                // it creates it), and one that can't stand there is said here instead of just staying away
                const std::string refused = refused_pieces(mod, id);
                set_status(refused.empty() ? "Building " + id + " is ready"
                                           : "Building " + id + " is ready, but can't stand where it is placed: " + refused + ". Move it; selecting it shows what is in the way.");
                reapply_mod(mod);
            });
        }).detach();
        note = "preparing... the building is made from your game files and appears when it is ready";
        return true;
    }
    std::string piece_state(const std::string& mod, const std::string& id) override {
        {
            std::lock_guard guard(g_piece_mutex);
            auto found = g_piece_states.find({mod, id});
            if (found != g_piece_states.end()) return found->second;
        }
        return story_actors::has_model(mod, story_game_pieces::model_key(id)) ? "ready" : "";
    }
    bool world_object_query(const std::string& mod, const story_notebook::ManifestActor& actor, bool wireframe, Value& result, std::string& why) override {
        if (!mod_actor::world_object_query(mod, actor, wireframe, result, why)) return false;
        // what is in the way names the game's characters and objects as the catalogues do
        name_layout_conflicts(actor, result);
        return true;
    }
    bool world_object_query_with(const std::string& mod, const story_notebook::ManifestActor& actor, const story_world_collision::mesh::Mesh& collision,
                                 Value& result, std::string& why) override {
        if (!mod_actor::world_object_query(mod, actor, false, result, why, std::make_shared<const story_world_collision::mesh::Mesh>(collision))) return false;
        name_layout_conflicts(actor, result);
        return true;
    }
    bool preview_text(const std::string& text, bool close, std::string& why) override {
        if (close) {
            post([](Cpu*) { g_preview.close = true; });
            return true;
        }
        if (!g_flying) return why = "the preview shows in the editor's view (the fly camera)", false;
        if (!story_mail::can_say()) return why = "the game's message hooks are not installed (no story mod is loaded)", false;
        std::u16string u;
        if (!story_mail::append_utf16(text, true, u)) return why = "the text can't be shown (characters the game has no glyphs for?)", false;
        post([u](Cpu* c) {
            if (g_preview.phase) preview_end(c, true);  // a new text replaces the shown one
            g_preview = {};
            g_preview.text = u;
            story_mail::say(kCarrier, &g_preview.text);
            g_preview.phase = 1;
        });
        return true;
    }
    bool look(const WorldPoint& eye, const WorldPoint& target, std::string& why) override {
        if (g_playing || g_previewing) return why = "not while playing", false;
        const double d[3] = {double(target.x) - eye.x, double(target.y) - eye.y, double(target.z) - eye.z};
        if (std::fabs(d[0]) + std::fabs(d[1]) + std::fabs(d[2]) < 1e-3) return why = "the eye and the target are one point", false;
        post([eye, d](Cpu*) {
            ResumeView v;
            v.set = true;
            v.eye[0] = eye.x, v.eye[1] = eye.y, v.eye[2] = eye.z;
            v.yaw = std::atan2(d[0], d[2]);
            v.pitch = std::atan2(d[1], std::hypot(d[0], d[2]));
            v.speed = g_fly.active ? g_fly.speed : 1000;
            if (g_fly.active) {
                for (int i = 0; i < 3; i++) g_fly.eye[i] = v.eye[i];
                g_fly.yaw = v.yaw, g_fly.pitch = v.pitch;
            } else {
                g_resume = v;
                g_manual_off = false;
                g_fly_want = 1;
            }
        });
        return true;
    }
    bool preview_scene(const std::string& mod, const story_notebook::ManifestScene& scene, bool declared, bool restore, std::string& why) override {
        if (g_playing) return why = "Stop first: a scene preview starts from the editor's view", false;
        if (g_previewing) return why = "a scene preview plays already (skip_scene or stop_scene)", false;
        if (!g_flying) return why = "a scene preview starts from the editor's view (the fly camera)", false;
        bool turned = false;
        if (!g_test) {  // as Play does: in-game saves go to the test save from now on
            if (!test_save(why)) return false;
            turned = true;
        }
        g_previewing = true;
        post([mod, scene, declared, restore, turned](Cpu* c) {
            if (g_preview.phase) preview_end(c, true);
            g_scene = {};
            g_scene.phase = 1;
            g_scene.mod = mod, g_scene.scene = scene, g_scene.declared = declared, g_scene.restore = restore;
            g_scene.view = current_view();
            g_resume = g_scene.view;
            g_fly_want = 2;  // the fly camera stops; Link stays where he is
            publish_scene("starting", 0);
            g_free_jobs.push_back([turned](Cpu*) {
                auto request = [](bool) {
                    post([](Cpu*) {
                        if (!g_previewing || g_scene.phase != 1) return;
                        std::string w;
                        if (g_scene.stop) return scene_preview_end("Preview stopped");
                        if (!story_scenes::editor_preview(g_scene.mod, g_scene.scene, g_scene.declared, w)) return scene_preview_end("Preview: " + w);
                        g_scene.phase = 2;
                        g_scene.frames = 0;
                        set_status("Preview: scene " + g_scene.scene.id + " requested");
                    });
                };
                auto snapshot = [request](bool ok) {
                    (void)ok;
                    if (!g_scene.restore) return request(true);
                    post([request](Cpu*) {
                        if (uint8_t* block = story_store::memory_block()) g_scene.story.assign(block, block + story_store::kMemoryBytes);
                        std::error_code ec;
                        fs::create_directories(fs::path(preview_file()).parent_path(), ec);
                        ss::request_save_portable_file(preview_file(), [request](bool ok2, const std::string& why2) {
                            g_scene.snapshot = ok2;
                            if (!ok2) set_status("Preview: the world can't be restored afterwards (" + why2 + ")");
                            request(ok2);
                        });
                    });
                };
                if (turned) test_start(snapshot);
                else snapshot(true);
            });
        });
        return true;
    }
    bool scene_control(bool stop, std::string& why) override {
        if (!g_previewing) return why = "no scene preview plays", false;
        post([stop](Cpu*) {
            if (stop) g_scene.stop = true;
            else g_scene.skip = true;
        });
        return true;
    }
    Value scene_state() override {
        std::lock_guard<std::mutex> lk(g_scene_mu);
        if (g_scene_state.type == Value::Null) {
            Value v;
            v["playing"] = false;
            return v;
        }
        return g_scene_state;
    }
    bool play(bool from_camera, bool restore, std::string& why) override { return request_play(from_camera, restore, why); }
    bool stop(std::string& why) override;
    bool playing() override { return g_playing; }
    // any thread, cheap: the game thread's last copy of the story area (records_tick)
    bool read_records(const std::vector<Record>& records, std::vector<int>& values, std::string& why) override {
        std::lock_guard<std::mutex> lk(g_records_mu);
        if (g_records_copy.empty()) return why = "no story progress in this game yet (no story mod, or no save file loaded)", false;
        const story_mail::Book book(g_records_copy.data(), story_block::kAreaRecords);
        values.clear();
        for (const auto& r : records) values.push_back(book.has(r.kind, r.key) ? int(book.get(r.kind, r.key)) : -1);
        return true;
    }
    bool write_records(const std::string& mod, const std::vector<RecordValue>& values, std::string& why) override {
        if (!g_test) return why = "test mode only", false;
        if (!story_store::ready()) return why = "no story progress in this game (no story mod is loaded)", false;
        post([mod, values](Cpu*) {
            story_mail::Book book = story_store::book();
            unsigned n = 0;
            for (const auto& v : values) n += v.value < 0 ? book.erase(v.record.kind, v.record.key) : book.set(v.record.kind, v.record.key, uint8_t(v.value));
            set_status("Set " + std::to_string(n) + " flag(s) of " + mod + " (test mode)");
            records_tick(true);
        });
        return true;
    }
    bool advance_days(int days, std::string& why) override {
        if (!g_test) return why = "test mode only", false;
        post([days](Cpu*) {
            const uint32_t save = ld32(GD(0x101F84DC));  // the save base (d_com_inf_game)
            if (!save) return set_status("No save file loaded: the calendar stays");
            const uint16_t today = uint16_t(ld16(save + story_notebook::kSaveDay) + days);
            st16(save + story_notebook::kSaveDay, today);
            set_status("The game's calendar is at day " + std::to_string(today) + " (test mode)");
        });
        return true;
    }
    bool start_at_step(const std::string& mod, const std::vector<Record>& erase, const std::vector<RecordValue>& values,
                       const std::vector<std::pair<std::string, int>>& items, const Location& to, bool play, std::string& why) override {
        if (!story_store::ready()) return why = "no story progress in this game (no story mod is loaded)", false;
        if (g_playing) return why = "Stop first: a quest starts at a step from the editor's view", false;
        bool turned = false;
        if (!g_test) {  // as Play does: in-game saves go to the test save from now on
            if (!test_save(why)) return false;
            turned = true;
        }
        const bool warp = !to.id.empty();
        ss::Warp w;
        if (warp) {  // the start-at-step destinations stand at a position (an actor's front, a step's place)
            w.stage = to.stage;
            w.room = to.room;
            w.at_position = true;
            w.pos[0] = to.position.x, w.pos[1] = to.position.y, w.pos[2] = to.position.z;
            w.angle = to.angle;
        }
        const std::string label = to.label;
        // the records first, then the warp: one state request at a time (test mode's return state first)
        auto go = [mod, erase, values, items, warp, w, play, label]() {
            post([mod, erase, values, items, warp, w, play, label](Cpu*) {
                story_mail::Book book = story_store::book();
                for (const auto& r : erase) book.erase(r.kind, r.key);
                unsigned quest_step = 0;
                for (const auto& v : values) {
                    if (v.record.kind == story_mail::kRecordTracked) {  // never untrack another mod's quest
                        const uint16_t now = story_notebook::tracked_key(book);
                        if (now && now != v.record.key) continue;
                    }
                    if (v.record.kind == story_mail::kRecordQuest) quest_step = unsigned(v.value);
                    book.set(v.record.kind, v.record.key, uint8_t(v.value));
                }
                for (const auto& [id, n] : items) {
                    const int have = std::max(0, int(story_items::count(mod, id)));
                    if (have < n) story_items::give(mod, id, uint32_t(n - have), "the editor (start at a step)", false);
                    else if (have > n) story_items::take(mod, id, uint32_t(have - n));
                }
                records_tick(true);
                set_status("Quest of " + mod + " at step " + std::to_string(quest_step) + (warp ? ": warping to " + label : ""));
                if (warp) {
                    g_resume.set = false;  // the editor's view comes back at the destination
                    ss::request_warp(w, [play](bool ok, const std::string& why) {
                        if (!ok) set_status("Start at step: " + why);
                        else if (play) g_play_after = 1;
                    });
                } else if (play)
                    g_play_after = 1;
            });
        };
        when_free([turned, go](Cpu*) {
            if (turned) test_start([go](bool) { go(); });  // the story area and place from before the test, kept first
            else go();
        });
        return true;
    }
};

std::recursive_mutex g_mu;  // the session (recursive: a script runs on the game thread inside logic_service)
GameHost g_host;
Session g_session(&g_host);

// The mod's actors as the session has them, applied again (a model became ready, a restart-only change
// is not involved): what waited for the model is created now.
std::string refused_pieces(const std::string& mod, const std::string& piece) {
    std::vector<story_notebook::ManifestActor> actors;
    {
        std::lock_guard<std::recursive_mutex> lk(g_mu);
        const Value* m = g_session.manifest(mod);
        std::string ignored;
        if (!m || !story_notebook::parse_actors(*m, actors, ignored)) return "";
    }
    std::string refused;
    for (const auto& a : actors) {
        if (!a.game_piece() || a.piece != piece) continue;
        Value r;
        std::string w;
        if (!mod_actor::world_object_query(mod, a, false, r, w)) continue;
        name_layout_conflicts(a, r);
        if (r.get("clear").boolean || r.get("conflicts").array.empty()) continue;
        refused += (refused.empty() ? "" : "; ") + a.id + " (in the way: " + r.get("conflicts").array.front().get("message").string() + ")";
    }
    return refused;
}
void reapply_mod(const std::string& mod) {
    std::vector<story_notebook::ManifestActor> actors;
    {
        std::lock_guard<std::recursive_mutex> lk(g_mu);
        const Value* m = g_session.manifest(mod);
        std::string why;
        if (!m || !story_notebook::parse_actors(*m, actors, why)) return;
    }
    std::string why;
    std::vector<std::string> pending;
    if (!story_actors::editor_update(mod, actors, {}, why, &pending)) set_status("Live update refused: " + why);
}

// ---------------------------------------------------------------- the handle drag
// The render thread leaves the latest poses here (never waiting); the game thread shows them at most once per
// logic step, when the session is free (a command that waits for the game holds it: the next step then).
// An actor that exists in the game moves in place (story_actors::editor_pose); the release's command then
// keeps that copy when it stands where the command puts it (the drag's end check, editor_update).
std::mutex g_drag_mu;
struct DragSlot {
    std::string mod;
    Value poses;            // the latest poses (or the ids to put back, without places)
    uint64_t seq = 0, shown = 0;
    std::string note;       // what can't move live
} g_drag;
constexpr int kDragSettle = 4;  // logic steps a re-created actor shows before the next pose (its first frames draw nothing)
void drag_tick(Cpu* c) {
    // An actor without a copy to move (not created yet) is created again at its new place, and a new
    // actor takes a few logic steps to load and draw: the next pose waits until those actors exist and
    // have shown for kDragSettle steps, or a drag would remove them before they ever show.
    static int settled = kDragSettle;
    static std::vector<std::string> shown_ids;
    static std::string shown_mod;
    if (story_actors::editor_pending()) return;
    for (const auto& id : shown_ids)
        if (!story_actors::actor_address(c, shown_mod, id)) return void(settled = 0);
    if (settled < kDragSettle) return void(++settled);
    std::string mod;
    Value poses;
    uint64_t seq;
    {
        std::lock_guard<std::mutex> lk(g_drag_mu);
        if (g_drag.seq == g_drag.shown) return;
        mod = g_drag.mod, poses = g_drag.poses, seq = g_drag.seq;
    }
    std::unique_lock<std::recursive_mutex> session(g_mu, std::try_to_lock);
    if (!session.owns_lock()) return;  // the next logic step
    std::vector<std::string> later, in_place;
    std::string why;
    const bool ok = g_session.drag_preview(mod, poses, later, why, &in_place);
    session.unlock();
    std::string note;
    if (!ok) note = "Live move: " + why;
    else if (!later.empty()) {
        note = "Moves when you let go, shows after a restart:";
        for (const auto& id : later) note += " " + id;
    }
    // actors moved in place show at once; only those created again need their settle time
    shown_mod = mod, shown_ids.clear();
    if (ok)
        for (const auto& pose : poses.array) {
            const std::string id = pose.get("id").string();
            if (std::find(later.begin(), later.end(), id) == later.end() && std::find(in_place.begin(), in_place.end(), id) == in_place.end()) shown_ids.push_back(id);
        }
    settled = shown_ids.empty() ? kDragSettle : 0;
    std::lock_guard<std::mutex> lk(g_drag_mu);
    g_drag.shown = seq;
    g_drag.note = note;
}

// ---------------------------------------------------------------- scene preview (game thread)
// A queued load is not a completed restore. Keep both the status line and log
// honest on parse errors, gate refusals and failed arrival as well as success.
void restore_world(const std::string&path,const std::string&success,const std::vector<uint8_t>&story){
    // Portable states deliberately exclude mod records. Restore these before
    // the stage changes, so actors/triggers see the pre-preview progress.
    if(uint8_t*block=story_store::memory_block();block&&story.size()==story_store::kMemoryBytes)
        std::memcpy(block,story.data(),story.size());
    set_status("Restoring the world...");
    ss::request_load_portable_file(path,[success](bool ok,const std::string&why){
        set_status(ok?success:"The world could not be restored: "+why);
    });
}
void scene_preview_end(const std::string& status) {
    const bool restore = g_scene.restore && g_scene.snapshot;
    g_resume = g_scene.view;
    g_scene.phase = 4;
    g_previewing = false;
    g_manual_off = false;
    g_retry = 450;
    if (!launch_mode()) g_fly_want = 1;  // the launch mode's editor view comes back by itself once Link is free
    publish_scene("", 0);
    if(restore)restore_world(preview_file(),status+": the world was restored",g_scene.story);
    else set_status(status);
}
void scene_preview_tick() {
    if (!g_previewing || g_scene.phase < 2 || g_scene.phase > 3) return;
    bool waiting = false, playing = false, native = false;
    size_t step = 0;
    const std::string id = g_scene.declared ? g_scene.scene.id : std::string("editor-preview");
    story_scenes::editor_state(g_scene.mod, id, waiting, playing, native, step);
    if (g_scene.stop) {
        story_scenes::abort();  // ends the scene without its end actions, or drops the request
        if (!story_scenes::active()) return scene_preview_end("Preview stopped");
        return;
    }
    if (g_scene.skip && playing) {
        g_scene.skip = false;
        story_scenes::editor_skip();
    }
    if (g_scene.phase == 2) {
        if (playing) {
            g_scene.phase = 3;
            g_scene.native = native;
            set_status(std::string("Preview: scene ") + g_scene.scene.id + (native ? " plays (its native version)" : " plays"));
        } else if (!waiting || ++g_scene.frames > 30 * 20)
            return scene_preview_end(waiting ? "Preview: the scene did not start within 20 seconds" : "Preview: the game did not start the scene");
        else
            return publish_scene("starting", 0);
    }
    if (playing) return publish_scene("playing", step + 1);
    if (!story_scenes::active()) scene_preview_end("Preview ended");
}

// ---------------------------------------------------------------- Play / Stop
bool request_play(bool from_camera, bool restore, std::string& why) {
    if (g_playing) return why = "already playing (Stop first)", false;
    if (!g_flying) return why = "Play starts from the editor's view (the fly camera)", false;
    g_playing = true;
    bool turned_test = false;
    if (!g_test) {
        std::string w;
        turned_test = test_save(w);
        if (!turned_test) set_status("Play without test mode: " + w);
    }
    post([from_camera, restore, turned_test](Cpu* c) {
        if (g_preview.phase) preview_end(c, true);
        g_play.restore = restore;
        g_play.view = current_view();
        g_play.snapshot = false;
        g_fly_want = from_camera ? 3 : 2;
        // once Link is free: test mode's start, and the world as it is now (for Stop) after it: one state
        // request at a time
        g_free_jobs.push_back([restore, turned_test](Cpu*) {
            auto snapshot = [restore](bool) {
                if (!restore) return;
                post([](Cpu*) {
                    if (uint8_t* block = story_store::memory_block()) g_play.story.assign(block, block + story_store::kMemoryBytes);
                    else g_play.story.clear();
                    std::error_code ec;
                    fs::create_directories(fs::path(play_state_file()).parent_path(), ec);
                    ss::request_save_portable_file(play_state_file(), [](bool ok, const std::string& why) {
                        g_play.snapshot = ok;
                        if (!ok) set_status("Play: the world can't be restored at Stop (" + why + ")");
                    });
                });
            };
            if (turned_test) test_start(snapshot);
            else snapshot(true);
        });
        set_status(from_camera ? "Playing from the camera's place (Tab or Stop: back to the editor)" : "Playing from Link's place (Tab or Stop: back to the editor)");
    });
    return true;
}
bool GameHost::stop(std::string& why) {
    if (!g_playing) return why = "not playing", false;
    post([](Cpu*) {
        g_resume = g_play.view;
        if (g_play.restore && g_play.snapshot) {
            restore_world(play_state_file(),"Stopped: the world was restored to before Play",g_play.story);
        } else
            set_status("Stopped");
        g_playing = false;
        g_manual_off = false;
        if (!launch_mode()) {  // the launch mode's editor view comes back by itself once Link is in the world
            g_retry = 450;
            g_fly_want = 1;
        }
    });
    return true;
}

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
    bool content = false;  // world content, quests, letters, charts or items
    for (const char* k : {"actors", "models", "scenes", "quests", "letters", "charts", "items", "paths", "stages"}) content = content || manifest.get(k).type != Value::Null;
    if (!content) return;
    std::lock_guard<std::recursive_mutex> lk(g_mu);
    g_session.add_package(mod, name, dir, manifest);
    LOG("[editor] package %s registered (%zu actors)", mod.c_str(), manifest.get("actors").array.size());
    // a model the start-up couldn't prepare: said in the status bar (its actors wait, the editor can prepare it again)
    for (const auto& note : guestmods::world_model_notes(mod)) set_status(mod + ": " + note);
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
bool try_with_session(const std::function<void(Session&)>& f) {
    std::unique_lock<std::recursive_mutex> lk(g_mu, std::try_to_lock);
    if (!lk.owns_lock()) return false;
    f(g_session);
    return true;
}
// one worker runs the UI's commands in order: a command that waits for the game never holds up a frame
namespace {
struct Queued {
    std::string command;
    Value params;
    std::function<void(const Value&)> done;
    std::string source;
};
std::mutex g_queue_mu;
std::condition_variable g_queue_cv;
std::deque<Queued> g_queue;
std::atomic<int> g_queued{0};
}  // namespace
void run_async(const std::string& command, const Value& params, std::function<void(const Value&)> done, const std::string& source) {
    static std::once_flag started;
    std::call_once(started, [] {
        std::thread([] {
            for (;;) {
                Queued q;
                {
                    std::unique_lock<std::mutex> lk(g_queue_mu);
                    g_queue_cv.wait(lk, [] { return !g_queue.empty(); });
                    q = std::move(g_queue.front());
                    g_queue.pop_front();
                }
                Value r;
                {
                    std::lock_guard<std::recursive_mutex> lk(g_mu);
                    r = g_session.run(q.command, q.params, true, q.source);
                }
                if (q.done) q.done(r);
                --g_queued;
            }
        }).detach();
    });
    ++g_queued;
    std::lock_guard<std::mutex> lk(g_queue_mu);
    g_queue.push_back({command, params, std::move(done), source});
    g_queue_cv.notify_one();
}
bool commands_pending() { return g_queued.load() > 0; }
void drag_actors(const std::string& mod, const Value& poses) {
    std::lock_guard<std::mutex> lk(g_drag_mu);
    if (g_drag.mod == mod && g_drag.poses == poses) return;  // held still: nothing new to show
    g_drag.mod = mod;
    g_drag.poses = poses;
    ++g_drag.seq;
}
void end_drag(const std::string& mod, const std::vector<std::string>& ids, bool put_back) {
    std::lock_guard<std::mutex> lk(g_drag_mu);
    g_drag.note.clear();
    if (!put_back || ids.empty()) {
        g_drag.shown = g_drag.seq;  // nothing more to show: the released command applies the change
        g_drag.poses = Value();
        return;
    }
    Value back;
    back.type = Value::Array;
    for (const auto& id : ids) {
        Value pose;
        pose["id"] = id;
        back.array.push_back(pose);
    }
    g_drag.mod = mod;
    g_drag.poses = back;
    ++g_drag.seq;
}
std::string drag_note() {
    std::lock_guard<std::mutex> lk(g_drag_mu);
    return g_drag.note;
}
void note(const std::string& command, const Value& params, const Value& result, const std::string& source) {
    std::lock_guard<std::recursive_mutex> lk(g_mu);
    g_session.note(command, params, result, source);
}
std::string status() {
    std::lock_guard<std::mutex> lk(g_status_mu);
    return g_status;
}

namespace {
void records_tick(bool now) {
    static int pass = 0;
    if (++pass % 8 && !now) return;  // about four times a second
    const uint8_t* area = story_store::ready() ? story_store::area() : nullptr;
    std::vector<uint8_t> copy;
    if (area) copy.assign(area, area + story_block::kAreaSize);
    std::lock_guard<std::mutex> lk(g_records_mu);
    g_records_copy = std::move(copy);
}
}  // namespace

namespace {
// the game thread, twice a second: the villagers in the actor list (see g_villagers)
void villagers_tick() {
    static unsigned n = 0;
    if (n++ % 15) return;
    const ss::LinkPlace at = ss::link_place();
    std::vector<story_notebook::LiveVillager> seen;
    if (at.valid) seen = story_notebook::scan_villagers(at.stage);
    std::lock_guard<std::mutex> lk(g_villagers_mu);
    g_villagers = std::move(seen);
}
}  // namespace

void logic_service(Cpu* c) {
    if (!enabled()) return;
    t_game = c;
    records_tick();
    villagers_tick();
    // the jobs first: a command waiting on the game thread (a ground check) never waits for a script
    std::vector<std::function<void(Cpu*)>> jobs;
    {
        std::lock_guard<std::mutex> lk(g_jobs_mu);
        jobs.swap(g_jobs);
    }
    for (auto& job : jobs) job(c);
    run_free_jobs(c);
    story_tick();  // a story change at a calm moment
    // start at a step: Play from Link once the editor's view is back (after the warp), half a second calm
    if (g_play_after > 0) {
        if (g_playing) g_play_after = 0;
        else if (g_flying && g_free_jobs.empty()) {
            if (++g_play_after > 15) {
                g_play_after = 0;
                std::string why;
                if (!request_play(false, true, why)) set_status("Play: " + why);
            }
        } else
            g_play_after = 1;
    }
    run_script();
    scene_preview_tick();
    drag_tick(c);
    preview_tick(c);
    t_game = nullptr;
}

void camera_service(Cpu* c, bool logic_pass) {
    if (!enabled()) return;
    const bool t60 = true60::enabled();
    if (!logic_pass && !t60) return;  // an interpolation-only pass draws between logic steps: no state change
    t_game = c;
    // the launch mode's editor view: the fly camera whenever Link is in the world and free, unless playing
    // (or the fly camera was turned off by hand); it comes back after warps, loads and stage changes
    static int calm = 0;
    if (launch_mode() && logic_pass && !g_playing && !g_previewing && !g_manual_off && !g_fly.active && g_fly_want == 0 && g_free_jobs.empty()) {
        const uint32_t play = call(c, kPlayFn);
        const bool free = play && ld32(play + kPlayer) && !story_scenes::active() && ss::control_problem().empty();
        calm = free ? calm + 1 : 0;
        if (calm >= 20) {  // two thirds of a second under control: the world is there
            calm = 0;
            g_fly_want = 1;
            if (!g_resume.set) LOG("[editor] editor view: fly camera on");
        }
    } else
        calm = 0;
    fly_tick(c, t60 ? 1.0 / 60 : 1.0 / 30);
    t_game = nullptr;
}

bool playing() { return g_playing; }
bool previewing() { return g_previewing; }
std::string preview_label() {
    std::lock_guard<std::mutex> lk(g_scene_mu);
    if (!g_scene_state.get("playing").boolean) return {};
    std::string s = "Scene " + g_scene_state.get("scene").string();
    if (g_scene_state.get("step").type == Value::Number) s += ": step " + std::to_string(int(g_scene_state.get("step").number));
    else s += ": " + g_scene_state.get("phase").string();
    if (g_scene_state.get("native").boolean) s += " (native)";
    return s;
}
bool editor_view() { return launch_mode() && !g_playing && !g_previewing && g_flying; }
void toggle_play() {
    std::string why;
    if (g_playing) {
        if (!g_host.stop(why)) set_status("Stop: " + why);
    } else if (!request_play(true, true, why))
        set_status("Play: " + why);
}
std::mutex g_view_mu;
CameraView g_camera;  // under g_view_mu
void camera_drawn(uint32_t cam) {
    if (!enabled() || !cam) return;
    CameraView v;
    v.valid = true;
    for (int i = 0; i < 3; i++) {
        v.eye[i] = float(ldf32(cam + 0xDC + 4 * i));
        v.center[i] = float(ldf32(cam + 0xE8 + 4 * i));
        v.up[i] = float(ldf32(cam + 0xF4 + 4 * i));
    }
    v.near_z = float(ldf32(cam + 0xCC));
    v.far_z = float(ldf32(cam + 0xD0));
    v.fovy = float(ldf32(cam + 0xD4));
    v.aspect = float(ldf32(cam + 0xD8));
    for (int i = 0; i < 16; i++) v.proj[i] = float(ldf32(cam + 0x1A4 + 4 * i));
    std::lock_guard<std::mutex> lk(g_view_mu);
    g_camera = v;
}
CameraView camera_view() {
    std::lock_guard<std::mutex> lk(g_view_mu);
    return g_camera;
}
void add_look(float dx, float dy) {
    if (!g_flying.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lk(g_fly_mu);
    g_in.mdx += dx;
    g_in.mdy += dy;
}
void add_speed(float notches) {
    if (!g_flying.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lk(g_fly_mu);
    g_in.wheel += notches;
}

void filter_pad(input::PadState& s) {
    if (!g_flying.load(std::memory_order_relaxed)) return;
    if (overlay::wants_text()) s = input::PadState{};  // typing into the editor's fields moves nothing
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
