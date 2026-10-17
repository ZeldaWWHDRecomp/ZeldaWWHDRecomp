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
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <thread>

#include "../gfx/renderer.h"
#include "../hle/save_root.h"
#include "../input.h"
#include "../mods/guest_mods.h"
#include "../mods/packages.h"
#include "../mods/story_actors.h"
#include "../mods/story_mail.h"
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
            const double back = 700, up = 350;
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
bool ground_below(Cpu* c, double x, double y, double z, float& h) {
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
// the first ground a ray meets: steps along it and narrows the step where it passes below the ground
bool ray_hit(Cpu* c, const WorldPoint& from, const WorldPoint& d, float range, WorldPoint& hit) {
    const double step = 60;
    double prev_t = 0;
    float h = 0;
    if (ground_below(c, from.x, from.y, from.z, h) && from.y < h - 1) return false;  // starts below the ground
    for (double t = step; t <= range + step; t += step) {
        const double tt = std::min<double>(t, range);
        const double x = from.x + d.x * tt, y = from.y + d.y * tt, z = from.z + d.z * tt;
        if (ground_below(c, x, y + 1, z, h) && y <= h) {
            double lo = prev_t, hi = tt;
            for (int i = 0; i < 12; i++) {
                const double mid = (lo + hi) / 2;
                float hm = 0;
                const double mx = from.x + d.x * mid, my = from.y + d.y * mid, mz = from.z + d.z * mid;
                if (ground_below(c, mx, my + 1, mz, hm) && my <= hm) hi = mid;
                else lo = mid;
            }
            const double hx = from.x + d.x * hi, hz = from.z + d.z * hi;
            float hy = 0;
            if (!ground_below(c, hx, from.y + d.y * hi + 1, hz, hy)) hy = float(from.y + d.y * hi);
            hit = {float(hx), hy, float(hz)};
            return true;
        }
        prev_t = tt;
        if (tt >= range) break;
    }
    return false;
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
struct PlayState {
    bool restore = true, turned_test = false, snapshot = false;
    ResumeView view;
    std::vector<uint8_t> story;  // the story area at Play
} g_play;                        // game thread
std::string play_state_file() { return (fs::path(ss::states_dir()) / "editor-test" / "play.wwstate").string(); }

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

// ---------------------------------------------------------------- the host
void reapply_mod(const std::string& mod);  // below: the live actors again (a model became ready)
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
        post([mod, actors, changed](Cpu*) {
            std::string w;
            std::vector<std::string> pending;
            if (!story_actors::editor_update(mod, actors, changed, w, &pending)) set_status("Live update refused: " + w);
            else if (!pending.empty()) {
                std::string list;
                for (const auto& p : pending) list += (list.empty() ? "" : "; ") + p;
                set_status("Saved in the package; after a restart: " + list);
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
                ss::request_load_portable_file(start);
                g_retry = 450;
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
            when_free([state, records, own, slot](Cpu*) {
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
        when_free([state, records, own, slot](Cpu*) {
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
            g_retry = 450;
            set_status("Loading test state " + std::to_string(slot));
        });
        return true;
    }

    // ---- building new content ----
    bool ground(const WorldPoint& above, float& height, std::string& why) override {
        bool found = false;
        if (!on_game([&](Cpu* c) { found = ground_below(c, above.x, above.y, above.z, height); }, why)) return false;
        return found || (why = "no ground below (outside the loaded rooms?)", false);
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
        }
        if (!g_flying) {
            g_manual_off = false;
            g_fly_want = 1;
        }
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
                if (!story_actors::editor_register_model(mod, model.get("id").string(), archive, model.get("kind").string() == "skinned", w2))
                    return set_status("Model " + model.get("id").string() + ": " + w2 + " (it is prepared when the game starts)");
                set_status("Model " + model.get("id").string() + " is ready: actors can use it now");
                reapply_mod(mod);
            });
        }).detach();
        note = "The model is being prepared for the game; actors using it appear when it is ready (a restart is needed for the first model "
               "actor of a game that had none).";
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
    bool play(bool from_camera, bool restore, std::string& why) override { return request_play(from_camera, restore, why); }
    bool stop(std::string& why) override;
    bool playing() override { return g_playing; }
};

std::recursive_mutex g_mu;  // the session (recursive: a script runs on the game thread inside logic_service)
GameHost g_host;
Session g_session(&g_host);

// The mod's actors as the session has them, applied again (a model became ready, a restart-only change
// is not involved): what waited for the model is created now.
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
            if (uint8_t* block = story_store::memory_block(); block && g_play.story.size() == story_store::kMemoryBytes)
                std::memcpy(block, g_play.story.data(), g_play.story.size());
            ss::request_load_portable_file(play_state_file());
            set_status("Stopped: the world is back as it was before Play");
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
    if (manifest.get("actors").type == Value::Null && manifest.get("models").type == Value::Null && manifest.get("scenes").type == Value::Null &&
        manifest.get("quests").type == Value::Null)
        return;
    std::lock_guard<std::recursive_mutex> lk(g_mu);
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
};
std::mutex g_queue_mu;
std::condition_variable g_queue_cv;
std::deque<Queued> g_queue;
std::atomic<int> g_queued{0};
}  // namespace
void run_async(const std::string& command, const Value& params, std::function<void(const Value&)> done) {
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
                const Value r = run(q.command, q.params);
                if (q.done) q.done(r);
                --g_queued;
            }
        }).detach();
    });
    ++g_queued;
    std::lock_guard<std::mutex> lk(g_queue_mu);
    g_queue.push_back({command, params, std::move(done)});
    g_queue_cv.notify_one();
}
bool commands_pending() { return g_queued.load() > 0; }
std::string status() {
    std::lock_guard<std::mutex> lk(g_status_mu);
    return g_status;
}

void logic_service(Cpu* c) {
    if (!enabled()) return;
    t_game = c;
    // the jobs first: a command waiting on the game thread (a ground check) never waits for a script
    std::vector<std::function<void(Cpu*)>> jobs;
    {
        std::lock_guard<std::mutex> lk(g_jobs_mu);
        jobs.swap(g_jobs);
    }
    for (auto& job : jobs) job(c);
    run_free_jobs(c);
    run_script();
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
    if (launch_mode() && logic_pass && !g_playing && !g_manual_off && !g_fly.active && g_fly_want == 0 && g_free_jobs.empty()) {
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
bool editor_view() { return launch_mode() && !g_playing && g_flying; }
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
