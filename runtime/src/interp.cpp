#include "mods/map_hud.h"
#include "interp.h"
#include "dsweep.h"
#include "mods/story_native_restore_resume.h"
namespace interp {class NativeResumeBoundaryIssuer {public:static void after_service(Cpu*c){story_native_restore_resume::LoadWindow::service_return(c);}};}
#include "countdown.h"
#include "mods/guest_mods.h"
#include "mods/mods.h"
#include "mods/packages.h"
// Frame interpolation (60, 120 or 240 fps output, game logic unchanged at 30 steps per second).
//
// With interpolation on, the main loop body runs N+1 times per logic step (N in-between frames:
// 1 at 60 fps, 3 at 120, 7 at 240; the virtual vsync of gx2_core.cpp ticks fast enough for them)
// but the game logic only on the first pass of each step:
//   logic pass:  logic advances S -> S+1; everything is drawn at t = 1/(N+1) between S and S+1
//   hold pass k (k = 1..N): no logic (no execute/create/delete, scene management, counters, audio,
//                HD UI screen updates);
//                everything is drawn again at t = (k+1)/(N+1); the last one (k = N) draws S+1
//                exactly and records it as the "before" of the next step's blended frames
// The painter at the start of each pass renders the previous pass's draw lists, so at 60 fps the
// screen shows halfway(S,S+1), S+1, halfway(S+1,S+2), S+2, ... and at 120 fps
// S+1/4, S+1/2, S+3/4, S+1, ...
// Hold passes 1..N-1 are "blended hold passes": no logic, but drawn blended like the logic pass.
// Everything that blends (camera, model matrices here; effects in interp_fx.cpp) takes the
// fraction from pass_t(); at 60 fps (t = 1/2) the arithmetic is bit-identical to the original
// halfway code (lerp_f() and friends special-case 1/2).
//
// Main loop functions in WWHD: see tools/recomp/hooks.txt and docs/decomp-notes.md.
// Camera layout (camera_draw, 024FFC40): near +0xCC, far +0xD0, fovy +0xD4, aspect +0xD8,
// eye +0xDC, center +0xE8, up +0xF4, bank (s16) +0x100.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "interp_camera.h"
#include "interp_fade.h"
#include "interp_joints.h"
#include "interp_nodraw.h"
#include "interp_pacing.h"
#include "interp_sploosh.h"
#include "mods/fast_forward.h"
#include "guest_addr.h"
#include "render_prof.h"
#include "perf_metrics.h"
#include "runtime.h"
#include "savestate.h"
#include "true60.h"
#include "true60_steps.h"
#include "gx2/gx2.h"
#include "gfx/renderer.h"


extern "C" {
void f_025F172C_orig(Cpu* c);  // main loop body
void f_0203593C_orig(Cpu* c);  // per-frame function (HD systems around the loop body)
void f_020315CC_orig(Cpu* c);  // audio frame callback (JAIZelBasic::gframeProcess)
void f_02032FE8_orig(Cpu* c);
void f_020357CC_orig(Cpu* c);
void f_025EE048_orig(Cpu* c);
void f_02756170_orig(Cpu* c);
void f_02617AF4_orig(Cpu* c);
void f_02614F74_orig(Cpu* c);
void f_0273841C_orig(Cpu* c);
void f_0270870C_orig(Cpu* c);
void f_0255E854_orig(Cpu* c);
void f_02738438_orig(Cpu* c);
void f_0278FEBC_orig(Cpu* c);
void f_027199C0_orig(Cpu* c);
void f_02618940_orig(Cpu* c);
void f_02728A74_orig(Cpu* c);
void f_02039834_orig(Cpu* c);

void f_025D42EC(Cpu* c);       // fapGm_Execute
void f_025DE788_orig(Cpu* c);  // fpcEx_Handler
void f_025DE024_orig(Cpu* c);  // fpcDt_Handler
void f_025E0EE4_orig(Cpu* c);  // fpcPi_Handler
void f_025DDCEC_orig(Cpu* c);  // fpcCt_Handler
void f_025D42C4_orig(Cpu* c);  // fapGm_After
void f_0200E6EC_orig(Cpu* c);  // cCt_Counter
void f_024FFC40_orig(Cpu* c);  // camera_draw
void f_025E1B44_orig(Cpu* c);  // mDoAud_getCameraInfo
void f_02027754_orig(Cpu* c);  // JAIZelBasic::setCameraPolygonPos
void f_0201EBA0_orig(Cpu* c);
void f_025E1988_orig(Cpu* c);
void f_025E19CC_orig(Cpu* c);
void f_025E1A04_orig(Cpu* c);
void f_025E1A40_orig(Cpu* c);
void f_025E1A7C_orig(Cpu* c);
void f_025E1AA4_orig(Cpu* c);
void f_027F55FC_orig(Cpu* c);  // J3DModel::viewCalc
void f_027F5018_orig(Cpu* c);  // J3DModel UBO update

}

namespace true60_test { void logic_step(uint64_t step); }

namespace interp {

// Keep periodic diagnostic I/O off the game thread unless requested. Model
// cleanup and the pacing counters still run regardless of whether logs are on.
static bool performance_reports() {
    static const bool on = getenv("WWHD_INTERP_PASS_STATS") || getenv("WWHD_LOG_FLIP_PACING") || getenv("WWHD_VK_STATS");
    return on;
}

// Output frame rate with interpolation: 40, 60, 120 or 240 (in-between frames per step: fps/30 - 1;
// at 40 fps one per three steps, pacing::is_fps40). WWHD_INTERP_FPS=40|60|120|240 sets it at start (and switches interpolation on); it wins over the
// saved choice and is not saved.
using pacing::valid_fps;
static const int g_env_fps = [] { const char* e = getenv("WWHD_INTERP_FPS"); return e ? valid_fps(atoi(e)) : 0; }();
static std::atomic<int> g_fps{g_env_fps ? g_env_fps : 60};
static std::atomic<bool> g_on{[] {
    const char* e = getenv("WWHD_INTERP");
    return (e ? atoi(e) != 0 : g_env_fps != 0) && !true60::enabled();
}()};
bool interp_on() { return g_on.load(std::memory_order_relaxed) && !mods::fast_forward_active(); }
// the pass structure below is used by both 60 fps modes and the 120/240 fps interpolation:
// interpolation (30 Hz logic) and true 60 (true60.cpp: 60 Hz processes also execute on the
// in-between "hold" passes; always one per step)
bool enabled() { return interp_on() || true60::enabled(); }
int fps() { return g_fps.load(std::memory_order_relaxed); }  // the chosen rate

// The display: refresh rate of the TV window's screen (hosts: gfx/display.mm, the SDL main loop;
// 0 = unknown) and whether presenting waits for its vsync (Metal; Vulkan FIFO). Frames beyond the
// refresh rate never reach the screen with vsync and would only slow the game down (or, paced, be
// dropped unevenly), so the drawn rate is capped to what the display shows (pacing::cap_fps: 240 fps
// on a 120 Hz display draws 120, on a 60 Hz display 60). WWHD_DISPLAY_HZ=n overrides the detected
// rate (0: no cap; tests and benchmarks with hidden windows).
static const int g_env_hz = getenv("WWHD_DISPLAY_HZ") ? atoi(getenv("WWHD_DISPLAY_HZ")) : -1;
static std::atomic<int> g_display_hz{0};
static std::atomic<bool> g_present_vsync{true};
int display_hz() { return g_env_hz >= 0 ? g_env_hz : g_display_hz.load(std::memory_order_relaxed); }
bool display_hz_overridden() { return g_env_hz >= 0; }
static std::atomic<int> g_display_hz_lo{0}, g_display_hz_hi{0}, g_requested_hz{-1};
static void note_display_hz_seen(int hz) {
    if (hz <= 0) return;
    int lo = g_display_hz_lo.load(std::memory_order_relaxed);
    while ((lo <= 0 || hz < lo) && !g_display_hz_lo.compare_exchange_weak(lo, hz)) {}
    int hi = g_display_hz_hi.load(std::memory_order_relaxed);
    while (hz > hi && !g_display_hz_hi.compare_exchange_weak(hi, hz)) {}
}
void display_hz_seen(int* lo, int* hi) {
    *lo = g_display_hz_lo.load(std::memory_order_relaxed);
    *hi = g_display_hz_hi.load(std::memory_order_relaxed);
}
void reset_display_hz_seen() {
    const int hz = g_display_hz.load(std::memory_order_relaxed);
    g_display_hz_lo = hz > 0 ? hz : 0;
    g_display_hz_hi = hz > 0 ? hz : 0;
}
void set_requested_display_hz(int hz) { g_requested_hz.store(hz, std::memory_order_relaxed); }
int requested_display_hz() { return g_requested_hz.load(std::memory_order_relaxed); }
bool present_vsync() { return g_present_vsync.load(std::memory_order_relaxed); }
int output_fps() { return pacing::cap_fps(fps(), display_hz(), present_vsync()); }
static void note_display_40();
void set_display_hz(int hz) {
    if (hz < 0) hz = 0;
    note_display_hz_seen(hz);
    const int old = g_display_hz.exchange(hz);
    if (old == hz) return;
    if (g_env_hz >= 0) LOG("[interp] display refresh rate %d Hz (WWHD_DISPLAY_HZ=%d is used)", hz, g_env_hz);
    else LOG("[interp] display refresh rate %d Hz: frame interpolation draws up to %d fps", hz, pacing::cap_fps(240, hz, present_vsync()));
    note_display_40();
}
void set_present_vsync(bool on) {
    if (g_present_vsync.exchange(on) != on) LOG("[interp] presentation %s the display's vsync", on ? "waits for" : "does not wait for");
}
// 40 fps frame interpolation runs (3 steps over 4 frames: pacing::is_fps40)
bool fps40() { return interp_on() && pacing::is_fps40(output_fps()); }
// in-between frames per logic step of the current mode (true 60: its one half pass; 40 fps: at most
// one, in one step of three)
int in_between() { return interp_on() ? (fps40() ? 1 : output_fps() / 30 - 1) : true60::enabled() ? 1 : 0; }
// frames drawn per logic step: 1 (30 fps), 2 (60 fps, true 60), 4 (120 fps), 8 (240 fps); at 40 fps
// the most a step has (2; 4 frames per 3 steps: frame_ns, vsync_granule_ticks)
int frames_per_step() { return in_between() + 1; }
// the drawn frame interval: two 59.94 Hz vsyncs over the frames of a step (16.68 ms at 60 fps), 1.5
// vsyncs at 40 fps (25.03 ms)
int64_t frame_ns() { return fps40() ? pacing::frame_40(2 * 16'683'333LL) : 16'683'333LL * 2 / std::max(1, frames_per_step()); }
// the virtual vsync's ticks (quarter 59.94 Hz vsyncs) per drawn frame: 4 (30/60 fps, true 60), 6 (40),
// 2 (120), 1 (240)
uint32_t vsync_granule_ticks() { return fps40() ? 6 : interp_on() ? uint32_t(8 / std::max(2, frames_per_step())) : 4; }
static const char* fps_name(int f) { return f == 240 ? "240 fps" : f == 120 ? "120 fps" : f == 40 ? "40 fps" : "60 fps"; }
// 40 fps looks even only on a 40, 80 or 120 Hz display (pacing::even_on_display); elsewhere the
// frames are shown for 1 and 2 refreshes in turn. They are still drawn (the game logic is the same);
// the log and the settings say so. Logged once per display rate while 40 fps is chosen.
static void note_display_40() {
    static int logged = -1;
    const int hz = display_hz();
    if (!interp_on() || !pacing::is_fps40(fps()) || hz <= 0 || pacing::even_on_display(40, hz)) return;
    if (logged == hz) return;
    logged = hz;
    LOG("[interp] 40 fps needs a 40, 80 or 120 Hz display; this one runs at %d Hz, so the frames are shown unevenly "
        "(for 1 and 2 refreshes in turn). 30 or 60 fps are even here.", hz);
}
void set_enabled(bool v) {
    if (v) true60::set_enabled(false);
    g_on = v;
    LOG("[interp] frame interpolation %s", v ? (fps() == 240 ? "on (240 fps)" : fps() == 120 ? "on (120 fps)" : fps() == 40 ? "on (40 fps)" : "on (60 fps)") : "off");
    note_display_40();
}
void set_fps(int f) {
    f = valid_fps(f);
    if (g_fps.exchange(f) != f && interp_on()) LOG("[interp] frame interpolation at %s", fps_name(f));
    note_display_40();
}
// the 60 fps mode: 0 off, 1 frame interpolation (at fps()), 2 true 60 (game logic at 60 steps per second)
int mode() { return true60::selected() ? 2 : g_on.load(std::memory_order_relaxed) ? 1 : 0; }
void set_mode(int m) {
    g_on = false;
    true60::set_enabled(false);
    if (m == 1) set_enabled(true);
    if (m == 2) true60::set_enabled(true);
}
// menus and the 6 key: frame interpolation at f on, or off if it is on at f already
void toggle_fps(int f) {
    if (mode() == 1 && fps() == valid_fps(f)) {
        set_mode(0);
        return;
    }
    set_fps(f);
    set_mode(1);
}
// the frame rate shown in titles and the overlay: "30 fps", "60 fps", "120 fps", "240 fps", "true 60"
// (with the rate capped to the display: "240 fps (120 shown)")
const char* mode_name() {
    if (mode() != 1) return mode() == 2 ? "true 60" : "30 fps";
    const int f = fps(), out = output_fps();
    if (out < f) return f == 240 ? (out == 120 ? "240 fps (120 shown)" : "240 fps (60 shown)") : "120 fps (60 shown)";
    return fps_name(f);
}

// GX2SetSwapInterval: N+1 paints per logic step. At 60 fps the interval is halved; at 120/240 fps
// the virtual vsync itself ticks 2/4 times as fast (vsync_granule_ticks(), gx2_core.cpp) and the interval is
// halved in those ticks, so every mode keeps the formula (and 30/60 fps their exact timing).
uint32_t effective_swap_interval(uint32_t game) { return enabled() ? std::max<uint32_t>(1, game / 2) : game; }
// a logic step on the virtual vsync, in ticks: the frames of a step at their swap interval (8 for the
// game's swap interval of 2; at 40 fps 8 as well, not frames x granule)
uint64_t step_ticks(uint32_t game) {
    return fps40() ? 8ull * effective_swap_interval(game) : uint64_t(frames_per_step()) * effective_swap_interval(game) * vsync_granule_ticks();
}

namespace {
constexpr uint32_t kEye = 0xDC, kCenter = 0xE8, kUp = 0xF4, kFovy = 0xD4, kBank = 0x100;
// the camera process's dCamera_c (+0x248): letterbox height mTrimHeight +0x5FC (eased by CalcTrimSize
// 024FF8A0 in the camera's execute, used as the scissor by camera_draw), mTrimSize +0x600, the
// cinema-scope height of its setup (mCamSetup +0x73C, +0x6C)
constexpr uint32_t kCamBody = 0x248, kTrimHeight = 0x5FC, kTrimSize = 0x600, kCinemaTrim = 0x73C + 0x6C;

using camera::CamState;

CamState read_cam(uint32_t cam) {
    CamState s;
    for (int i = 0; i < 3; i++) {
        s.eye[i] = (float)ldf32(cam + kEye + 4 * i);
        s.center[i] = (float)ldf32(cam + kCenter + 4 * i);
        s.up[i] = (float)ldf32(cam + kUp + 4 * i);
    }
    s.fovy = (float)ldf32(cam + kFovy);
    s.bank = (int16_t)ld16(cam + kBank);
    return s;
}

void write_cam(uint32_t cam, const CamState& s) {
    for (int i = 0; i < 3; i++) {
        stf32(cam + kEye + 4 * i, s.eye[i]);
        stf32(cam + kCenter + 4 * i, s.center[i]);
        stf32(cam + kUp + 4 * i, s.up[i]);
    }
    stf32(cam + kFovy, s.fovy);
    st16(cam + kBank, (uint16_t)s.bank);
}

// The blend fraction t of a pass: 1/(N+1) on the logic pass, (k+1)/(N+1) on blended hold pass k;
// 1 = exact. The blends (interp_pacing.h) keep the original halfway arithmetic at t = 1/2, so 60 fps
// draws bit-identical frames to the halfway-only code (0.5f * (a + b), nlerp, integer halving).
using pacing::lerp_f;
using pacing::lerp_s16;

// Blended camera between the last exact step (a) and the new one (b).
// - Snaps (re-centring behind Link, doors, mode changes, cutscene cuts) are not blended: a step is a
//   snap when it is far larger than the camera's recent motion, or larger than an absolute limit.
//   Decided once per step, on the logic pass (cam_snap updates the camera's recent motion, kept per
//   camera process: Prev::last_step); the step's blended hold passes reuse the decision.
// - Orbits around the look-at point blend the eye's direction and distance separately, so the
//   blended eye stays on the arc instead of cutting the corner towards the target.
bool step_is_cut(const CamState& a, const CamState& b, float& last_step) {
    static const float kCut = getenv("WWHD_INTERP_CUT") ? (float)atof(getenv("WWHD_INTERP_CUT")) : 800.0f;
    return camera::cam_snap(a, b, last_step, kCut);
}
// the 10 % distance rule (interp_camera.h): debug WWHD_INTERP_CAM_DIST_RULE=0 off, 2 on every step
int dist_rule_mode() {
    static const int m = getenv("WWHD_INTERP_CAM_DIST_RULE") ? atoi(getenv("WWHD_INTERP_CAM_DIST_RULE")) : camera::kDistCollision;
    return m;
}
// what the camera's logic did this step (bumpCheck hook), per camera process
struct CamStep {
    uint32_t cam = 0;
    uint64_t step = ~0ull;
    camera::StepInfo info;
};
CamStep g_cam_step[4];
camera::StepInfo step_info(uint32_t cam, uint64_t step) {
    for (const auto& s : g_cam_step)
        if (s.cam == cam && s.step == step) return s.info;
    return {};
}

std::atomic<uint64_t> g_executed_steps{0};
uint64_t g_logic_steps = 0; // full logic steps (all passes without a 60 fps mode)
uint64_t g_passes = 0;      // every pass of the per-frame function
bool g_hold = false;        // hold pass: draw only, no logic
bool g_cam_blended = false; // camera_draw is drawing the blended camera
uint32_t g_drawing_cam = 0; // the camera process camera_draw is drawing (zoom_ease)
// exact culling on blended logic passes (exact_cull, below camera_draw)
bool g_cam_prerun = false;          // camera_draw runs with the exact camera for the culling view
uint64_t g_view_pass = ~0ull;       // the pass whose camera_draw set j3dSys's view (view_drawn_this_pass)
uint64_t g_prerun_pass = ~0ull;     // the pass whose camera_draw had that run
uint32_t g_exact_view[12];          // j3dSys's view matrix (104B45F8) of the exact camera, as drawn at 30 fps
bool g_logic_pass = false;  // logic pass with interpolation on: camera drawn blended
bool g_blend_draw = false;  // inside the loop body of a blended pass (logic pass or blended hold pass)
bool g_hold_next = false;   // the next pass is a hold pass
bool g_hold_frame = false;  // inside the per-frame function on a hold pass
bool g_in_frame = false;  // inside the per-frame function (pass_t() is the current pass's)
// The step's pass structure: in-between frames planned for this step (N; fewer when paced
// interpolation sees that they do not fit, 1 on an exact step) and the current pass (0 logic pass,
// k = hold pass k). Hold pass N is the record pass (exact, recorded for the next step).
int g_step_n = 1;
int g_phase = 0;
uint64_t g_record_passes = 0;  // counts record passes (record generation of the step-stamped histories)
// paced interpolation: a logic pass that does not follow a record pass draws its step exactly.
// The previous states the blended frames blend from (camera, model joints, effects) are recorded on
// the record pass; without one they are steps old (camera trailing behind Link, models jumping,
// a head blended from another step than its body).
bool g_exact_step = false;
// this pass is a blended hold pass (no logic, drawn at pass_t())
bool blended_hold() { return g_hold && g_phase < g_step_n; }
// last camera state that was drawn normally, per camera process; the snap decision of the step
// (slots: camera::history_slot, interp_camera.h)
struct Prev {
    uint32_t cam = 0; CamState s{}; bool valid = false; bool snap = true; uint64_t snap_step = ~0ull;
    uint64_t used = 0;         // when the slot was last asked for (history_slot; 0 = unused)
    float last_step = 0;       // the camera's movement over its previous step (cam_snap)
    bool dist_rule = true;     // the step's 10 % distance rule applies (decided with the snap)
    float trim = 0;            // letterbox height (dCamera_c mTrimHeight) at the exact step
    CamState drawn{};          // the camera as camera_draw drew it on pass drawn_pass (blended or exact)
    uint64_t drawn_pass = ~0ull;
    uint32_t projection[16]{}; // raw projection-view words before true60's actor rollback
    uint64_t projection_pass = ~0ull;
    camera::Shake shake;       // the shake offset the exact step was drawn with (interp_camera.h)
    fade::FloatsGate<1> zoom;  // the HD projection zoom camera_draw eases (zoom_ease)
};
constexpr int kCamSlots = 4;
Prev g_prev[kCamSlots];
uint64_t g_prev_clock = 0;

// The shake offset the camera's store (024F8430) added to the drawn camera (interp_camera.h Shake):
// dCamera_c (camera +0x248) mCenter +0x10, mEye +0x1C, mCenterShake +0x56C, mEyeShake +0x578,
// mFovYShake +0x584, mBankShake +0x588. The store adds it unless a demo camera or event flag 1 draws
// the camera: it was added when the drawn eye and centre are the body's plus the offset.
// debug: WWHD_INTERP_CAM_SHAKE=0 blends the shaken cameras as before
camera::Shake read_shake(uint32_t cam) {
    static const bool off = getenv("WWHD_INTERP_CAM_SHAKE") && !atoi(getenv("WWHD_INTERP_CAM_SHAKE"));
    camera::Shake k;
    if (off) return k;
    const uint32_t body = cam + kCamBody;
    bool any = false;
    for (int i = 0; i < 3; i++) {
        k.eye[i] = (float)ldf32(body + 0x578 + 4 * i);
        k.center[i] = (float)ldf32(body + 0x56C + 4 * i);
        any |= k.eye[i] != 0.0f || k.center[i] != 0.0f;
    }
    k.fovy = (float)ldf32(body + 0x584);
    k.bank = (int16_t)ld16(body + 0x588);
    if (!any && k.fovy == 0.0f && k.bank == 0) return k;
    bool added = true;
    for (int i = 0; i < 3; i++) {
        const float e = (float)ldf32(body + 0x1C + 4 * i) + k.eye[i], c = (float)ldf32(body + 0x10 + 4 * i) + k.center[i];
        added &= e == (float)ldf32(cam + kEye + 4 * i) && c == (float)ldf32(cam + kCenter + 4 * i);
    }
    k.on = added;
    return k;
}

void capture_projection(Prev& p) {
    if (!enabled()) return;
    memcpy(p.projection, ppc_ptr(p.cam + 0x1A4), sizeof p.projection);
    p.projection_pass = g_passes;
}

// debug: WWHD_INTERP_CAM_SLOT_TRACE=n logs n cameras taking a history slot (and the camera that had it)
Prev* prev_for(uint32_t cam) {
    static int trace = getenv("WWHD_INTERP_CAM_SLOT_TRACE") ? atoi(getenv("WWHD_INTERP_CAM_SLOT_TRACE")) : 0;
    bool known = trace <= 0;
    uint32_t had = 0;
    if (!known) {
        uint64_t oldest = ~0ull;
        for (const auto& q : g_prev) {
            known |= q.cam == cam;
            if (q.used < oldest) oldest = q.used, had = q.cam;
        }
    }
    Prev& p = camera::history_slot(g_prev, cam, g_prev_clock);
    if (!known) {
        trace--;
        LOG("[interp] camera %08X takes a history slot (it had camera %08X)", cam, had);
    }
    return &p;
}
}  // namespace

// 40 fps: the logic pass's fraction (pacing::Fractions40), set by plan_step_40 (g_step40: this
// step was planned at 40 fps)
bool g_step40 = false;
float g_t40 = 1.0f;
pacing::Fractions40 g_frac40;
// blend fraction of this pass: 1/(N+1) on a logic pass, (k+1)/(N+1) on blended hold pass k, 1 on
// the record pass, an exact step and without interpolation (40 fps logic passes: Fractions40)
float pass_t() {
    if (!enabled()) return 1.0f;
    if (g_step40 && !g_hold) return g_t40;
    return pacing::pass_fraction(g_hold ? g_phase : 0, g_step_n, g_exact_step);
}
// At 40 fps the logic pass of a step without an in-between frame also records its step (g_record_logic):
// - after a record pass it is a blended record pass: drawn blended (pass_t()), and what it draws is
//   recorded as it is at the step (exact) for the next step's frames (record_blended());
// - an exact step (no record pass before it) is drawn exactly and recorded like a record pass.
bool g_record_logic = false;
// the step's exact state is drawn and recorded (the "before" of the next step's blended frames): the
// record pass, or an exact record logic pass at 40 fps
bool record_pass() { return (g_hold && g_phase >= g_step_n) || (g_record_logic && g_exact_step); }
bool view_drawn_this_pass() { return g_view_pass == g_passes; }  // j3dSys's view is this pass's camera's
// a blended record pass (40 fps): drawn blended, its step recorded for the next step's frames. Its
// records are stamped hold_pass_count() + 1 (record_stamp): the count goes up when the next pass
// starts (g_record_pending), so that this pass's blends still see the records from before it.
bool record_blended() { return g_record_logic && !g_exact_step; }
bool g_record_pending = false;
uint64_t record_stamp() { return g_record_passes + (record_blended() ? 1 : 0); }
// Gates that step a value once per logic step and leave the pass's blend of it in guest memory
// (fade::fader_call, bytes_call: their next call takes it back) rely on the step's last pass, the
// record pass, showing it exactly before the next step's logic runs. After a blended record pass the
// next pass is a logic pass: the step's exact values go back into guest memory when it starts
// (after the blended pass has been painted), so the game's logic never sees a blended one.
// exact_after_pass(ea, value, size): put back `size` (1 or 4) bytes at ea; only on blended record passes.
struct ExactWrite { uint32_t ea, value; int size; };
std::vector<ExactWrite> g_exact_writes;
void exact_after_pass(uint32_t ea, uint32_t value, int size) {
    if (record_blended()) g_exact_writes.push_back({ea, value, size});
}
static void put_back_exact() {
    for (auto it = g_exact_writes.rbegin(); it != g_exact_writes.rend(); ++it) {
        if (it->size == 1) st8(it->ea, (uint8_t)it->value);
        else st32(it->ea, it->value);
    }
    g_exact_writes.clear();
}

// A sight point follows the camera's draw fraction, including camera cuts. A
// blend can also snap internally (a >45 degree turn or abrupt collision zoom).
float reticle_t() {
    if (!g_blend_draw) return 1.0f;
    const uint32_t view = ld32(GD(0x1046F0B0) + 0x5FA4); // mDoLib_project's active view
    for (const auto& p : g_prev) {
        if (!p.cam || p.cam != view || p.drawn_pass != g_passes) continue;
        if (p.snap_step == g_logic_steps && p.snap) return 1.0f;
        CamState cur = read_cam(p.cam);
        bool moved = false, exact = true;
        for (int i = 0; i < 3; ++i) {
            moved |= p.s.eye[i] != cur.eye[i] || p.s.center[i] != cur.center[i];
            exact &= p.drawn.eye[i] == cur.eye[i] && p.drawn.center[i] == cur.center[i];
        }
        if (moved && exact) return 1.0f;
    }
    return pass_t();
}

bool reticle_projection(uint32_t& address, uint32_t* words) {
    const uint32_t view = ld32(GD(0x1046F0B0) + 0x5FA4);
    for (const auto& p : g_prev) {
        if (p.cam != view || p.projection_pass != g_passes) continue;
        address = view + 0x1A4;
        memcpy(words, p.projection, sizeof p.projection);
        return true;
    }
    return false;
}

}  // namespace interp

// debug: WWHD_INTERP_CAM_TRACE=n logs n camera draws: logic step, pass fraction, eye and center as drawn
static void cam_trace(uint32_t cam, const char* what) {
    static int left = getenv("WWHD_INTERP_CAM_TRACE") ? atoi(getenv("WWHD_INTERP_CAM_TRACE")) : 0;
    if (left <= 0 || !interp::enabled()) return;
    left--;
    interp::CamState s = interp::read_cam(cam);
    LOG("[interp] camera %08X step %llu t %.3f %-7s eye %.3f %.3f %.3f center %.3f %.3f %.3f", cam, (unsigned long long)interp::g_logic_steps,
        interp::pass_t(), what, s.eye[0], s.eye[1], s.eye[2], s.center[0], s.center[1], s.center[2]);
}

namespace editor { void camera_drawn(uint32_t camera); }  // editor/editor_game.cpp: the editor's overlays project with it
static void camera_draw(Cpu* c);
// camera_draw(camera_process_class*)
static std::atomic<uint32_t> g_last_camera{0};
uint32_t interp::last_camera() { return g_last_camera.load(std::memory_order_relaxed); }
extern "C" void hook_024FFC40(Cpu* c) {
    const uint32_t cam = c->r[3];
    g_last_camera.store(cam, std::memory_order_relaxed);
    camera_draw(c);
    editor::camera_drawn(cam);
}
static void camera_draw_body(Cpu* c);
// Exact culling. Which actors a pass draws decides game logic: a drawn actor's draw takes random
// numbers (the candle and torch flicker of plightcol_plus, one per light per actor), and other draws
// that take them later in the pass (the HUD meter) get other values when that count differs. At 30 fps
// the step is drawn with the exact camera; the logic pass of frame interpolation draws with the
// blended one, so while the camera turned, actors at the screen's edge were drawn or culled
// differently and the game's random sequence left the 30 fps one within seconds (a 120 fps run of the
// Windfall route at step 285). On blended logic passes camera_draw now runs once with the exact camera
// first (its focus lines and the sound listener left out, the zoom ease not advanced twice) and keeps
// j3dSys's view matrix; the culling checks of the pass (fopAcM_cullingCheck 025D6CE8,
// fopAcM_checkCullingBox 025D6B70, J3DUClipper::clip 02838148 on j3dSys's view) see that matrix, so the pass draws the actors the 30 fps frame
// draws, while everything is drawn with the blended camera. Actors drawn before the camera process
// cull with the view the previous pass left: exact after a record pass; after a 40 fps blended record
// pass its pre-run's. (Their second test, the shadow cascades: shadow_cascades_launch.) Passes without
// logic cull with the camera they draw with; what their draws decide does not reach the game's logic
// (fopAc_Draw keeps actor_condition).
// debug: WWHD_INTERP_EXACT_CULL=0 culls with the blended camera as before
std::vector<uint8_t> g_exact_cam_img, g_cam_blended_img;
// j3dSys words camera_draw writes (view matrix +0x38..+0x67, projection words +0x14C +0x160 +0x174 +0x178)
constexpr uint32_t kJ3dCamWords[] = {0x38, 0x3C, 0x40, 0x44, 0x48, 0x4C, 0x50, 0x54, 0x58, 0x5C, 0x60, 0x64, 0x14C, 0x160, 0x174, 0x178};
uint32_t g_exact_j3d_words[16];
uint32_t g_exact_cam_addr = 0;
// The step's camera is drawn exactly by its record pass, the step's last pass, at 30/60/120 fps. Two
// cases leave the camera's last draw before the next step's logic blended: a 40 fps blended record
// pass (no exact pass in its step), and a step whose later passes do not draw the camera at all (the
// fade-out of a scene change: the logic pass drew it blended, the record pass draws no scene). What a
// blended camera_draw wrote (the camera process: view and projection matrices; j3dSys's view; the
// play window's scissor) then stayed in place for the next step's logic: the camera's execute reads
// the scissor (the lock-on camera's sight angle, 40 fps Z-target) and the process kept the blended
// matrices (the camera's process hash at the scene change, 60 fps). The words a blended draw changed
// go back to the exact pre-run's before the next step's logic, unless a later pass of the step drew
// the camera exactly. debug: WWHD_INTERP_CAM_EXACT_BACK=0 does it only after a 40 fps blended record
// pass (as before)
bool g_cam_exact_pending = false;  // the camera's last draw was blended: exact again before the next logic
uint64_t g_exact_img_step = ~0ull;  // the logic step whose pre-run g_exact_cam_img holds
static bool cam_exact_back_on() {
    static const bool off = getenv("WWHD_INTERP_CAM_EXACT_BACK") && !atoi(getenv("WWHD_INTERP_CAM_EXACT_BACK"));
    return !off;
}
// The play window (dComIfGp's dDlst_window_c 10474B7C, 0x2C bytes: view port, scissor +0x18..+0x27,
// camera id): camera_draw sets its scissor from the letterbox height (dCamera_c mTrimHeight), and the
// camera's next execute reads it; it goes back with the camera (above).
// debug: WWHD_INTERP_WINDOW_EXACT=0 leaves the blended scissor in place
static uint32_t window_ea() { return GD(0x10474B7C); }
constexpr int kWindowWords = 0x2C / 4;
uint32_t g_exact_window[kWindowWords], g_blended_window[kWindowWords];
static bool window_exact_on() {
    static const bool off = getenv("WWHD_INTERP_WINDOW_EXACT") && !atoi(getenv("WWHD_INTERP_WINDOW_EXACT"));
    return !off;
}
namespace interp {
bool exact_cull_on() {
    static const bool off = getenv("WWHD_INTERP_EXACT_CULL") && !atoi(getenv("WWHD_INTERP_EXACT_CULL"));
    return !off;
}
void exact_prerun(Cpu* c) {
    if (!exact_cull_on() || !interp_on() || true60::enabled() || g_prerun_pass == g_passes) return;
    const Cpu saved = *c;
    g_cam_prerun = true;
    g_cam_blended = true;  // (the sound engine keeps the record pass's camera)
    f_024FFC40_orig(c);
    memcpy(g_exact_view, ppc_ptr(GD(0x104B45F8)), sizeof g_exact_view);
    {   // (the exact camera's process for the shadow cascades of a 40 fps blended record pass)
        g_exact_cam_addr = saved.r[3];
        // (the process's size from its profile, +0x10 -> +0x10, as WWHD_PROC_DIGEST takes it)
        const uint32_t prof = ld32(g_exact_cam_addr + 0x10);
        uint32_t size = prof >= 0x10000000 && prof < 0x50000000 ? ld32(prof + 0x10) : 0;
        if (size < 0x100 || size > 0x8000) size = 0;
        g_exact_cam_img.assign((uint8_t*)ppc_ptr(g_exact_cam_addr), (uint8_t*)ppc_ptr(g_exact_cam_addr) + size);
        for (int i = 0; i < 16; i++) g_exact_j3d_words[i] = ld32(GD(0x104B45C0) + kJ3dCamWords[i]);
        for (int i = 0; i < kWindowWords; i++) g_exact_window[i] = ld32(window_ea() + 4 * i);
        g_exact_img_step = g_logic_steps;
    }
    g_cam_prerun = false;
    g_cam_blended = false;
    g_prerun_pass = g_passes;
    *c = saved;
}
// the culling view of this pass: the exact camera's on a blended logic pass
struct ExactCullView {
    uint32_t saved[12];
    bool on = false;
    ExactCullView() {
        // debug: WWHD_INTERP_CULL_TRACE=n logs the first n culling checks of logic passes
        static int trace = getenv("WWHD_INTERP_CULL_TRACE") ? atoi(getenv("WWHD_INTERP_CULL_TRACE")) : 0;
        if (trace > 0 && !g_hold && enabled()) {
            trace--;
            LOG("[interp] cull step %llu pass %llu: logic pass %d, exact view %s", (unsigned long long)g_logic_steps, (unsigned long long)g_passes,
                g_logic_pass, g_prerun_pass == g_passes ? "of this pass" : g_prerun_pass + 1 == g_passes ? "of the pass before" : "in memory");
        }
        // (actors drawn before the camera process cull with the view the previous pass left: exact
        // after a record pass, as at 30 fps; after a 40 fps blended record pass, its exact view)
        if (!g_logic_pass || g_hold || (g_prerun_pass != g_passes && g_prerun_pass + 1 != g_passes)) return;
        memcpy(saved, ppc_ptr(GD(0x104B45F8)), sizeof saved);
        memcpy(ppc_ptr(GD(0x104B45F8)), g_exact_view, sizeof g_exact_view);
        on = true;
    }
    ~ExactCullView() { if (on) memcpy(ppc_ptr(GD(0x104B45F8)), saved, sizeof saved); }
};
// The culling frustum (mDoLib_clipper: J3DUClipper mClipper 1048CFF0, mSystemFar 1048D04C, mFovyRate
// 1048D050) is set up by the camera's execute (view_setup 024F8068) with the step's exact fovy, while
// the blended frames are drawn with the blended fovy: during a zoom (telescope, events) things at the
// edge of the screen popped in and out. Passes without logic cull with a frustum set up from the fovy
// they draw with (mDoLib_clipper::setup 025F11AC), the exact one is back at the start of the next pass;
// the logic pass keeps the exact frustum for its culling (the 30 fps game's) and its edge draws
// (edge_draw) test with the blended one. debug: WWHD_INTERP_CLIPPER=0 keeps the exact frustum
constexpr uint32_t kClipperBytes = 0x64;
uint8_t g_clipper_exact[kClipperBytes], g_clipper_blended[kClipperBytes];
uint64_t g_clipper_pass = ~0ull;  // the pass that has a blended frustum
bool g_clipper_in_memory = false; // the blended frustum is in guest memory (a pass without logic)
bool clipper_on() {
    static const bool off = getenv("WWHD_INTERP_CLIPPER") && !atoi(getenv("WWHD_INTERP_CLIPPER"));
    return !off;
}
void clipper_put_back() {
    if (g_clipper_in_memory) memcpy(ppc_ptr(GD(0x1048CFF0)), g_clipper_exact, kClipperBytes);
    g_clipper_in_memory = false;
}
// with the blended frustum (an edge draw's test on a logic pass)
struct BlendedClipper {
    bool on = false;
    BlendedClipper() {
        if (g_clipper_pass != g_passes || g_clipper_in_memory) return;
        memcpy(ppc_ptr(GD(0x1048CFF0)), g_clipper_blended, kClipperBytes);
        on = true;
    }
    ~BlendedClipper() { if (on) memcpy(ppc_ptr(GD(0x1048CFF0)), g_clipper_exact, kClipperBytes); }
};
}  // namespace interp
extern "C" void f_025F11AC(Cpu* c);  // mDoLib_clipper::setup(fovy, aspect, near, far)
namespace interp {
// camera_draw drew camera `cam` blended with fovy `fovy` (its exact one `exact`)
void clipper_blend(Cpu* c, uint32_t cam, float fovy, float exact) {
    if (!clipper_on() || !(fovy == fovy) || fovy == exact || g_clipper_pass == g_passes) return;
    const uint32_t a = GD(0x1048CFF0);
    memcpy(g_clipper_exact, ppc_ptr(a), kClipperBytes);
    const float far_plane = (float)ldf32(GD(0x1048D04C));  // (the far view_setup chose: the stage's cull point or the camera's)
    const Cpu saved = *c;
    c->f[1].ps0 = fovy;
    c->f[2].ps0 = (float)ldf32(cam + 0xD8);
    c->f[3].ps0 = (float)ldf32(cam + 0xCC);
    c->f[4].ps0 = far_plane;
    f_025F11AC(c);
    *c = saved;
    memcpy(g_clipper_blended, ppc_ptr(a), kClipperBytes);
    g_clipper_pass = g_passes;
    g_clipper_in_memory = g_hold;
    if (!g_hold) memcpy(ppc_ptr(a), g_clipper_exact, kClipperBytes);
    static int trace = getenv("WWHD_INTERP_CAM_TRACE") ? atoi(getenv("WWHD_INTERP_CAM_TRACE")) : 0;
    if (trace > 0) {
        trace--;
        LOG("[interp] culling frustum step %llu t %.3f: fovy %.3f (exact %.3f), %s", (unsigned long long)g_logic_steps, pass_t(), fovy, exact,
            g_hold ? "this pass culls with it" : "edge draws test with it");
    }
}
}  // namespace interp
// Light state of drawn things (dKy_tevstr_c, 0x1C8 bytes: actors +0x110, other processes and models
// their own). settingTevStruct (025626A4, from the draws) eases it towards the lights around it and
// counts its own timers on every call: with frame interpolation 2x/4x/8x per logic step. Its light
// handling then ran ahead (a light reaching an object a step early), and with it the candle flicker's
// random numbers, which are game logic (the sea route: from step 309). On passes without logic each
// tevstr is saved before its first settingTevStruct of the pass and put back at the start of the next
// pass (after it has been painted), so it advances once per step, on the logic pass, as at 30 fps.
// debug: WWHD_INTERP_TEV_KEEP=0 lets the draws of passes without logic keep it
namespace interp {
struct TevKeep { uint32_t addr; uint8_t bytes[0x1C8]; };
std::vector<TevKeep> g_tev_kept;
void tevstr_put_back() {
    for (auto it = g_tev_kept.rbegin(); it != g_tev_kept.rend(); ++it) memcpy(ppc_ptr(it->addr), it->bytes, sizeof it->bytes);
    g_tev_kept.clear();
}
}  // namespace interp
void interp_tevstr_keep(uint32_t tevstr) {
    using namespace interp;
    static const bool off = getenv("WWHD_INTERP_TEV_KEEP") && !atoi(getenv("WWHD_INTERP_TEV_KEEP"));
    if (off || !g_hold || !enabled() || true60::enabled() || tevstr < 0x10000000 || tevstr >= 0x50000000) return;
    for (const auto& k : g_tev_kept)
        if (k.addr == tevstr) return;
    if (g_tev_kept.size() >= 4096) return;
    g_tev_kept.emplace_back();
    g_tev_kept.back().addr = tevstr;
    memcpy(g_tev_kept.back().bytes, ppc_ptr(tevstr), sizeof g_tev_kept.back().bytes);
}
void interp_exact_cull(Cpu* c, void (*orig)(Cpu*)) {
    // debug: WWHD_INTERP_CULL_ACTOR=hex logs that actor's culling checks on logic passes with hashes of the
    // view matrix, the clipper (f_op_actor_mng) and the shadow cascades
    static const uint32_t who = getenv("WWHD_INTERP_CULL_ACTOR") ? (uint32_t)strtoul(getenv("WWHD_INTERP_CULL_ACTOR"), nullptr, 16) : 0;
    const uint32_t actor = c->r[3];
    interp::ExactCullView v;
    if (who && actor == who && !interp::g_hold) {
        auto h = [](uint32_t a, uint32_t n) { uint64_t x = 1469598103934665603ull; for (uint32_t i = 0; i < n; i++) x = (x ^ ld8(a + i)) * 1099511628211ull; return (unsigned long long)x; };
        uint32_t casc = 0, cn = 0;
        const uint32_t root = ld32(GD(0x101F95D0));
        if (root) { const uint32_t pp = ld32(root + 0x1024); const uint32_t sv = pp ? ld32(pp) : 0; if (sv) { cn = ld32(sv + 0x610 + 0x254); casc = ld32(sv + 0x610 + 0x2C0); } }
        LOG("[interp] cull %08X step %llu: view %016llx swapped %d, clipper %016llx, cascades %u %016llx", actor, (unsigned long long)interp::g_logic_steps,
            h(GD(0x104B45F8), 0x30), v.on, h(GD(0x1048CFF0), 0x80), cn, cn <= 8 && casc ? h(casc, cn * 0x5B4) : 0ull);
    }
    orig(c);
}
namespace interp {
// The shadow cascades. Culling counts an actor as drawn when it is in the camera's view or in one of
// the shadow cascades, which a worker job (setup 027C93A8, from per-frame child 10) computes from the
// camera's view matrix and projection, reading them through pointers while the main thread already
// draws the next pass. The logic pass after a record pass then culled with cascades from whatever
// camera the next pass had written by then, and after a 40 fps blended record pass (no exact pass in
// the step) from the blended camera. On passes followed by a logic pass (the record pass, a 40 fps
// blended record pass) the job gets a copy of the step's exact view and projection taken when the
// pass starts it (the exact camera pre-run's on a blended pass), so the next logic pass culls with
// the cascades the 30 fps frame of the step had. Other passes keep the camera they draw with.
std::atomic<uint32_t> g_shadow_view_src{0}, g_shadow_proj_src{0};
std::atomic<bool> g_shadow_sub{false};
constexpr uint32_t kShadowView = 0x30, kShadowProj = 0x80;
void shadow_cascades_launch() {
    g_shadow_sub = false;
    if (!exact_cull_on() || !interp_on() || true60::enabled() || !(record_pass() || record_blended())) return;
    const uint32_t v = g_shadow_view_src, pr = g_shadow_proj_src, slot = mem::fixed_slot(mem::kFixShadowCam);
    if (!v || !pr) return;
    if (record_pass()) {  // drawn exactly: the camera as it is now
        memcpy(ppc_ptr(slot), ppc_ptr(v), kShadowView);
        memcpy(ppc_ptr(slot + kShadowView), ppc_ptr(pr), kShadowProj);
    } else {  // a blended pass: the exact camera of its pre-run
        const uint32_t lo = g_exact_cam_addr, hi = lo + (uint32_t)g_exact_cam_img.size();
        if (g_prerun_pass != g_passes || v < lo || v + kShadowView > hi || pr < lo || pr + kShadowProj > hi) {
            static bool logged = false;
            if (!logged) {
                logged = true;
                LOG("[interp] shadow cascades: camera %08X/%08X not in the camera process %08X..%08X: not taken from the exact camera", v, pr, lo, hi);
            }
            return;
        }
        memcpy(ppc_ptr(slot), g_exact_cam_img.data() + (v - lo), kShadowView);
        memcpy(ppc_ptr(slot + kShadowView), g_exact_cam_img.data() + (pr - lo), kShadowProj);
    }
    g_shadow_sub = true;
}
}  // namespace interp
// the cascade setup (worker thread): view r5, projection r6
extern "C" void f_027C93A8_orig(Cpu* c);
extern "C" void hook_027C93A8(Cpu* c) {
    using namespace interp;
    g_shadow_view_src = c->r[5];
    g_shadow_proj_src = c->r[6];
    if (g_shadow_sub) {
        const uint32_t slot = mem::fixed_slot(mem::kFixShadowCam);
        c->r[5] = slot;
        c->r[6] = slot + kShadowView;
    }
    f_027C93A8_orig(c);
}
// dCamera_c::Draw (024FFC18, end of camera_draw): the focus lines, drawn once per pass (not in the
// exact pre-run, which would draw them, and take their random numbers, a second time)
extern "C" void f_024FFC18_orig(Cpu* c);
extern "C" void hook_024FFC18(Cpu* c) {
    if (interp::g_cam_prerun) { c->r[3] = 1; return; }
    f_024FFC18_orig(c);
}
extern "C" void f_025D6CE8_orig(Cpu* c);
extern "C" void f_025D4654(Cpu* c);
// Actors the step's logic pass did not draw (culled with the exact camera, as the 30 fps frame culls
// them) but a later pass draws (its blended camera shows them; or the edge draws below): their draw
// leaves draw-side state in their memory (animation caches, line buffers, rope point counts) that the
// 30 fps game, which did not draw them, does not have. Their memory is saved before such a draw and
// put back at the start of the next pass (after the pass has been painted), except the links of their
// packets that a draw list still holds (as true60.cpp does for its half-pass previews).
namespace interp {
struct ActorKeep { uint32_t actor; std::vector<uint32_t> words; };
// the process manager's part of an actor (f_pc_base.h, f_op_actor.h): base_process_class (+0 .. +0xB8:
// state, layer, line, delete and priority tags), leafdraw_class (.. +0xC4: draw method and priority),
// fopAc_ac_c's actor_type, actor_tag, draw_tag (+0xDC: the draw list's link), sub_method, heap (.. +0xF8)
constexpr uint32_t kProcessTags = 0xF8;
std::vector<ActorKeep> g_actor_kept;
// The models such a draw calcs (J3DModel::calc 027F4D5C: the joint world matrices, model +0x2C ->
// block +0x10, block +0x2C count) live outside the actor, and its next execute may read them: the
// harbour boat (ikada) places its crane rope from joint 1's matrix (setRopePos). A boat the step's
// logic pass culled but the 120 fps record pass drew had its matrices calced a step early and its
// rope moved off the 30 fps one. Their matrices are saved before the calc and put back with the
// actor. debug: WWHD_INTERP_KEEP_MODELS=0 leaves them calced
uint32_t g_keep_drawing = 0;  // the kept actor whose draw is running
struct ModelKeep { uint32_t mtx; std::vector<uint32_t> words; };
std::vector<ModelKeep> g_model_kept;
void model_calc_before(uint32_t model) {
    static const bool off = getenv("WWHD_INTERP_KEEP_MODELS") && !atoi(getenv("WWHD_INTERP_KEEP_MODELS"));
    if (!g_keep_drawing || off) return;
    const uint32_t jnt = ld32(model + 0x2C), mtx = jnt ? ld32(jnt + 0x10) : 0, n = jnt ? ld16(jnt + 0x2C) : 0;
    if (mtx < 0x10000000 || mtx >= 0x50000000 || n == 0 || n > 1024 || g_model_kept.size() >= 512) return;
    for (const auto& k : g_model_kept)
        if (k.mtx == mtx) return;
    g_model_kept.push_back({mtx, std::vector<uint32_t>(12 * n)});
    memcpy(g_model_kept.back().words.data(), ppc_ptr(mtx), 48 * n);
}
void actor_keep(uint32_t actor) {
    for (const auto& k : g_actor_kept)
        if (k.actor == actor) return;
    const uint32_t prof = ld32(actor + 0x10);
    uint32_t size = prof >= 0x10000000 && prof < 0x50000000 ? ld32(prof + 0x10) : 0;
    if (size < 0x3AC || size > 0x20000 || g_actor_kept.size() >= 512) return;
    g_actor_kept.push_back({actor, std::vector<uint32_t>(size / 4)});
    memcpy(g_actor_kept.back().words.data(), ppc_ptr(actor), size & ~3u);
}
void actors_put_back() {
    for (auto it = g_actor_kept.rbegin(); it != g_actor_kept.rend(); ++it) {
        const uint32_t actor = it->actor, n = (uint32_t)it->words.size();
        uint32_t* cur = (uint32_t*)ppc_ptr(actor);
        auto entered = [&](uint32_t i) {  // J3DPacket in the actor: +0x94 its draw-list slot, +0x10 the next packet
            if (i * 4 < 0x94) return false;
            const uint32_t slot = __builtin_bswap32(cur[i]);
            if (slot < 0x10000000 || slot >= 0x50000000 || (slot & 3)) return false;
            const uint32_t pkt = actor + 4 * i - 0x94;
            uint32_t q = ld32(slot);
            for (int k = 0; q && k < 4096; k++) {
                if (q == pkt) return true;
                if (q < 0x10000000 || q >= 0x50000000 || (q & 3)) return false;
                q = ld32(q + 0x10);
            }
            return false;
        };
        std::vector<uint32_t> keep;
        for (uint32_t i = 0x94 / 4; i < n; i++)
            if (cur[i] != it->words[i] && entered(i)) { keep.push_back(i); keep.push_back(i - (0x94 - 0x10) / 4); }
        // (the process manager's part, +0 .. kProcessTags: its state and queue tags. Logic after the draw
        // in the same pass changes them: a scene change queues the room's actors for deletion (state,
        // layer, line, delete, priority and draw tags). Put back, the actor left the delete queue's view
        // of it (delete tag cleared, still linked in the queue) and the next pass's deletion crashed on
        // a null layer (fpcLy_DeletedMesg, 40/120 fps, a scene change at sea); its draw tag went back
        // into use. No draw writes them: left alone. debug: WWHD_INTERP_KEEP_PROCESS_TAGS=0 puts them
        // back too, as before)
        static const bool tags_too = getenv("WWHD_INTERP_KEEP_PROCESS_TAGS") && !atoi(getenv("WWHD_INTERP_KEEP_PROCESS_TAGS"));
        for (uint32_t i = tags_too ? 0 : kProcessTags / 4; i < n; i++)
            if (cur[i] != it->words[i] && std::find(keep.begin(), keep.end(), i) == keep.end()) cur[i] = it->words[i];
    }
    g_actor_kept.clear();
    for (auto it = g_model_kept.rbegin(); it != g_model_kept.rend(); ++it)
        memcpy(ppc_ptr(it->mtx), it->words.data(), 4 * it->words.size());
    g_model_kept.clear();
}
}  // namespace interp
// Edge of the screen on blended logic passes: an actor the exact camera culls (so the 30 fps frame does
// not draw it) but the blended camera of the frame shows would be missing from that one frame while
// the camera turns. It is drawn a second time with the pass's own camera, as on a pass without logic
// (g_hold for the call: its random numbers, light state, animations, per-draw state and culling
// result do not reach the game's logic; see the hold-pass hooks). debug: WWHD_INTERP_EDGE_DRAW=0
namespace interp {
void edge_draw(const Cpu& call, Cpu* c, uint32_t actor) {
    static const bool off = getenv("WWHD_INTERP_EDGE_DRAW") && !atoi(getenv("WWHD_INTERP_EDGE_DRAW"));
    if (off || g_hold || !g_logic_pass || !exact_cull_on() || (g_prerun_pass != g_passes && g_prerun_pass + 1 != g_passes)) return;
    if (!(ld32(actor + 0x2E4) & 0x04) || !(ld32(actor + 0x2E0) & 0x100)) return;  // drawn, or not by culling
    const Cpu result = *c;
    *c = call;
    bool culled;
    {
        BlendedClipper blended;
        f_025D6CE8_orig(c);  // (the check with the pass's own camera)
        culled = c->r[3] != 0;
    }
    if (!culled) {
        static int trace = getenv("WWHD_INTERP_CULL_TRACE") ? atoi(getenv("WWHD_INTERP_CULL_TRACE")) : 0;
        if (trace > 0) {
            trace--;
            LOG("[interp] step %llu: actor %08X culled by the exact camera, drawn with the pass's", (unsigned long long)g_logic_steps, actor);
        }
        const uint32_t status = ld32(actor + 0x2E0), condition = ld32(actor + 0x2E4);
        actor_keep(actor);  // (put back after the pass has been painted)
        *c = call;
        g_hold = true;
        f_025D4654(c);  // (through the hooks: as on a pass without logic)
        g_hold = false;
        st32(actor + 0x2E0, status);
        st32(actor + 0x2E4, condition);
    }
    *c = result;
}
}  // namespace interp
// fopAcM_checkCullingBox (025D6B70): the other culling check on j3dSys's view
extern "C" void f_025D6B70_orig(Cpu* c);
extern "C" void hook_025D6B70(Cpu* c) { interp_exact_cull(c, f_025D6B70_orig); }
// J3DUClipper::clip(clipper, view, pos, radius) (02838148) with j3dSys's view: draws that test their
// own visibility (bombs, before they set up their light; the particle system's simple emitters; the
// trees' clip state)
extern "C" void f_02838148_orig(Cpu* c);
extern "C" void hook_02838148(Cpu* c) {
    static const uint32_t view = GD(0x104B45F8);
    if (c->r[4] != view) { f_02838148_orig(c); return; }
    interp::ExactCullView v;
    f_02838148_orig(c);
}
static void camera_draw(Cpu* c) {
    const uint32_t saved = interp::g_drawing_cam;
    interp::g_drawing_cam = c->r[3];
    camera_draw_body(c);
    interp::g_drawing_cam = saved;
}
static void camera_draw_body(Cpu* c) {
    using namespace interp;
    uint32_t cam = c->r[3];
    mods::moon_jump_camera(cam);
    Prev* p = prev_for(cam);
    if (g_blend_draw && !true60::runs_60(cam)) {  // blended between the step drawn last (S) and the new one (S+1)
        if (!g_hold && p->valid) exact_prerun(c);  // (logic pass: the exact camera's culling view first)
        CamState cur = read_cam(cam);
        float trim_drawn = NAN;  // the exact letterbox height, when it was drawn blended
        if (p->valid) {
            if (!g_hold) {  // logic pass: the step's snap decision
                p->snap = step_is_cut(p->s, cur, p->last_step);
                p->snap_step = g_logic_steps;
                p->dist_rule = camera::distance_rule_applies(step_info(cam, g_logic_steps), dist_rule_mode());
                static int dist_trace = getenv("WWHD_INTERP_CAM_DIST_TRACE") ? atoi(getenv("WWHD_INTERP_CAM_DIST_TRACE")) : 0;
                if (dist_trace > 0 && !p->snap) {
                    const float la = camera::dist(p->s.eye, p->s.center), lb = camera::dist(cur.eye, cur.center);
                    if (camera::distance_jump(la, lb)) {
                        dist_trace--;
                        LOG("[interp] camera %08X step %llu: distance %.1f -> %.1f (%s)", cam, (unsigned long long)g_logic_steps, la, lb,
                            p->dist_rule ? "drawn exactly: collision pull-in or event camera" : "blended");
                    }
                }
            }
            if (p->snap_step == g_logic_steps && !p->snap) {
                // (a shaking camera: the base camera blended, the new step's shake on top)
                const camera::Shake shake = read_shake(cam);
                write_cam(cam, camera::blend_shaken(p->s, p->shake, cur, shake, pass_t(), p->dist_rule));
                static int shake_trace = getenv("WWHD_INTERP_CAM_TRACE") ? atoi(getenv("WWHD_INTERP_CAM_TRACE")) : 0;
                if (shake_trace > 0 && (shake.on || p->shake.on)) {
                    shake_trace--;
                    LOG("[interp] camera shake step %llu t %.3f: eye offset y %.2f -> %.2f, drawn eye y %.2f (exact %.2f)",
                        (unsigned long long)g_logic_steps, pass_t(), p->shake.on ? p->shake.eye[1] : 0.0f, shake.on ? shake.eye[1] : 0.0f,
                        (float)ldf32(cam + kEye + 4), cur.eye[1]);
                }
            }
            // letterbox bars (interp_camera.h trim_blends): between the steps' heights
            const uint32_t body = cam + kCamBody;
            const float trim = (float)ldf32(body + kTrimHeight);
            if (p->snap_step == g_logic_steps && !p->snap &&
                camera::trim_blends((int32_t)ld32(body + kTrimSize), p->trim, trim, (float)ldf32(body + kCinemaTrim)))
            {
                trim_drawn = trim;
                stf32(body + kTrimHeight, lerp_f(p->trim, trim, pass_t()));
                static int trim_trace = getenv("WWHD_INTERP_CAM_TRACE") ? atoi(getenv("WWHD_INTERP_CAM_TRACE")) : 0;
                if (trim_trace > 0) {
                    trim_trace--;
                    LOG("[interp] letterbox step %llu t %.3f: %.2f -> %.2f, drawn %.2f", (unsigned long long)g_logic_steps, pass_t(), p->trim, trim,
                        (float)ldf32(body + kTrimHeight));
                }
            }
        }
        p->drawn = read_cam(cam);
        p->drawn_pass = g_passes;
        // (also on a snap: the sound listener keeps following only the record pass's camera)
        g_cam_blended = p->valid;
        cam_trace(cam, "blended");
        f_024FFC40_orig(c);
        capture_projection(*p);
        g_cam_blended = false;
        g_view_pass = g_passes;
        if (p->valid && !true60::enabled()) clipper_blend(c, cam, (float)ldf32(cam + kFovy), cur.fovy);
        write_cam(cam, cur);
        if (trim_drawn == trim_drawn) stf32(cam + kCamBody + kTrimHeight, trim_drawn);  // (exact back)
        if (record_blended()) {  // 40 fps blended record pass: the step's camera for the next step's frames
            p->s = cur;
            p->shake = read_shake(cam);
            p->trim = (float)ldf32(cam + kCamBody + kTrimHeight);
            p->valid = true;
        }
        // (what this camera_draw wrote goes back to the step's exact pre-run's before the next step's
        // logic unless a later pass draws the camera exactly: g_cam_exact_pending)
        const bool back = cam_exact_back_on() ? g_exact_img_step == g_logic_steps : record_blended() && g_prerun_pass == g_passes;
        if (back && g_exact_cam_addr == cam && !g_exact_cam_img.empty()) {
            g_cam_exact_pending = true;
            g_cam_blended_img.assign((uint8_t*)ppc_ptr(cam), (uint8_t*)ppc_ptr(cam) + g_exact_cam_img.size());
            for (int i = 0; i < kWindowWords; i++) g_blended_window[i] = ld32(window_ea() + 4 * i);
        }
        return;
    }
    if (cam == g_exact_cam_addr) g_cam_exact_pending = false;  // (drawn exactly: nothing to put back)
    p->s = read_cam(cam);  // exact step: remember it for the next step's blended frames
    p->shake = read_shake(cam);
    p->trim = (float)ldf32(cam + kCamBody + kTrimHeight);
    p->valid = true;
    p->drawn = p->s;
    p->drawn_pass = g_passes;
    cam_trace(cam, "exact");
    if (g_hold && true60::enabled()) {  // true 60: the half pass's camera is a preview (true60.cpp)
        p->drawn_pass = ~0ull;  // (drawn with the preview: the stars keep the camera as it is)
        true60::camera_draw_preview(true);
        f_024FFC40_orig(c);
        capture_projection(*p);
        true60::camera_draw_preview(false);
        return;
    }
    static const bool dbg = getenv("WWHD_T60_CAMDRAWLOG") != nullptr;  // debug: statics camera_draw writes
    if (dbg && g_hold) {
        static std::vector<uint32_t> before;
        static std::unordered_map<uint32_t, int> cnt;
        static int n = 0;
        const uint32_t lo = GD(0x10100000), hi = 0x10500000;
        before.assign((uint32_t*)ppc_ptr(lo), (uint32_t*)ppc_ptr(hi));
        f_024FFC40_orig(c);
        capture_projection(*p);
        const uint32_t* cur = (const uint32_t*)ppc_ptr(lo);
        for (size_t i = 0; i < before.size(); i++)
            if (cur[i] != before[i]) cnt[lo + 4 * (uint32_t)i]++;
        if (++n % 200 == 0) {
            std::vector<std::pair<uint32_t, int>> v(cnt.begin(), cnt.end());
            std::sort(v.begin(), v.end());
            std::string o;
            uint32_t start = 0, last = 0; int c0 = 0;
            for (auto& [a, k] : v) {
                if (start && a == last + 4) { last = a; continue; }
                if (start) { char t[48]; snprintf(t, sizeof t, " %08X-%08X:%d", start, last + 3, c0); o += t; }
                start = last = a; c0 = k;
            }
            if (start) { char t[48]; snprintf(t, sizeof t, " %08X-%08X:%d", start, last + 3, c0); o += t; }
            LOG("[camdrawlog]%s", o.c_str());
        }
        return;
    }
    f_024FFC40_orig(c);
    capture_projection(*p);
    g_view_pass = g_passes;
}

// The HD projection zoom (dCamera_c +0x730 mScale, +0x734 mTarget): camera_draw, which runs on every
// drawn pass, eases it towards 1 with cLib_addCalc (0.2, at most 0.3, at least 0.1 per call) after
// the cinema-scope display mode (trim sizes 2/3 set 4:3, 1.333) ends, so the ease ran 2x/4x/8x per
// logic step. The call (returning to 0250018C) is gated (interp_fade.h floats_call): the zoom eases
// once per logic step and every pass shows it at its fraction. true60.cpp hook_0200ECD4 hands the
// call here. debug: WWHD_INTERP_ZOOM_EASE=0 turns the gate off
namespace interp {
bool zoom_ease(Cpu* c, void (*orig)(Cpu*)) {
    static const bool off = getenv("WWHD_INTERP_ZOOM_EASE") && !atoi(getenv("WWHD_INTERP_ZOOM_EASE"));
    if (!interp_on() || off || true60::enabled() || !g_drawing_cam) return false;
    Prev* p = prev_for(g_drawing_cam);
    const uint32_t v = c->r[3];
    float x = (float)ldf32(v), before = x;
    double ret = 0;
    // (the exact pre-run of a logic pass advanced it: the blended run after it only shows it)
    const bool advance = !g_hold && (g_cam_prerun || g_prerun_pass != g_passes);
    fade::floats_call(p->zoom, g_drawing_cam, g_logic_steps, advance, g_cam_prerun ? 1.0f : pass_t(), 1u, &x, [&](float* y) {
        stf32(v, y[0]);
        orig(c);
        y[0] = (float)ldf32(v);
        ret = c->f[1].ps0;
    });
    stf32(v, x);
    c->f[1].ps0 = ret;
    // (camera_draw stores the drawn zoom in mScale/mTarget, dCamera_c +0x730/+0x734: exact again before
    // the next step's logic after a 40 fps blended record pass)
    if (x != p->zoom.after[0])
        for (uint32_t o : {0x730u, 0x734u}) exact_after_pass(g_drawing_cam + kCamBody + o, f32_as_u32(p->zoom.after[0]), 4);
    static int trace = getenv("WWHD_INTERP_CAM_TRACE") ? atoi(getenv("WWHD_INTERP_CAM_TRACE")) : 0;
    if (trace > 0 && (before != 1.0f || x != 1.0f)) {
        trace--;
        LOG("[interp] zoom ease step %llu t %.3f: %.4f -> %.4f, drawn %.4f", (unsigned long long)g_logic_steps, pass_t(), p->zoom.before[0],
            p->zoom.after[0], x);
    }
    return true;
}
}  // namespace interp

// dCamera_c::bumpCheck (024FD11C, from dCamera_c::Run in the camera's execute): the collision check
// that pulls the eye in front of walls, trees and bushes. dCamera_c +0 is its camera process (the
// one camera_draw draws), +0x510 mEventFlags: Run clears 0x80 and 0x80000 just before the call, and
// bumpCheck sets 0x80 (wall hit, eye placed in front of it) or 0x80000 (corner). The step's
// camera_draw applies the 10 % distance rule (interp_camera.h) only on such steps, and for event and
// demo cameras (style algorithm 11: mCamParam +4 -> +4; the demo object's active camera,
// dDemo_object_c +0x84).
extern "C" void f_024FD11C_orig(Cpu* c);
extern "C" void hook_024FD11C(Cpu* c) {
    using namespace interp;
    const uint32_t dcam = c->r[3];
    f_024FD11C_orig(c);
    if (!enabled()) return;
    const uint32_t cam = ld32(dcam);
    camera::StepInfo info;
    info.pulled = (ld32(dcam + 0x510) & 0x80080u) != 0;
    const uint32_t param = ld32(dcam + 0x8A8);
    info.event = param && ld32(param + 4) == 11;
    const uint32_t demo = ld32(GD(0x101D5FFC));
    info.event = info.event || (demo && ld32(demo + 0x84) != 0);
    CamStep* slot = nullptr;
    for (auto& s : g_cam_step)
        if (s.cam == cam) slot = &s;
    if (!slot) {  // a new camera process: the slot used longest ago
        slot = &g_cam_step[0];
        for (auto& s : g_cam_step)
            if (s.step == ~0ull || (slot->step != ~0ull && s.step < slot->step)) slot = &s;
    }
    slot->cam = cam;
    slot->step = g_logic_steps;
    slot->info = info;
    static int trace = getenv("WWHD_INTERP_CAM_TRACE") ? atoi(getenv("WWHD_INTERP_CAM_TRACE")) : 0;
    if (trace > 0 && (info.pulled || info.event)) {
        trace--;
        LOG("[interp] camera %08X step %llu: %s%s", cam, (unsigned long long)g_logic_steps, info.pulled ? "collision pull-in " : "",
            info.event ? "event/demo camera" : "");
    }
}

// Models. J3DModel::calc (027F4D5C, actor Execute or Draw) writes the world (joint) matrices:
// model +0x2C -> joint matrix block, block +0x10 -> world matrices (3x4, row-major), block +0x2C
// u16 count. In actor Draw, J3DModel::viewCalc (027F55FC, via mDoExt_modelUpdateDL /
// modelEntryDL) computes view-space draw matrices (027DE8A0) and queues the model for the
// "update_ubo" job thread, whose UBO update (027F5018) copies the world matrices into the uniform
// buffers the painter uses, concurrently with the rest of the main thread's draw.
// Record pass: record each model's world matrices (step S+1, keyed by the joint matrix block).
// Logic pass and blended hold passes: viewCalc and the UBO update run on blend(recorded step,
// current step, pass_t()), both on the main thread inside the viewCalc hook (the job skips the
// model); right after, the exact matrices are back in place, so game logic and attachments
// (swords, carried bombs, the boat's parts) keep reading the exact step.
// (The UBO update used to stay on the job thread, with the blended matrices written into the
// model's world matrices around it: that put them in guest memory while the main thread was still
// drawing, and whatever it attached to the model in that window - an item in Link's hand, parts of
// the boat - was placed from the blended matrices and then blended a second time: random one-frame
// jumps of carried and attached models (issue #68).)
namespace interp {
float pass_t();
namespace {
constexpr uint32_t kMdlJoints = 0x2C, kJntMtx = 0x10, kJntNum = 0x2C;
constexpr int kMaxJoints = 1024;

const SplooshMarkers* g_sploosh_markers = nullptr;  // scoped to the board draw, main thread only

struct ModelPrev {
    uint8_t marker_cell = 0;
    uint32_t mtx = 0;
    uint64_t pass = 0;  // hold pass it was recorded on
    std::vector<uint32_t> w;  // raw guest words, 12 per matrix
    std::vector<uint8_t> stale;  // per joint: hidden and not updated on that record pass (interp_joints.h)
};
std::unordered_map<uint32_t, ModelPrev> g_models;
// 40 fps blended record pass: the records it replaced, which its blends use (cleared every pass)
std::unordered_map<uint32_t, ModelPrev> g_models_before;
// shown: joints of blended models drawn at the current step (hidden pose joints coming back)
struct ModelStats { uint32_t blended = 0, fresh = 0, cut = 0, shown = 0; } g_mstats;

// Does joint `j` of `model` draw nothing? True when the joint has shapes of its own and every one is
// hidden. HD J3D layout (as in daPy_lk_c::setDrawHandModel): model +0xAC model data; data +4 joint
// count, +8 joint nodes (0x1C each); node +0x10 the first material of its mesh, material +4 the next
// one, +8 its shape; shape +4 visible (u8). Joints without shapes (bones) count as drawn.
bool joint_hidden(uint32_t data, uint32_t j) {
    uint32_t mat = ld32(ld32(data + 8) + j * 0x1C + 0x10);
    if (!mat) return false;
    for (int guard = 0; mat && guard < 256; mat = ld32(mat + 4), guard++) {
        const uint32_t shape = ld32(mat + 8);
        if (!shape || ld8(shape + 4)) return false;
    }
    return !mat;
}
// models whose UBO update of this pass already ran on the main thread (blended), per model: the
// update_ubo job skips that many of its updates
std::mutex g_ubo_mu;
std::unordered_map<uint32_t, uint32_t> g_ubo_done;

float wf(uint32_t w) { return u32_as_f32(__builtin_bswap32(w)); }
uint32_t fw(float f) { return __builtin_bswap32(f32_as_u32(f)); }

// debug: WWHD_INTERP_MODEL_TRACE=n logs n draws of one model (the first with at least
// WWHD_INTERP_MODEL_TRACE_JOINTS joints, default 40): root joint translation as drawn
// _ALL=1 follows every eligible model; _START=n starts at logic step n. _FILE=path
// also writes CSV: step,t,decision,joint-block,joint-count,joint-index,12 matrix floats.
// _SPLOOSH=1 restricts it to the board markers (use _JOINTS=1).
void trace_model(const char* what, uint32_t jnt, uint32_t n, const uint32_t* w) {
    static int left = getenv("WWHD_INTERP_MODEL_TRACE") ? atoi(getenv("WWHD_INTERP_MODEL_TRACE")) : 0;
    static const uint32_t min_joints = getenv("WWHD_INTERP_MODEL_TRACE_JOINTS") ? atoi(getenv("WWHD_INTERP_MODEL_TRACE_JOINTS")) : 40;
    static const bool all = getenv("WWHD_INTERP_MODEL_TRACE_ALL") != nullptr;
    static const uint64_t start = getenv("WWHD_INTERP_MODEL_TRACE_START") ? strtoull(getenv("WWHD_INTERP_MODEL_TRACE_START"), nullptr, 10) : 0;
    static const bool sploosh = getenv("WWHD_INTERP_MODEL_TRACE_SPLOOSH") != nullptr;
    if (sploosh) {
        bool marker = false;
        if (g_sploosh_markers) for (const auto& m : *g_sploosh_markers)
            if (m.model && ld32(m.model + kMdlJoints) == jnt) { marker = true; break; }
        if (!marker) return;
    }
    static uint32_t which = 0;
    if (left <= 0 || g_logic_steps < start || n < min_joints || (!all && which && which != jnt)) return;
    which = jnt;
    left--;
    static FILE* matrices = [] {
        const char* path = getenv("WWHD_INTERP_MODEL_TRACE_FILE");
        return path ? fopen(path, "w") : nullptr;
    }();
    if (matrices) {
        for (uint32_t joint = 0; joint < n; ++joint) {
            fprintf(matrices, "%llu,%.6f,%s,%08X,%u,%u", (unsigned long long)g_logic_steps,
                    pass_t(), what, jnt, n, joint);
            for (int k = 0; k < 12; ++k) fprintf(matrices, ",%.9g", wf(w[joint * 12 + k]));
            fputc('\n', matrices);
        }
        fflush(matrices);
    }
    LOG("[interp] model %08X (%u joints, mtx %08X) step %llu t %.3f %-7s root %.2f %.2f %.2f", jnt, n, ld32(jnt + kJntMtx),
        (unsigned long long)g_logic_steps, pass_t(), what, wf(w[3]), wf(w[7]), wf(w[11]));
}

// rotation part of a 3x4 matrix (columns = scaled axes) to a unit quaternion; false if it is not a
// rotation times a positive scale per axis (shear, mirror, degenerate)
bool to_quat(const float* m, float* s, float* q) {
    float r[3][3];
    for (int j = 0; j < 3; j++) {
        s[j] = std::sqrt(m[j] * m[j] + m[4 + j] * m[4 + j] + m[8 + j] * m[8 + j]);
        if (s[j] < 1e-6f) return false;
        for (int i = 0; i < 3; i++) r[i][j] = m[4 * i + j] / s[j];
    }
    for (int a = 0; a < 3; a++)
        for (int b = a + 1; b < 3; b++)
            if (std::fabs(r[0][a] * r[0][b] + r[1][a] * r[1][b] + r[2][a] * r[2][b]) > 0.02f) return false;
    float det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    if (det < 0.9f) return false;
    float tr = r[0][0] + r[1][1] + r[2][2];
    if (tr > 0) {
        float k = 0.5f / std::sqrt(tr + 1.0f);
        q[0] = 0.25f / k; q[1] = (r[2][1] - r[1][2]) * k; q[2] = (r[0][2] - r[2][0]) * k; q[3] = (r[1][0] - r[0][1]) * k;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        float k = 2.0f * std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]);
        q[0] = (r[2][1] - r[1][2]) / k; q[1] = 0.25f * k; q[2] = (r[0][1] + r[1][0]) / k; q[3] = (r[0][2] + r[2][0]) / k;
    } else if (r[1][1] > r[2][2]) {
        float k = 2.0f * std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]);
        q[0] = (r[0][2] - r[2][0]) / k; q[1] = (r[0][1] + r[1][0]) / k; q[2] = 0.25f * k; q[3] = (r[1][2] + r[2][1]) / k;
    } else {
        float k = 2.0f * std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]);
        q[0] = (r[1][0] - r[0][1]) / k; q[1] = (r[0][2] + r[2][0]) / k; q[2] = (r[1][2] + r[2][1]) / k; q[3] = 0.25f * k;
    }
    return true;
}

// between two 3x4 matrices at t: translation and scale linear, rotation slerp (nlerp at 1/2 is
// exact, and keeps the original halfway arithmetic); anything that is not rotation x scale is
// blended element-wise
void blend_mtx(const float* a, const float* b, float* out, float t) {
    float sa[3], sb[3], qa[4], qb[4];
    for (int i = 0; i < 3; i++) out[4 * i + 3] = lerp_f(a[4 * i + 3], b[4 * i + 3], t);
    if (!to_quat(a, sa, qa) || !to_quat(b, sb, qb)) {
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) out[4 * i + j] = lerp_f(a[4 * i + j], b[4 * i + j], t);
        return;
    }
    float d = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
    float sg = d < 0 ? -1.0f : 1.0f, q[4], n = 0;
    float wa, wb;
    pacing::slerp_weights(std::fabs(d), t, wa, wb);  // slerp the short way (|d|); nlerp at t = 1/2
    wb *= sg;
    for (int k = 0; k < 4; k++) { q[k] = wa * qa[k] + wb * qb[k]; n += q[k] * q[k]; }
    n = 1.0f / std::sqrt(n);
    float w = q[0] * n, x = q[1] * n, y = q[2] * n, z = q[3] * n;
    const float r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
                           {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
                           {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
    for (int j = 0; j < 3; j++) {
        float s = lerp_f(sa[j], sb[j], t);
        for (int i = 0; i < 3; i++) out[4 * i + j] = r[i][j] * s;
    }
}
}  // namespace
void blend_world_matrix(const float* a, const float* b, float* out, float t) { blend_mtx(a, b, out, t); }
}  // namespace interp

// The board assigns its pooled J3D hit/miss models by scanning cells at +0x598
// (021C4F34), so an earlier cell inserted by a shot shifts later assignments.
// Carry that cell identity into the matrix history. The board, cursor, ships and
// models whose assignment stayed the same retain normal camera/model blending.
extern "C" void f_021C4BB4_orig(Cpu* c);
extern "C" void hook_021C4BB4(Cpu* c) {
    using namespace interp;
    if (!enabled()) { f_021C4BB4_orig(c); return; }
    const uint32_t board = c->r[3];
    const auto cells = sploosh_marker_cells(static_cast<const uint8_t*>(ppc_ptr(board + 0x598)));
    SplooshMarkers markers{};
    for (unsigned i = 0; i < markers.size(); ++i)
        if (cells[i]) markers[i] = {ld32(board + 0x3BC + 4 * i), cells[i]};
    const auto* previous = g_sploosh_markers;
    g_sploosh_markers = &markers;
    f_021C4BB4_orig(c);
    g_sploosh_markers = previous;
}

// Model particles (pot shards, rock and barrel debris: dPa_modelEmitterDraw 025A4204) take a model from
// a pool for each particle on every draw and place it from the particle, which is already drawn
// blended (interp_fx.cpp). Blending those models again would mix two particles' matrices (the pool
// hands its models out in particle order, which changes as particles are born and die) and blend a
// blended value a second time: they are drawn as placed. debug: WWHD_INTERP_PTCL_MODELS=1 blends them
namespace interp { bool g_ptcl_model_draw = false; }
extern "C" void f_025A4204_orig(Cpu* c);
extern "C" void hook_025A4204(Cpu* c) {
    static const bool blend = getenv("WWHD_INTERP_PTCL_MODELS") && atoi(getenv("WWHD_INTERP_PTCL_MODELS"));
    if (blend) { f_025A4204_orig(c); return; }
    const bool saved = interp::g_ptcl_model_draw;
    interp::g_ptcl_model_draw = true;
    f_025A4204_orig(c);
    interp::g_ptcl_model_draw = saved;
}
// J3DModel::viewCalc(J3DModel*)
extern "C" void hook_027F55FC(Cpu* c) {
    using namespace interp;
    static const bool off = getenv("WWHD_INTERP_MODELS") && !atoi(getenv("WWHD_INTERP_MODELS"));  // debug
    if (!enabled() || off || g_ptcl_model_draw || (!g_hold && !g_record_logic && (true60::drawing_60() || g_exact_step))) {
        f_027F55FC_orig(c);
        return;
    }
    const bool record = record_pass();
    uint32_t model = c->r[3];
    const uint8_t marker_cell = sploosh_marker_cell(g_sploosh_markers, model);
    uint32_t jnt = ld32(model + kMdlJoints);
    uint32_t mtx = jnt ? ld32(jnt + kJntMtx) : 0;
    uint32_t n = jnt ? ld16(jnt + kJntNum) : 0;
    if (!mtx || n == 0 || n > kMaxJoints) {
        f_027F55FC_orig(c);
        return;
    }
    const uint32_t words = 12 * n;
    const uint32_t* cur = (const uint32_t*)ppc_ptr(mtx);  // guest (big-endian) words
    // 40 fps blended record pass: this step is recorded (stamped for the next pass) and drawn blended
    // from the record it replaces
    const bool blendRecord = record_blended();
    const uint64_t stamp = record_stamp();
    if (blendRecord) {
        auto old = g_models.find(jnt);
        if (old != g_models.end() && old->second.pass != stamp) g_models_before[jnt] = old->second;
    }
    if (record || blendRecord) {  // exact step: remember it for the next step's blended frames
        ModelPrev& p = g_models[jnt];
        // joints hidden and not written since the last record pass: drawn at the next step instead of
        // blended from here (interp_joints.h; a model drawn twice in a pass keeps its first decision)
        if (p.pass != stamp || p.stale.size() != n) {
            const bool before = p.pass + 1 == stamp && p.mtx == mtx && p.w.size() == words;
            const uint32_t data = ld32(model + 0xAC);
            const bool layout = data && ld32(data + 4) == n && ld32(data + 8);
            p.stale.assign(n, 0);
            for (uint32_t j = 0; layout && j < n; j++)
                p.stale[j] = joints::stale(joint_hidden(data, j), cur + 12 * j, before ? p.w.data() + 12 * j : nullptr);
        }
        // debug: WWHD_INTERP_MULTIDRAW=n logs n models whose joint block is drawn again on the same
        // record pass with other matrices (one model placed several times per pass: only the last
        // placement is recorded, so the next step's first placement would blend from another one)
        static int multidraw = getenv("WWHD_INTERP_MULTIDRAW") ? atoi(getenv("WWHD_INTERP_MULTIDRAW")) : 0;
        if (multidraw > 0 && p.pass == stamp && p.w.size() == words && memcmp(p.w.data(), cur, 4 * words) != 0) {
            multidraw--;
            const float dx = wf(p.w[3]) - wf(cur[3]), dy = wf(p.w[7]) - wf(cur[7]), dz = wf(p.w[11]) - wf(cur[11]);
            LOG("[interp] step %llu: model %08X (joints %08X, %u) drawn again on the record pass, root %.1f away (vtable caller %08X)",
                (unsigned long long)g_logic_steps, model, jnt, n, std::sqrt(dx * dx + dy * dy + dz * dz), c->lr);
        }
        p.marker_cell = marker_cell;
        p.mtx = mtx;
        p.pass = stamp;
        p.w.assign(cur, cur + words);
        if (!blendRecord) {
            trace_model("exact", jnt, n, cur);
            f_027F55FC_orig(c);
            return;
        }
    }
    // logic pass or blended hold pass: between the step drawn last and the new one, at pass_t()
    auto& from = blendRecord ? g_models_before : g_models;
    auto it = from.find(jnt);
    if (it == from.end() || it->second.mtx != mtx || it->second.w.size() != words ||
        it->second.pass + 1 < g_record_passes) {  // new model, or not drawn on the last record pass
        g_mstats.fresh++;
        trace_model("new", jnt, n, cur);
        f_027F55FC_orig(c);
        return;
    }
    if (it->second.marker_cell != marker_cell) {  // a pooled marker now represents another cell
        g_mstats.fresh++;
        trace_model("reassigned", jnt, n, cur);
        f_027F55FC_orig(c);
        return;
    }
    const std::vector<uint32_t>& prev = it->second.w;
    if (memcmp(prev.data(), cur, 4 * words) == 0) {  // not moving
        trace_model("still", jnt, n, cur);
        f_027F55FC_orig(c);
        return;
    }
    static const float kCut = getenv("WWHD_INTERP_MODEL_CUT") ? (float)atof(getenv("WWHD_INTERP_MODEL_CUT")) : 400.0f;
    const float t = pass_t();
    std::vector<uint32_t> saved(cur, cur + words);
    std::vector<uint32_t> mid(words);
    const auto r = joints::blend(prev.data(), saved.data(), it->second.stale.size() == n ? it->second.stale.data() : nullptr, n,
                                 kCut, t, mid.data(), blend_mtx);
    // debug: WWHD_INTERP_JOINT_TRACE=1 logs (on the logic pass) the hidden joints drawn at the current
    // step and the joint that made a model's cut
    static const bool joint_trace = getenv("WWHD_INTERP_JOINT_TRACE") != nullptr;
    if (r.cut) {  // teleport (or NaN): show the new step
        if (joint_trace && !g_hold) {
            const uint32_t* a = prev.data() + 12 * r.cut_joint;
            const uint32_t* b = saved.data() + 12 * r.cut_joint;
            LOG("[interp] step %llu: model %08X (%u joints) cut by joint %u: %.1f %.1f %.1f -> %.1f %.1f %.1f",
                (unsigned long long)g_logic_steps, model, n, r.cut_joint, wf(a[3]), wf(a[7]), wf(a[11]), wf(b[3]), wf(b[7]), wf(b[11]));
        }
        g_mstats.cut++;
        trace_model("cut", jnt, n, cur);
        f_027F55FC_orig(c);
        return;
    }
    g_mstats.blended++;
    g_mstats.shown += r.shown;
    // (WWHD_INTERP_JOINT_TRACE: how far the blend from their stale matrix would have moved them)
    if (joint_trace && r.shown && !g_hold)
        for (uint32_t j = 0; j < n; j++) {
            const uint32_t* a = prev.data() + 12 * j;
            const uint32_t* b = saved.data() + 12 * j;
            if (!it->second.stale[j] || memcmp(a, b, 48) == 0) continue;
            const float dx = wf(a[3]) - wf(b[3]), dy = wf(a[7]) - wf(b[7]), dz = wf(a[11]) - wf(b[11]);
            LOG("[interp] step %llu: model %08X joint %u hidden on the last step, shown at %.1f %.1f %.1f (was %.1f away)",
                (unsigned long long)g_logic_steps, model, j, wf(b[3]), wf(b[7]), wf(b[11]), std::sqrt(dx * dx + dy * dy + dz * dz));
        }
    trace_model("blended", jnt, n, mid.data());
    {
        std::lock_guard<std::mutex> lk(g_ubo_mu);
        g_ubo_done[model]++;  // (before viewCalc queues the model for the job)
    }
    memcpy(ppc_ptr(mtx), mid.data(), 4 * words);
    f_027F55FC_orig(c);
    const uint32_t r3 = c->r[3];
    c->r[3] = model;
    f_027F5018_orig(c);  // the model's UBO update, here and now on the blended matrices
    c->r[3] = r3;
    memcpy(ppc_ptr(mtx), saved.data(), 4 * words);
}

// J3DModel UBO update (027F5018), run by the "update_ubo" job thread while the main thread is
// still drawing: copies the world matrices into the model's uniform buffers. A model blended by
// the viewCalc hook had its update there already (on the blended matrices): skipped here.
extern "C" void hook_027F5018(Cpu* c) {
    using namespace interp;
    const uint32_t model = c->r[3];
    {
        std::lock_guard<std::mutex> lk(g_ubo_mu);
        if (!enabled()) g_ubo_done.clear();  // switched off since viewCalc
        auto it = g_ubo_done.find(model);
        if (it != g_ubo_done.end()) {
            if (--it->second == 0) g_ubo_done.erase(it);
            return;
        }
    }
    f_027F5018_orig(c);
}

// Per-frame function (0203593C): WWHD's own per-frame systems (HD menus, system UI, lighting setup)
// around the loop body. On hold passes the whole frame is held back and only redrawn, so these
// systems stay in step with the game logic at 30 steps per second.
namespace interp {
void fx_pass_start();          // interp_fx.cpp: puts back the values drawn blended
void fx_anm_settle(Cpu* c);    // interp_fx.cpp: material animations exact again before a logic pass
void fx_hold_blend(float t);   // interp_fx.cpp: blended hold pass: logic-time effect blends at t
void fx_ss_reset();
void nodraw_reset();          // fopAc_Draw hook: the step's one-shot no-draw requests
void reticle_reset();
// a save state was loaded: nothing may blend across the jump
void ss_reset() {
    mods::moon_jump_reset();
    mods::jump_reset();
    countdown::reset();
    for (auto& p : g_prev) p = Prev{};
    g_models.clear();
    {
        std::lock_guard<std::mutex> lk(g_ubo_mu);
        g_ubo_done.clear();
    }
    g_record_passes += 8;  // step-stamped histories (models, effects) no longer match
    g_hold_next = false;
    g_phase = 0;
    g_step_n = 1;
    fx_ss_reset();
    nodraw_reset();
    reticle_reset();
    true60::ss_reset();
}
// Paced interpolation (WWHD_INTERP_PACED=1, the default on Android, Windows on ARM64 and at 120/240
// fps): an in-between pass is drawn only when it fits before the next logic step is due (33.3 ms
// after the last one, measured with the passes' recent durations); otherwise the rest of the step's
// in-between passes are dropped and the next step waits for its time. The game then always advances
// 30 steps a second, and the picture gets 60/120/240 frames a second where the device draws (and
// the display shows) them fast enough and fewer where it does not. Without pacing, every logic step
// is followed by all its in-between passes, and a device that draws fewer frames a second runs the
// whole game slower than real time (at 120/240 fps on a 60 Hz display with vsync: half or a quarter
// of the speed), which is why pacing is on by default there.
// At 120/240 fps the logic pass also plans how many in-between frames the step gets (as many as
// fit at the recent pass duration, at least one) and spaces them evenly over the step
// (t = k/(n+1)): a 60 Hz display then gets clean 60 fps blending instead of uneven frames and a
// dropped exact frame every step. A step whose passes all fit gets the full N (t = k/(N+1)).
// The settings overlay switches it ("Keep game speed", saved with the graphics options, one value
// for 60 fps and one for 120/240 fps); the variable sets both at start and wins over the saved
// values.
static const char* const g_env_paced = getenv("WWHD_INTERP_PACED");
// Windows on ARM64 (Snapdragon X) draws 60 fps only where the Adreno driver's per-draw CPU cost
// allows: unpaced, the Outset save-state benchmark ran 16 logic steps a second (half speed, and the
// audio, which follows the game's speed, crackled)
static std::atomic<bool> g_paced{[] {
#if defined(__ANDROID__) || (defined(_WIN32) && (defined(_M_ARM64) || defined(__aarch64__)))
    return !g_env_paced || atoi(g_env_paced) != 0;
#else
    return g_env_paced && atoi(g_env_paced) != 0;
#endif
}()};
static std::atomic<bool> g_paced_hi{!g_env_paced || atoi(g_env_paced) != 0};  // 120/240 fps
static std::atomic<bool>& paced_flag(int f) { return f > 60 ? g_paced_hi : g_paced; }
static bool paced() { return paced_flag(fps()).load(std::memory_order_relaxed); }
bool paced_interpolation() { return paced(); }  // at the current frame rate
// the step schedule of paced interpolation is in use (gx2_core.cpp step slots): Keep game speed on
// at the current rate and frame interpolation on (not true 60, whose passes are not dropped)
bool paced_step_schedule() { return paced() && interp_on(); }
bool paced_interpolation_at(int f) { return paced_flag(f).load(std::memory_order_relaxed); }
// Where each value came from, for the log (players' logs show which one they play with): the
// platform's default, WWHD_INTERP_PACED, the saved setting or a change in the settings overlay.
enum PacedSource : int { kPacedDefault, kPacedEnv, kPacedSaved, kPacedChanged };
static std::atomic<int> g_paced_src{g_env_paced ? kPacedEnv : kPacedDefault}, g_paced_hi_src{g_env_paced ? kPacedEnv : kPacedDefault};
static std::atomic<int>& paced_src(int f) { return f > 60 ? g_paced_hi_src : g_paced_src; }
static const char* paced_src_name(int src) {
    switch (src) {
    case kPacedEnv: return "WWHD_INTERP_PACED";
    case kPacedSaved: return "saved setting";
    case kPacedChanged: return "changed in the settings";
    default: return "default on this platform";
    }
}
static std::atomic<bool> g_paced_start_logged{false};
// the start values, once (the first frame, after the hosts loaded the saved settings)
static void log_paced_start() {
    if (g_paced_start_logged.exchange(true)) return;
    LOG("[interp] keep game speed at 60 fps %s (%s), at 120/240 fps %s (%s)", g_paced.load() ? "on" : "off",
        paced_src_name(g_paced_src.load()), g_paced_hi.load() ? "on" : "off", paced_src_name(g_paced_hi_src.load()));
}
static void set_paced(int f, bool on, int src) {
    const bool changed = paced_flag(f).exchange(on) != on;
    paced_src(f) = src;
    // a saved value that equals the default is logged with the start values; any other change now
    if (changed || (src != kPacedSaved && g_paced_start_logged.load()))
        LOG("[interp] keep game speed at %s %s (%s)", f > 60 ? "120/240 fps" : "60 fps", on ? "on" : "off", paced_src_name(src));
}
void set_paced_interpolation_at(int f, bool on) { set_paced(f, on, kPacedChanged); }
void load_paced_interpolation_at(int f, bool on) { set_paced(f, on, kPacedSaved); }
void set_paced_interpolation(bool on) { set_paced_interpolation_at(fps(), on); }
// share of in-between frames drawn over the last 60 steps (performance overlay), -1 before any
static std::atomic<float> g_paced_share{-1};
float paced_drawn_share() { return paced() && interp_on() ? g_paced_share.load(std::memory_order_relaxed) : -1; }
// The decision is taken at the end of each pass, before the frame's controller read: that read
// repeats the previous sample when an in-between pass follows (repeat_input), so deciding later
// left every read a repeat while all in-between passes were dropped (the controller stopped).
using pace_clock = std::chrono::steady_clock;
static pace_clock::time_point g_last_logic{}, g_last_entry{};
// recent durations of logic passes and of hold passes (they differ: a hold pass only draws); a
// pass's duration includes its wait for the (virtual) vsync, so it is never below one tick
static pace_clock::duration g_slept{}, g_logic_avg = std::chrono::milliseconds(16), g_hold_avg = std::chrono::milliseconds(16);
static pace_clock::time_point g_last_hold{};  // the last hold pass drawn (probe after a long pause)
static bool g_probe = false;  // this hold pass re-measures their cost after a long pause
static pace_clock::time_point g_probe_retry{};  // another probe then (the last one met a hitch)
constexpr auto kProbeAfter = std::chrono::seconds(10), kProbeRetry = std::chrono::seconds(1);
// one tick of the virtual vsync at the drawn rate (59.94 Hz x frames per step / 2; 40 fps: 25.03 ms)
static pace_clock::duration vsync_tick() {
    return std::chrono::nanoseconds(fps40() ? frame_ns() : 16'683'333LL * 2 / std::max(2, frames_per_step()));
}
// (the hold-pass estimate after a pass of the previous kind: logic or hold)
static bool g_pass_was_hold = false;
static bool g_wait_step = false;  // the next logic pass waits for its time
// 60 fps: the step before the next logic pass dropped its in-between frame, and the next logic pass
// starts at once on the step slots (pacing::wait_after_drop; without a slot it waits as before)
static bool g_drop_lead = false;
static uint64_t g_paced_possible = 0, g_paced_holds = 0, g_paced_steps = 0, g_paced_planned = 0;
// a logic step: two vsyncs of the 59.94 Hz grid (the game's swap interval; 29.97 steps a second, as
// on the console). (It was 33.333 ms, which drifted against the grid's 33.367 ms: issue #164.)
constexpr auto kPacedStep = std::chrono::nanoseconds(2 * 16'683'333);
// the passes of a step wait for the grid's vsyncs and together take the whole step: 2 ms of slack
constexpr auto kPacedBudget = std::chrono::nanoseconds(kPacedStep + std::chrono::milliseconds(2));
// Even frame delivery (interp_pacing.h EvenSchedule): the renderer holds a frame that is ready
// early until one frame interval after the previous one. On by default on Android (phones whose
// game thread is the bottleneck: logic and in-between passes of different lengths showed a third of
// the 60 fps frames for 1 or 3 refreshes of a 120 Hz screen); off elsewhere until measured there.
// WWHD_INTERP_EVEN_PACING=0/1 wins. The interval of the frame being swapped: the step's length over
// its planned frames (60 fps and true 60: 16.68 ms; paced 120/240 fps: over the frames this step
// plans); 0 (frames go out as they come) at 30 fps.
#ifdef __ANDROID__
constexpr bool kEvenPacingDefault = true;
#else
constexpr bool kEvenPacingDefault = false;
#endif
static const char* const g_env_even = getenv("WWHD_INTERP_EVEN_PACING");
static const bool g_even_pacing = g_env_even ? atoi(g_env_even) != 0 : kEvenPacingDefault;
bool even_pacing() { return g_even_pacing; }
int64_t frame_interval_ns() {
    static bool logged = false;
    if (!logged) {
        logged = true;
        LOG("[interp] even frame pacing %s (%s): %s", g_even_pacing ? "on" : "off",
            g_env_even ? "WWHD_INTERP_EVEN_PACING" : "default on this platform",
            g_even_pacing ? "a frame ready early waits for its slot before it is presented" : "frames are presented when ready");
    }
    if (!g_even_pacing) return 0;
    if (!enabled() || frames_per_step() < 2) return 0;
    if (fps40()) return frame_ns();  // every frame 1.5 vsyncs apart, whatever the step's frames
    return int64_t(kPacedStep.count()) / (std::max(1, g_step_n) + 1);
}
// debug: WWHD_TRACE_STEPS=file writes a line per paced logic pass at 120/240 fps: its start (steady
// clock, ms), the step start before and after it (W waited for it, K kept the schedule, N started a
// new one), and the recent logic and hold pass durations; and a line per skip to the record pass
// (the phase it followed) and per dropped rest of a step (D). With WWHD_TRACE_FLIPS (gx2_core.cpp).
static void trace_step(pace_clock::time_point now, pace_clock::time_point before, char how) {
    static FILE* out = getenv("WWHD_TRACE_STEPS") ? fopen(getenv("WWHD_TRACE_STEPS"), "w") : nullptr;
    if (!out) return;
    const auto ms = [](pace_clock::time_point t) { return std::chrono::duration<double, std::milli>(t.time_since_epoch()).count(); };
    fprintf(out, "%.3f %.3f %.3f %c %.3f %.3f\n", ms(now), ms(before), ms(g_last_logic), how, std::chrono::duration<double, std::milli>(g_logic_avg).count(),
            std::chrono::duration<double, std::milli>(g_hold_avg).count());
}
// start of every pass: the last pass's own duration, and the wait before a logic pass that follows
// a dropped in-between pass (and at 120/240 fps before every logic pass that comes early)
static bool g_previous_record = false;  // the pass before this one was a record pass (or a record logic pass)
static bool g_resume_step = false;
static void restart_fast_forward_pacing(bool active) {
    g_wait_step = g_drop_lead = g_previous_record = g_pass_was_hold = false;
    g_last_entry = g_last_logic = {};
    g_slept = {};
    // The old estimates include waits at the previous rate and may suppress
    // the first resumed step's in-between frames. Measure the new mode afresh.
    g_logic_avg = g_hold_avg = vsync_tick();
    g_last_hold = pace_clock::now();
    g_probe = false;
    g_probe_retry = {};
    g_resume_step = !active && interp_on();
    gx2::drop_step_slot();
}
static void paced_pass_start() {
    bool& previousRecord = g_previous_record;
    if (!paced() || !interp_on()) { g_exact_step = false; previousRecord = false;
        if (mods::fast_forward_active()) { g_wait_step = g_drop_lead = false; g_last_entry = {}; g_last_logic = {}; }
        return; }
    if (!g_hold_next) g_exact_step = !previousRecord;  // this logic pass: blend only after a record pass
    previousRecord = g_hold_next && g_phase + 1 >= g_step_n;  // this pass is the step's record pass
    const auto now = pace_clock::now();
    if (g_last_entry != pace_clock::time_point{}) {
        const auto pass = now - g_last_entry - g_slept;
        pace_clock::duration& avg = g_pass_was_hold ? g_hold_avg : g_logic_avg;
        // a probe after a long pause replaces the old value (pacing::probe_estimate; one that hit a
        // hitch is repeated a second later)
        if (g_pass_was_hold && g_probe) {
            bool retry;
            avg = pace_clock::duration(pacing::probe_estimate(avg.count(), pass.count(), retry));
            if (retry) {
                LOG("[interp] paced: the in-between pass probe took %.1f ms (a hitch?): another probe in a second",
                    std::chrono::duration<double, std::milli>(pass).count());
                g_probe_retry = now + kProbeRetry;
            }
        } else {
            avg = pace_clock::duration(pacing::update_average(avg.count(), pass.count()));
        }
        if (g_pass_was_hold) g_probe = false;
    }
    g_pass_was_hold = g_hold_next;
    g_last_entry = now;
    g_slept = {};
    if (g_hold_next) return;
    // The virtual vsync's step slots are the schedule (gx2/vsync_pacing.h; at 60, 120 and 240 fps): a
    // step's logic frame is due two vsyncs after the previous one's, so the logic pass starts one drawn
    // frame before its slot, as it does after a record pass. (Paced on a clock of its own, the logic pass
    // could sleep past its frame's vsync, which bunched up the step's frames: issue #164.) The slot
    // keeps the schedule for a logic frame that was less than a step late, and the elapsed time
    // counted from it makes the step after a late one plan fewer in-between frames (at 60 fps: skip
    // its in-between frame), so the game catches up. (60 fps had no slots and no catch-up before:
    // a step whose in-between frame was drawn could end up to 2 ms late, a render thread that
    // delays the flips made it later still, and the lost time was never recovered: 24 logic steps a
    // second with a 15 ms render load, slow motion with Keep game speed on.)
    pace_clock::time_point slot;
    if (gx2::next_step_time(slot)) {
        using ns = std::chrono::nanoseconds;
        const auto plan = pacing::slot_step_start(ns(now.time_since_epoch()).count(), ns(slot.time_since_epoch()).count(),
                                                  ns(vsync_tick()).count(), ns(kPacedStep).count(), g_wait_step,
                                                  pacing::keep_window(ns(kPacedStep).count(), ns(vsync_tick()).count(), in_between() == 1));
        const auto before = g_last_logic;
        g_last_logic = pace_clock::time_point(std::chrono::duration_cast<pace_clock::duration>(ns(plan.start)));
        if (plan.how == pacing::StepStart::kWait) {
            threads::park_sleep_until(g_last_logic);
            g_slept = pace_clock::now() - now;
        } else if (plan.how == pacing::StepStart::kLate) {
            gx2::drop_step_slot();
        }
        g_wait_step = g_drop_lead = false;
        trace_step(now, before, plan.how == pacing::StepStart::kWait ? 'W' : plan.how == pacing::StepStart::kKeep ? 'K' : 'N');
        return;
    }
    // (no slot: a step after a dropped in-between frame waits at 60 fps too)
    if (g_drop_lead) g_wait_step = true;
    g_drop_lead = false;
    if (g_wait_step && now < g_last_logic + kPacedStep) {
        const auto due = g_last_logic + kPacedStep;
        threads::park_sleep_until(due);  // the game keeps 30 steps a second
        g_slept = pace_clock::now() - now;
        // the step starts at its time, not when the sleep timer woke up (0.2-1 ms later): counted from
        // the wake-up, every waited step was that much longer than 33.3 ms (at 120 fps, with flips
        // that do not wait for a missed vsync, 28.9 instead of 30 logic steps a second)
        g_wait_step = false;
        g_last_logic = due;
        trace_step(now, due - kPacedStep, 'W');
        return;
    }
    // Before there is a step slot (every step waits for its time): a step that starts late, because
    // its in-between passes took longer than planned, keeps the schedule if it is less than a step
    // late, so this step plans fewer in-between frames and the game catches up (counted from its own
    // start, a late step was lost: 29.4 logic steps a second at 240 fps on a computer that draws ~150
    // fps)
    const bool keep = g_wait_step && g_last_logic != pace_clock::time_point{} &&
                      now - (g_last_logic + kPacedStep) < kPacedStep;
    const auto before = g_last_logic;
    g_wait_step = false;
    g_last_logic = keep ? g_last_logic + kPacedStep : pace_clock::now();
    trace_step(now, before, keep ? 'K' : 'N');
}
// logic pass: the in-between frames of this step (pacing::plan_in_between: as many as fit at the
// recent pass duration)
static int g_step_holds = 0;  // hold passes drawn in this step (a skip to the record pass leaves some out)
static int g_step_capacity = 1;  // in-between frames this step could have (40 fps: 1 or 0)
static uint64_t g_steps40 = 0;  // 40 fps steps without a step slot (one in three gets the in-between frame)
// 40 fps (pacing::is_fps40): 1 in-between frame for the step whose next step's frame is two frames
// later on the step slots, 0 (a blended record pass) for the others; an exact step (no record pass
// before it) is drawn once, as an exact record logic pass, and its slot's second frame stays empty.
// The logic pass's fraction: pacing::Fractions40.
static int plan_step_40() {
    int frames = gx2::step_frames();
    if (frames <= 0) frames = 1 + pacing::in_between_40(g_steps40++);
    else g_steps40 = 0;
    int n = std::clamp(frames - 1, 0, 1);
    g_step_capacity = n;
    g_step_holds = 0;
    if (paced()) g_paced_planned += n;
    if (g_exact_step) n = pacing::exact_step_holds(n, true, g_resume_step);
    g_step40 = true;
    g_t40 = g_frac40.logic(n, g_exact_step);
    return n;
}
static int plan_step() {
    if (fps40()) return plan_step_40();
    g_step40 = false;
    g_step_capacity = std::max(1, in_between());
    int n = std::max(1, in_between());
    if (n > 1 && paced() && interp_on()) {
        n = pacing::plan_in_between(n, kPacedBudget.count(), std::chrono::duration_cast<std::chrono::nanoseconds>(g_logic_avg).count(),
                                    std::chrono::duration_cast<std::chrono::nanoseconds>(g_hold_avg).count());
        // even frame delivery: only counts that divide the display's refreshes per step evenly
        if (g_even_pacing && present_vsync() && display_hz() > 0)
            n = pacing::even_in_between(n, int(std::lround(display_hz() * std::chrono::duration<double>(kPacedStep).count())));
        g_paced_planned += n;
    }
    if (g_exact_step) n = pacing::exact_step_holds(g_step_capacity, false, g_resume_step);
    g_step_holds = 0;
    return n;
}
// a paced step ended after `holds` in-between frames
static void paced_step_done(int holds) {
    const int n = g_step_capacity;
    g_paced_holds += holds;
    g_paced_possible += n;
    g_paced_steps++;
    static unsigned recentHolds = 0, recentPossible = 0, recentN = 0;
    recentHolds += holds;
    recentPossible += n;
    if (++recentN == 60 && recentPossible > 0) {
        g_paced_share.store(recentHolds / float(recentPossible), std::memory_order_relaxed);
        recentHolds = recentPossible = recentN = 0;
    }
    if (g_paced_steps >= 300) {
        if (performance_reports() && g_paced_possible > 0 && n > 1)
            LOG("[interp] paced: %.0f%% of in-between frames drawn (in-between pass %.1f ms; %.2f of %d planned per step)",
                100.0 * g_paced_holds / double(g_paced_possible), std::chrono::duration<double, std::milli>(g_hold_avg).count(),
                g_paced_planned / double(g_paced_steps), n);
        else if (performance_reports() && g_paced_possible > 0)
            LOG("[interp] paced: %.0f%% of in-between frames drawn (in-between pass %.1f ms)", 100.0 * g_paced_holds / double(g_paced_possible),
                std::chrono::duration<double, std::milli>(g_hold_avg).count());
        g_paced_possible = g_paced_holds = g_paced_steps = g_paced_planned = 0;
    }
}
// end of every pass with a 60 fps mode on (phase 0: the logic pass): g_hold_next = another hold
// pass of this step follows (always, unless paced and it does not fit before the next step is due;
// pacing::next_pass). At 60 fps (one in-between pass, the record pass) it is drawn when it fits,
// otherwise dropped.
static void after_pass(int phase) {
    const bool pacing = paced() && interp_on();
    if (phase > 0) g_step_holds++;
    if (phase >= g_step_n) {  // the record pass ended the step
        g_resume_step = false;
        g_hold_next = false;
        if (pacing) {
            paced_step_done(g_step_holds);
            g_wait_step = true;  // the next step waits for its time (fewer passes than planned would make the step short)
        }
        return;
    }
    if (!pacing) { g_hold_next = true; g_wait_step = g_drop_lead = false; return; }
    if (phase > 0) g_last_hold = pace_clock::now();
    const auto now = pace_clock::now();
    using ns = std::chrono::nanoseconds;
    auto elapsed = std::chrono::duration_cast<ns>(now - g_last_logic).count();
    // 60 fps (one in-between frame, its record pass ends the step): the logic pass is not over at this
    // point, and the next step may start up to a frame late (pacing::logic_pass_elapsed). (At 120/240
    // fps the later passes of a step see the time the earlier ones took, and a step planned with one
    // in-between frame there keeps the rule it was measured with.)
    auto budget = kPacedBudget.count();
    // (40 fps: a step with its in-between frame, the rule of 60 fps over its two frames of 25 ms)
    const bool oneFrame = phase == 0 && g_step_n == 1 && in_between() == 1;
    if (oneFrame) {
        elapsed = pacing::logic_pass_elapsed(ns(now.time_since_epoch()).count(), ns((g_last_entry + g_slept).time_since_epoch()).count(),
                                             ns(g_last_logic.time_since_epoch()).count(), std::chrono::duration_cast<ns>(g_logic_avg).count());
        budget = fps40() ? pacing::one_frame_budget_40(ns(vsync_tick()).count(), std::chrono::duration_cast<ns>(g_logic_avg).count(), ns(kPacedStep).count())
                         : pacing::one_frame_budget(ns(kPacedStep).count(), ns(vsync_tick()).count());
    }
    // After a long time without in-between passes their cost is stale (the scene may be lighter
    // now): one probe, if one vsync tick fits; its duration replaces the estimate.
    const bool probe = phase == 0 && (now - g_last_hold >= kProbeAfter || (g_probe_retry != pace_clock::time_point{} && now >= g_probe_retry));
    const auto pass = std::chrono::duration_cast<std::chrono::nanoseconds>(probe ? vsync_tick() : g_hold_avg).count();
    switch (pacing::next_pass(phase, g_step_n, elapsed, pass, budget)) {
    case pacing::Next::kHold:
        g_hold_next = true;
        g_probe = probe;
        if (probe) g_probe_retry = {};
        g_last_hold = now;
        break;
    case pacing::Next::kRecord:  // blended hold passes that do not fit are skipped (the next hold pass increments g_phase)
        trace_step(now, g_last_logic, char('0' + std::min(phase, 9)));
        g_hold_next = true;
        g_phase = pacing::skip_to(phase, g_step_n, elapsed, pass, budget);
        g_last_hold = now;
        break;
    default:  // the rest of the step's in-between passes (and its record pass) are dropped
        trace_step(now, g_last_logic, 'D');
        g_hold_next = false;
        // (60 fps: the next logic pass starts at once and keeps what the skip saved as a lead)
        g_wait_step = pacing::wait_after_drop(oneFrame);
        g_drop_lead = !g_wait_step;
        paced_step_done(g_step_holds);
        break;
    }
}
}  // namespace interp

namespace mods { void cheats_service(); }  // mods/cheats.cpp
namespace mods { void play_context_step(); }  // mods/play_context.cpp

// debug: WWHD_INTERP_PASS_STATS=1 adds to the 300-step log the main thread's CPU time per pass
// (thread CPU time, so waits for the vsync and the GPU are left out): logic passes, blended hold
// passes and record passes - the cost of each in-between frame at 120/240 fps
namespace interp {
namespace {
struct PassCpu {
    double ms[3] = {0, 0, 0};
    uint32_t n[3] = {0, 0, 0};
};
PassCpu g_pass_cpu;
bool pass_stats() {
    static const bool on = getenv("WWHD_INTERP_PASS_STATS") != nullptr;
    return on;
}
double thread_cpu_ms() { return rprof::thread_cpu_ns() / 1e6; }
// around one pass of the per-frame function: kind 0 logic, 1 blended hold, 2 record
struct PassTimer {
    int kind;
    double t0;
    explicit PassTimer(int k) : kind(k), t0(pass_stats() ? thread_cpu_ms() : 0) {}
    ~PassTimer() {
        if (!pass_stats()) return;
        g_pass_cpu.ms[kind] += thread_cpu_ms() - t0;
        g_pass_cpu.n[kind]++;
    }
};
std::string pass_cpu_report() {
    if (!pass_stats()) return {};
    char b[160];
    auto avg = [](int k) { return g_pass_cpu.n[k] ? g_pass_cpu.ms[k] / g_pass_cpu.n[k] : 0.0; };
    snprintf(b, sizeof b, "; main thread CPU per pass: logic %.2f ms, blended hold %.2f ms (%u), record %.2f ms", avg(0), avg(1),
             g_pass_cpu.n[1], avg(2));
    g_pass_cpu = PassCpu{};
    return b;
}
}  // namespace
}  // namespace interp

static void fader_between_passes();  // debug (WWHD_INTERP_FADER_TRACE), below
namespace interp {
// The game's other sead tasks (ResLoadTask: archive and stage loads) calc in the same frame as the
// per-frame function, before it: this frame is a hold pass (frame interpolation, not true 60)
bool frame_is_hold() { return interp_on() && !true60::enabled() && g_hold_next; }
}  // namespace interp
extern "C" void hook_0203593C(Cpu* c) {
    using namespace interp;
    // Opt-in countdown trace: scalar timing measurements only, no save or asset data.
    struct CountdownTrace {
        bool hold;
        ~CountdownTrace() {
            static FILE* out = getenv("WWHD_COUNTDOWN_TRACE") ? fopen(getenv("WWHD_COUNTDOWN_TRACE"), "w") : nullptr;
            if (!out) return;
            const uint32_t timer = ld32(GD(0x1046F0B0) + 0x5CF0);
            if (timer < mem::kMem2Start || timer >= mem::kMem2End - 0x136) return;
            static bool paced = false;
            if (!paced && getenv("WWHD_COUNTDOWN_REALTIME")) {
                gx2::set_uncapped(false);
                paced = true;
            }
            const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            fprintf(out, "%llu %llu %d %.6f %u %u %u %u\n", (unsigned long long)g_passes,
                    (unsigned long long)g_logic_steps, int(hold), wall, ld32(timer + 0x114),
                    unsigned(ld8(timer + 0x124)), unsigned(ld8(timer + 0x122)), ld32(timer + 0x10C));
            fflush(out);
            static bool captured = false;
            if (!captured && !g_hold_next) {
                const char* capture = getenv("WWHD_COUNTDOWN_CAPTURE");
                unsigned frames = 0; int n = 0;
                if (capture && sscanf(capture, "%u:%n", &frames, &n) == 1 && n && capture[n] &&
                    ld32(timer + 0x10C) == frames) {
                    // Tests can align the capture with the painter's delayed draw lists.
                    const char* ahead = getenv("WWHD_COUNTDOWN_CAPTURE_AHEAD");
                    render::request_tv_dump(capture + n, ahead ? std::clamp(atoi(ahead), 1, 16) : 1);
                    captured = true;
                }
            }
        }
    } countdown_trace{enabled() && g_hold_next};
    // (a 40 fps blended record pass ended: its records count from here, after it has been painted,
    // and the values it left blended are exact again before the next step's logic)
    if (g_record_pending) {
        g_record_passes++;
        g_record_pending = false;
    }
    if (!g_exact_writes.empty()) put_back_exact();
    if (!g_tev_kept.empty()) tevstr_put_back();
    if (!g_actor_kept.empty() || !g_model_kept.empty()) actors_put_back();
    clipper_put_back();
    // (the step's camera drawn blended last: exact again before the next step's logic)
    if (g_cam_exact_pending && (!cam_exact_back_on() || !enabled() || !g_hold_next)) {
        g_cam_exact_pending = false;
        // (only the words the blended camera_draw changed, where nothing else changed them since; the
        // same camera process: a scene change may have deleted it meanwhile)
        const bool same = g_exact_cam_addr && g_cam_blended_img.size() == g_exact_cam_img.size() &&
                          memcmp(ppc_ptr(g_exact_cam_addr + 4), g_exact_cam_img.data() + 4, 4) == 0 &&     // process id
                          memcmp(ppc_ptr(g_exact_cam_addr + 0x10), g_exact_cam_img.data() + 0x10, 4) == 0;  // profile
        if (same) {
            uint8_t* m = (uint8_t*)ppc_ptr(g_exact_cam_addr);
            for (size_t i = 0; i + 4 <= g_exact_cam_img.size(); i += 4)
                if (memcmp(m + i, g_cam_blended_img.data() + i, 4) == 0 && memcmp(m + i, g_exact_cam_img.data() + i, 4) != 0)
                    memcpy(m + i, g_exact_cam_img.data() + i, 4);
        }
        for (int i = 0; i < 16; i++) st32(GD(0x104B45C0) + kJ3dCamWords[i], g_exact_j3d_words[i]);
        if (window_exact_on())
            for (int i = 0; i < kWindowWords; i++)
                if (ld32(window_ea() + 4 * i) == g_blended_window[i]) st32(window_ea() + 4 * i, g_exact_window[i]);
    }
    if (!g_models_before.empty()) g_models_before.clear();
    fader_between_passes();
    perf::game_frame();
    fx_pass_start();
    if (enabled() && !g_hold_next) fx_anm_settle(c);  // (a logic pass follows)
    ss::service(c);  // save states: exact values are back in guest memory, all other threads idle
    interp::NativeResumeBoundaryIssuer::after_service(c); // before any resumed native/game service
    mods::cheats_service();
    // the item ring: its equip, the game's pace while open (full logic passes, and true 60's half passes,
    // which also run Link), the ring drawn
    mods::item_ring_service(c, !enabled() || !g_hold_next, enabled() && g_hold_next && true60::enabled());
    mods::context_touch_service();  // tests: the context buttons' assets export (WWHD_CONTEXT_ASSETS)
    mods::hud_service();  // the boost bar and climb wheel fade out over the game's menus, events, messages
    const bool was_fast_forward = mods::fast_forward_active();
    mods::fast_forward_service();
    const bool ff_transition = was_fast_forward != mods::fast_forward_active();
    g_passes++;
    // test aid: WWHD_INTERP_AT_STEP=n switches interpolation on after n frames
    static uint64_t passes = 0;
    static const uint64_t at = getenv("WWHD_INTERP_AT_STEP") ? strtoull(getenv("WWHD_INTERP_AT_STEP"), nullptr, 10) : 0;
    if (at && ++passes == at) set_enabled(true);
    static uint64_t at60 = getenv("WWHD_TRUE60_AT_STEP") ? strtoull(getenv("WWHD_TRUE60_AT_STEP"), nullptr, 10) : 0;
    static uint64_t passes60 = 0;
    if (at60 && ++passes60 == at60) set_mode(2);
    if (ff_transition) restart_fast_forward_pacing(mods::fast_forward_active());
    log_paced_start();
    paced_pass_start();
    true60::new_pass();
    true60::pass_begin(ff_transition || !enabled() || !g_hold_next);  // full pass: take back Link's half-pass preview
    if (ff_transition) {
        for (auto& p : g_prev) p = Prev{};
        g_models.clear();
        g_record_passes += 8;
        g_hold_next = false;
        g_phase = 0;
        g_step_n = 1;
        g_steps40 = 0;
        g_frac40 = {};
        fx_ss_reset();
        reticle_reset();
    }
    if (!enabled() || !g_hold_next) {
        g_logic_steps++;
        true60_test::logic_step(g_logic_steps);
        mods::play_context_step();  // once per full logic step (the context buttons' reading)
    }
    if (!enabled()) {
        g_hold_next = false;
        g_phase = 0;
        g_step_n = 1;
        {
            PassTimer timer(0);
            g_in_frame = true;
            f_0203593C_orig(c);
            g_in_frame = false;
        }
        static unsigned stats_steps = 0;
        if (pass_stats() && ++stats_steps % 300 == 0) LOG("[interp]%s", pass_cpu_report().c_str());
        return;
    }
    static uint64_t frames = 0;  // passes drawn with interpolation on (frames per step in the log)
    frames++;
    {
        std::lock_guard<std::mutex> lk(g_ubo_mu);
        g_ubo_done.clear();  // (an update of the last pass the job never got to)
    }
    if (g_hold_next) {
        // hold pass: the per-frame function runs, but its children only per kHoldRun and the loop
        // body draws without logic (blended at pass_t() before the record pass)
        g_phase++;
        if (true60::enabled()) g_step_n = 1;  // (switched to true 60 within a step)
        g_hold = true;
        g_hold_frame = true;
        PassTimer timer(record_pass() ? 2 : 1);
        if (record_pass()) {
            g_record_passes++;
            if (g_step40) g_frac40.record();
        } else {
            fx_hold_blend(pass_t());  // the effects blended at logic time, at this pass's fraction
        }
        g_in_frame = true;
        f_0203593C_orig(c);
        g_in_frame = false;
        g_hold_frame = false;
        after_pass(g_phase);
        g_hold = false;
        return;
    }
    g_phase = 0;
    g_step_n = plan_step();
    // a step without an in-between frame (40 fps): its logic pass is also its record pass (blended:
    // its records count from the next pass on)
    g_record_logic = g_step_n == 0;
    if (g_record_logic) {
        if (g_exact_step) g_record_passes++;
        else g_record_pending = true;
        g_previous_record = true;
    }
    {
        PassTimer timer(0);
        g_in_frame = true;
        f_0203593C_orig(c);  // logic pass (the loop body hook marks it)
        g_in_frame = false;
    }
    g_record_logic = false;
    after_pass(0);  // g_hold_next: an in-between pass follows (always, unless paced)
    static uint64_t n = 0, t0 = timebase::now(), f0 = 0;
    if (++n % 300 == 0) {
        uint64_t t = timebase::now();
        if (performance_reports())
            LOG("[interp] %.1f logic steps/s (%s, %.2f frames per step); models per step: %.1f blended, %.1f new, %.1f cut (%.2f hidden joints shown)%s",
                300.0 * timebase::kTicksPerSec / (double)(t - t0), mode_name(), (frames - f0) / 300.0, g_mstats.blended / 300.0,
                g_mstats.fresh / 300.0, g_mstats.cut / 300.0, g_mstats.shown / 300.0, pass_cpu_report().c_str());
        t0 = t;
        f0 = frames;
        g_mstats.blended = g_mstats.fresh = g_mstats.cut = g_mstats.shown = 0;
        for (auto it = g_models.begin(); it != g_models.end();)  // models no longer drawn
            it = it->second.pass + 2 < g_record_passes ? g_models.erase(it) : std::next(it);
    }
}

// The pass whose draw lists are painted. The painter draws a pass's lists (J3DPacket draws: trails,
// sky sprites) after the per-frame function, before the next pass starts (g_passes is still the
// pass's): their hooks take the fraction the pass was drawn at from here.
namespace interp {
namespace {
struct Drawn { uint64_t pass = ~0ull; float t = 1.0f; bool record = false, record_blended = false; } g_drawn;
void note_drawn() {
    g_drawn = Drawn{g_passes, pass_t(), record_pass(), record_blended()};
    mods::hud_drawn(g_drawn.t, g_logic_steps, interp_on() && !true60::enabled());  // (the mod HUDs of this frame)
}
}  // namespace
// blend fraction of the pass being drawn or painted (1: exact, or interpolation off)
float display_t() {
    if (!enabled()) return 1.0f;
    if (g_in_frame) return pass_t();
    return g_drawn.pass == g_passes ? g_drawn.t : 1.0f;
}
// the pass being drawn or painted is the step's record pass (exact, the next step's "before")
bool display_record() {
    if (!enabled()) return false;
    if (g_in_frame) return record_pass();
    return g_drawn.pass == g_passes && g_drawn.record;
}
// the pass being drawn or painted is a 40 fps blended record pass (record_blended); its records are
// stamped display_record_stamp()
bool display_record_blended() {
    if (!enabled()) return false;
    if (g_in_frame) return record_blended();
    return g_drawn.pass == g_passes && g_drawn.record_blended;
}
uint64_t display_record_stamp() { return g_record_passes + (display_record_blended() ? 1 : 0); }
}  // namespace interp

// main loop body (inside the per-frame function): logic pass -> camera drawn blended;
// hold pass -> only fapGm_Execute, whose logic parts are skipped (hooks below); blended hold
// passes draw the camera blended too
extern "C" void hook_025F172C(Cpu* c) {
    using namespace interp;
    if (g_hold_frame) {
        g_blend_draw = blended_hold();
        f_025D42EC(c);
        note_drawn();
        g_blend_draw = false;
        return;
    }
    g_logic_pass = enabled() && !g_exact_step;  // (an exact step draws like interpolation off)
    g_blend_draw = g_logic_pass;
    f_025F172C_orig(c);
    note_drawn();
    g_logic_pass = false;
    g_blend_draw = false;
}

// children of the per-frame function on hold passes: run (bit set) or skip.
// debug: WWHD_HOLD_RUN=mask overrides the default
// Only calls made directly by the per-frame function (return address inside 0203593C..02035A78) are
// held back; the same functions are also called from elsewhere (e.g. 025EE048 as a getter inside
// drawing code), and those calls must always run.
static Cpu* g_hold_child_cpu = nullptr;
static bool called_from_frame_function() {
    // the per-frame function, in this build (runtime/include/guest_addr.h)
    static const uint32_t lo = GC(0x0203593C), hi = GC(0x02035A78);
    uint32_t lr = g_hold_child_cpu ? g_hold_child_cpu->lr : 0;
    return lr > lo && lr < hi;
}
static bool hold_skip_child(int i) {
    // children 8-14 (after the loop body: frame setup, lighting, display) run; 1, 4, 5 (HD menus and
    // systems) and 7 are held. Child 7 (0270870C, hdSceneBoot_c::execute, called while the boot
    // scene exists) plays the start-up loading screen (Boot_00: its parts' states and animations);
    // run on every pass it played 2x/4x/8x too fast at 60/120/240 fps (issue #300). Its draw (the
    // scene's draw delegate) still runs on every pass.
    // Child 0 always runs: it only reads a system state, and a non-zero result makes the per-frame
    // function skip the whole frame (a skipped call would leave its argument, a pointer, in r3, so
    // hold passes drew nothing and every step was shown twice).
    // Also run: 2-3 (025EE048/02756170: heap setup the drawing needs) and 6 (0273841C: clears the
    // light counts of the HD light manager at *101F8A20 (+0x10..+0x1C), which dKy_setLight (child 8)
    // fills via 0273862C and child 9 hands to the renderer; held, every point light was added a
    // second time on hold passes: candle light twice as bright on every other frame at 60 fps).
    static const uint32_t run = (getenv("WWHD_HOLD_RUN") ? (uint32_t)strtoul(getenv("WWHD_HOLD_RUN"), nullptr, 0) : 0x7F4C) | 1;
    return interp::g_hold_frame && !(run & (1u << i)) && called_from_frame_function();
}
// debug: WWHD_INTERP_FADER_TRACE=n logs n changes of the HD display's fader object (*101F86B4 +0x440,
// the stage fade dOvlpFd_startFadeInHD starts): which per-frame child, or the time between passes
// (painter), changed which words
namespace {
int g_fader_trace = getenv("WWHD_INTERP_FADER_TRACE") ? atoi(getenv("WWHD_INTERP_FADER_TRACE")) : 0;
uint32_t g_fader_words[64];
uint32_t fader_object() {
    const uint32_t d = ld32(GD(0x101F86B4));
    return d >= 0x10000000 && d < 0x50000000 ? ld32(d + 0x440) : 0;
}
void fader_snap() {
    const uint32_t f = fader_object();
    for (int i = 0; i < 64; i++) g_fader_words[i] = f >= 0x10000000 && f < 0x50000000 ? ld32(f + 4 * i) : 0;
}
void fader_diff(const char* who) {
    const uint32_t f = fader_object();
    if (f >= 0x10000000 && f < 0x50000000 && g_fader_trace > 0) {
        std::string o;
        for (int i = 0; i < 64; i++) {
            const uint32_t w = ld32(f + 4 * i);
            if (w == g_fader_words[i]) continue;
            char b[48];
            snprintf(b, sizeof b, " +%02X:%08X->%08X", 4 * i, g_fader_words[i], w);
            o += b;
        }
        if (!o.empty()) {
            g_fader_trace--;
            LOG("[fader] step %llu pass %llu %s %s (%08X):%s", (unsigned long long)interp::g_logic_steps, (unsigned long long)interp::g_passes,
                interp::g_hold ? "hold" : "logic", who, f, o.c_str());
        }
    }
    fader_snap();
}
struct FaderWatch {
    const char* who;
    explicit FaderWatch(const char* w) : who(w) { if (g_fader_trace > 0) fader_diff("before"); }
    ~FaderWatch() { if (g_fader_trace > 0) fader_diff(who); }
};
}  // namespace
static void fader_between_passes() { if (g_fader_trace > 0) fader_diff("between passes (painter)"); }
extern "C" void hook_02032FE8(Cpu* c) { FaderWatch fw("child 0"); g_hold_child_cpu = c; if (!hold_skip_child(0)) f_02032FE8_orig(c); }
extern "C" void hook_020357CC(Cpu* c) { FaderWatch fw("child 1"); g_hold_child_cpu = c; if (!hold_skip_child(1)) f_020357CC_orig(c); }
extern "C" void hook_025EE048(Cpu* c) { FaderWatch fw("child 2"); g_hold_child_cpu = c; if (!hold_skip_child(2)) f_025EE048_orig(c); }
extern "C" void hook_02756170(Cpu* c) { FaderWatch fw("child 3"); g_hold_child_cpu = c; if (!hold_skip_child(3)) f_02756170_orig(c); }
extern "C" void hook_02617AF4(Cpu* c) { FaderWatch fw("child 4"); g_hold_child_cpu = c; if (!hold_skip_child(4)) f_02617AF4_orig(c); }
extern "C" void hook_02614F74(Cpu* c) { FaderWatch fw("child 5"); g_hold_child_cpu = c; if (!hold_skip_child(5)) f_02614F74_orig(c); }
extern "C" void hook_0273841C(Cpu* c) { FaderWatch fw("child 6"); g_hold_child_cpu = c; if (!hold_skip_child(6)) f_0273841C_orig(c); }
extern "C" void hook_0270870C(Cpu* c) { FaderWatch fw("child 7"); g_hold_child_cpu = c; if (!hold_skip_child(7)) f_0270870C_orig(c); }
namespace interp { void light_trace_add(const char* fmt, ...); }
// dKy_setLight (child 8, every pass) keeps state across calls: the candle flicker target (static,
// smoothed with cLib_addCalc2 toward a random value per call), the eflight target and the main
// light position (lightStatusPt->mPos, cLib_addCalc). On hold passes it would smooth a second time
// per logic step (light changing on every drawn frame, with a different amplitude than at 30 fps).
// The state from before the logic pass's call is put back first, so with the random numbers
// replayed (interp_fx.cpp) the hold pass gets exactly the logic pass's light.
namespace {
const uint32_t kSetLightTarget = GD(0x101E8EC8), kSetLightEfTarget = GD(0x101E8ECC), kLightStatusPt = GD(0x101E8CC8);
struct SetLightState {
    uint64_t step = ~0ull;
    uint32_t target, ef_target, status, pos[3];
} g_setlight;
}  // namespace
extern "C" void hook_0255E854(Cpu* c) {
    FaderWatch fw("child 8");
    g_hold_child_cpu = c;
    if (hold_skip_child(8)) return;
    if (interp::enabled() && called_from_frame_function()) {
        SetLightState& s = g_setlight;
        uint32_t st = ld32(kLightStatusPt);
        if (!interp::g_hold_frame) {
            s.step = interp::g_logic_steps;
            s.target = ld32(kSetLightTarget);
            s.ef_target = ld32(kSetLightEfTarget);
            s.status = st;
            for (int i = 0; i < 3; i++) s.pos[i] = st ? ld32(st + 4 * i) : 0;
        } else if (s.step == interp::g_logic_steps && s.status == st) {
            st32(kSetLightTarget, s.target);
            st32(kSetLightEfTarget, s.ef_target);
            for (int i = 0; i < 3 && st; i++) st32(st + 4 * i, s.pos[i]);
        }
    }
    // true 60: the hold pass ran with the preview camera; the next full pass continues from the
    // state the logic pass left, as the 30 fps game's next step does
    static SetLightState after_logic;
    if (true60::enabled() && called_from_frame_function() && !interp::g_hold_frame && after_logic.step + 1 == interp::g_logic_steps) {
        uint32_t st2 = ld32(kLightStatusPt);
        if (after_logic.status == st2) {
            st32(kSetLightTarget, after_logic.target);
            st32(kSetLightEfTarget, after_logic.ef_target);
            for (int i = 0; i < 3 && st2; i++) st32(st2 + 4 * i, after_logic.pos[i]);
        }
    }
    f_0255E854_orig(c);
    if (true60::enabled() && called_from_frame_function() && !interp::g_hold_frame) {
        uint32_t st2 = ld32(kLightStatusPt);
        after_logic.step = interp::g_logic_steps;
        after_logic.target = ld32(kSetLightTarget);
        after_logic.ef_target = ld32(kSetLightEfTarget);
        after_logic.status = st2;
        for (int i = 0; i < 3; i++) after_logic.pos[i] = st2 ? ld32(st2 + 4 * i) : 0;
    }
    uint32_t st = ld32(GD(0x101E8CC8));  // lightStatusPt
    interp::light_trace_add(" setLight t%.2f r%u p(%.1f,%.1f,%.1f)", ldf32(GD(0x101E8EC8)), st ? ld8(st + 0x18) : 0, st ? ldf32(st) : 0.0,
                            st ? ldf32(st + 4) : 0.0, st ? ldf32(st + 8) : 0.0);
}
extern "C" void hook_02738438(Cpu* c) {
    FaderWatch fw("child 9");
    g_hold_child_cpu = c;
    if (hold_skip_child(9)) return;
    uint32_t o = c->r[3];
    interp::light_trace_add(" hdl[%08X %08X %08X %08X]", ld32(o + 0x10), ld32(o + 0x14), ld32(o + 0x18), ld32(o + 0x1C));
    f_02738438_orig(c);
}
namespace interp { void shadow_cascades_launch(); }
// child 10 starts the shadow cascade job (shadow_cascades_launch)
extern "C" void hook_0278FEBC(Cpu* c) {
    FaderWatch fw("child 10");
    g_hold_child_cpu = c;
    if (hold_skip_child(10)) return;
    interp::shadow_cascades_launch();
    f_0278FEBC_orig(c);
}
extern "C" void hook_027199C0(Cpu* c) { FaderWatch fw("child 11"); g_hold_child_cpu = c; if (!hold_skip_child(11)) f_027199C0_orig(c); }
extern "C" void hook_02618940(Cpu* c) { FaderWatch fw("child 12"); g_hold_child_cpu = c; if (!hold_skip_child(12)) f_02618940_orig(c); }
// Child 13 (02728A74) advances the HD display's fader (the stage fades) on every pass it runs, so
// they ran 2x/4x/8x too fast with frame interpolation (a 26-frame stage fade-in: 0.22 s at 120 fps
// instead of 0.87 s). Gated (interp_fade.h fader_call): advanced once per logic step, shown at the
// pass's fraction. Fader object: *101F86B4 (the display) +0x440.
// debug: WWHD_INTERP_FADER=0 turns the gate off
extern "C" void hook_02728A74(Cpu* c) {
    FaderWatch fw("child 13");
    g_hold_child_cpu = c;
    if (hold_skip_child(13)) return;
    static const bool off = getenv("WWHD_INTERP_FADER") && !atoi(getenv("WWHD_INTERP_FADER"));
    const uint32_t f = fader_object();
    if (!interp::enabled() || off || !called_from_frame_function() || f < 0x10000000 || f >= 0x50000000) {
        f_02728A74_orig(c);
        return;
    }
    // (the start delay, +0x448 of the fader manager, counts down once per logic step too)
    static const bool wipe_off = getenv("WWHD_INTERP_WIPE_FADE") && !atoi(getenv("WWHD_INTERP_WIPE_FADE"));
    const uint32_t mgr = ld32(GD(0x101F86B4));
    const uint32_t delay = ld32(mgr + 0x448);
    static interp::fade::FaderGate gate;
    interp::fade::Fader v;
    for (int i = 0; i < 3; i++) v.w[i] = ld32(f + 4 * i);
    auto put = [&](const interp::fade::Fader& x) { for (int i = 0; i < 3; i++) st32(f + 4 * i, x.w[i]); };
    interp::fade::fader_call(gate, interp::g_logic_steps, !interp::g_hold_frame, interp::pass_t(), v, [&](interp::fade::Fader& x) {
        put(x);
        f_02728A74_orig(c);
        for (int i = 0; i < 3; i++) x.w[i] = ld32(f + 4 * i);
    });
    put(v);
    for (int i = 0; i < 3; i++)
        if (v.w[i] != gate.after.w[i]) interp::exact_after_pass(f + 4 * i, gate.after.w[i], 4);
    if (interp::g_hold_frame && !wipe_off) st32(mgr + 0x448, delay);
    // debug: WWHD_INTERP_FADE_PASSES=n logs n passes' shown fades (the task fader's words, the colour
    // fade of l_graphics) while a fade is on screen
    static int passes = getenv("WWHD_INTERP_FADE_PASSES") ? atoi(getenv("WWHD_INTERP_FADE_PASSES")) : 0;
    if (passes > 0) {
        const uint32_t g = GD(0x101F4808);
        const uint32_t col = ld32(g + 0x14);
        if ((v.w[0] != 0 && v.w[0] != 1) || (v.w[2] & 0xFF) || (col & 0xFF) || ld8(g + 0x1F) || ld8(GD(0x101D616C))) {
            passes--;
            const uint32_t w = GD(0x101D6160);
            LOG("[fadepass] step %llu pass %llu %s t %.3f task %08X %08X %08X delay %u | gph fade %u rate %.4f speed %.4f col %08X | gc on %u rate %.4f speed %.4f",
                (unsigned long long)interp::g_logic_steps, (unsigned long long)interp::g_passes, interp::g_hold_frame ? "hold " : "logic",
                interp::pass_t(), v.w[0], v.w[1], v.w[2], ld32(mgr + 0x448), ld8(g + 0x1F), (float)ldf32(g + 8),
                (float)ldf32(g + 0xC), col, ld8(w + 0xC), (float)ldf32(w), (float)ldf32(w + 4));
        }
    }
}
extern "C" void hook_02039834(Cpu* c) { FaderWatch fw("child 14"); g_hold_child_cpu = c; if (!hold_skip_child(14)) f_02039834_orig(c); }


// logic parts of fpcM_Management / fapGm_Execute, skipped on hold passes
// debug: WWHD_INTERP_RUN=mask runs selected parts on hold passes too
// (1 execute, 2 delete, 4 priority, 8 create, 16 fapGm_After, 32 counter, 64 HD UI screens)
static bool skip(int bit) {
    static const int run = getenv("WWHD_INTERP_RUN") ? atoi(getenv("WWHD_INTERP_RUN")) : 0;
    return true60::skip_step_part(interp::g_hold, true60::enabled(), run, bit);
}
static bool g_in_execute = false;  // inside fpcEx_Handler (actor Execute): logic, not drawing
namespace mods { void after_execute(Cpu* c, uint32_t execute_fn); }  // mods/turbo.cpp
namespace editor { void logic_service(Cpu* c); void camera_service(Cpu* c, bool logic_pass); }  // editor/editor_game.cpp: the world editor (live actors, fly camera)
extern "C" void hook_025DE788(Cpu* c) {
    if (skip(1) && !true60::enabled()) return;  // true 60: the per-process gate decides (true60.cpp)
    interp::record_executed_step();
    g_in_execute = true;
    uint32_t execute_fn = c->r[3];
    if(!interp::g_hold_frame)guestmods::maphud::begin_logic(c,double(true60::dt())/30.0);
    dsweep::before_execute(c);  // the setup's actor sweep (WWHD_DSWEEP): its actors do not run their logic
    f_025DE788_orig(c);
    dsweep::after_execute(c);
    mods::after_execute(c, execute_fn);  // quick doors / fast scene changes: extra steps (full passes only)
    if (!interp::g_hold_frame) {guestmods::frame(interp::g_logic_steps);guestmods::logic_service(c);editor::logic_service(c);mods::packages::frame(interp::g_logic_steps);}
    editor::camera_service(c, !interp::g_hold_frame);  // the fly camera: every logic pass, and true 60's 60 Hz passes
    if(!interp::g_hold_frame)guestmods::maphud::finish_logic();
    guestmods::draw_frame(c,interp::executed_steps());
    g_in_execute = false;
}
extern "C" void hook_025DE024(Cpu* c) { if (!skip(2)) f_025DE024_orig(c); }
extern "C" void hook_025E0EE4(Cpu* c) { if (skip(4)) c->r[3] = 1; else f_025E0EE4_orig(c); }
extern "C" void hook_025DDCEC(Cpu* c) { if (skip(8)) c->r[3] = 1; else f_025DDCEC_orig(c); }
extern "C" void hook_025D42C4(Cpu* c) { if (!skip(16)) f_025D42C4_orig(c); }
extern "C" void hook_0200E6EC(Cpu* c) { if (!skip(32)) f_0200E6EC_orig(c); }
// WWHD's addition at the end of fpcM_Management (025DFA58): the HD UI manager (*101F8344) updates
// three of its screens (vtable +0x5C of the objects at +0x214, +0x1EC and +0x1E4; +0x1EC is the TV
// pause screen): state machines and frame-counted layout animations, i.e. logic. Run on every pass,
// they advanced once per drawn frame instead of once per logic step (their fades ran 2x/4x/8x as fast
// at 60/120/240 fps), and the TV pause screen, closed by the menu on a logic pass, reopened itself on
// the next in-between pass (the game still counted as paused until the next logic pass). The menu's
// "back to play" state checks the screen on logic passes only; with 3 or more in-between passes per
// step it always found the screen open again and waited forever: the menu would not close (issues
// #64, #74; #73, no control after an item-get message, looks like the same). Once per logic step, as
// at 30 fps (true 60 too: these screens are 30 Hz logic).
extern "C" void f_02715310_orig(Cpu* c);
extern "C" void hook_02715310(Cpu* c) {
    if (!skip(64)) f_02715310_orig(c);
    else countdown::refresh(c);
}

// Game Over's state machine is installed in the leaf process's Draw slot, so the
// Execute gate above does not hold it back. An extra draw can request the save
// window and then read its still-idle state before the next scene/UI Execute
// opens it. That commits the default "To Title" choice, even when the player
// subsequently selects Continue, leaving the scene waiting for a title reset.
// This callback only advances logic; the HD UI paints the screen separately.
// Keep its native return value on presentation passes without advancing it.
extern "C" void f_025499A0_orig(Cpu* c);
extern "C" void hook_025499A0(Cpu* c) {
    if (interp::enabled() && interp::g_hold) {
        c->r[3] = 1;
        return;
    }
    f_025499A0_orig(c);
}

namespace interp { bool exact_cull_on(); void edge_draw(const Cpu& call, Cpu* c, uint32_t actor); void actor_keep(uint32_t actor); }
// fopAc_Draw(fopAc_ac_c*): one-shot "not drawn this frame" requests (fopAcStts_NODRAW_e) made by
// the step's logic hold for all passes of the step (interp_nodraw.h; issue #126: in conversations
// the talk camera hides Link on logic frames only, so he flickered at 60/120/240 fps and true 60).
// debug: WWHD_INTERP_NODRAW=0 turns this off; WWHD_INTERP_NODRAW_TRACE=n logs n kept requests
extern "C" void f_025D4654_orig(Cpu* c);
static interp::OneShotNoDraw g_nodraw;
extern "C" void hook_025D4654(Cpu* c) {
    const uint32_t actor = c->r[3];
    const uint32_t status_in = ld32(actor + interp::OneShotNoDraw::kStatus);
    // On passes without logic the draw's culling result does not reach the actor: actor_condition
    // (+0x2E4: fopAcCnd_NODRAW, which executes read) keeps what the step's logic pass set. Those passes
    // cull with the camera they draw with (blended), and the record pass with shadow cascades set up
    // after the blended logic pass: at the next step's logic an actor could count as drawn or not
    // drawn where at 30 fps it did not. debug: WWHD_INTERP_EXACT_CULL=0 (as before)
    const bool keep_condition = interp::enabled() && interp::g_hold && interp::exact_cull_on() && !true60::enabled();
    const uint32_t condition = keep_condition ? ld32(actor + 0x2E4) : 0;
    const Cpu saved = *c;
    const bool kept = keep_condition && (condition & 0x04);
    // (saved before the step's one-shot no-draw request is made again, interp_nodraw.h before_draw;
    // issue #283) debug: WWHD_INTERP_NODRAW=0 turns the requests off
    static const bool off = getenv("WWHD_INTERP_NODRAW") && !atoi(getenv("WWHD_INTERP_NODRAW"));
    const uint32_t want = g_nodraw.before_draw(actor, status_in, interp::g_hold, interp::g_logic_steps, interp::enabled() && !off, kept,
                                               [actor] { interp::actor_keep(actor); });  // (not drawn by the step's logic pass)
    if (want != status_in) {
        st32(actor + interp::OneShotNoDraw::kStatus, want);
        static int trace = getenv("WWHD_INTERP_NODRAW_TRACE") ? atoi(getenv("WWHD_INTERP_NODRAW_TRACE")) : 0;
        if (trace > 0) {
            trace--;
            LOG("[interp] step %llu phase %d: actor %08X kept hidden (requested by the step's logic)",
                (unsigned long long)interp::g_logic_steps, interp::g_phase, actor);
        }
    }
    const uint32_t keep_drawing = interp::g_keep_drawing;
    if (kept) interp::g_keep_drawing = actor;  // (its models' matrices too: model_calc_before)
    const uint32_t status_seen = ld32(actor + interp::OneShotNoDraw::kStatus);
    if (!dsweep::hide_actor_draw(actor)) f_025D4654_orig(c);
    interp::g_keep_drawing = keep_drawing;
    // debug: WWHD_INTERP_LINK_DRAW_TRACE=path logs every draw of Link: pass, logic step, interpolation on,
    // hold pass, phase, the status the draw saw (fopAcStts_NODRAW_e 0x01000000), the condition it left
    // (fopAcCnd_NODRAW 0x04: not drawn), drawn, the status before this hook and after the draw
    static FILE* link_trace = getenv("WWHD_INTERP_LINK_DRAW_TRACE") ? fopen(getenv("WWHD_INTERP_LINK_DRAW_TRACE"), "w") : nullptr;
    if (link_trace && actor == ld32(GD(0x1046F0B0) + 0x5B34)) {
        const uint32_t cond_after = ld32(actor + 0x2E4);
        fprintf(link_trace, "pass %llu step %llu interp %d hold %d phase %d status %08X cond %08X drawn %d in %08X after %08X link %08X\n",
                (unsigned long long)interp::g_passes, (unsigned long long)interp::g_logic_steps, (int)interp::enabled(),
                (int)interp::g_hold, interp::g_phase, status_seen, cond_after, !(cond_after & 0x04), status_in,
                ld32(actor + interp::OneShotNoDraw::kStatus), actor);
        fflush(link_trace);
    }
    if (keep_condition) st32(actor + 0x2E4, condition);
    interp::edge_draw(saved, c, actor);
    dsweep::after_actor_draw(c, actor);  // debug D-sweep (WWHD_DSWEEP), off by default
}

namespace interp {
void nodraw_reset() { g_nodraw.reset(); }
// The pads are read at the end of every frame. JUTGamePad derives "pressed this frame" from
// consecutive reads, so a change first seen on the read after a logic pass would be spent on a pass
// without logic. While interpolating, that read repeats the previous sample (see VPADRead /
// KPADReadEx), so every change reaches the logic exactly once.
void trace_read(const char* who) {
    // debug: WWHD_TRACE_INPUT_PHASE=n logs n controller reads with the pass they happen in
    static int n = getenv("WWHD_TRACE_INPUT_PHASE") ? atoi(getenv("WWHD_TRACE_INPUT_PHASE")) : 0;
    if (n > 0 && enabled()) {
        n--;
        LOG("[interp] %s read: logic_pass=%d hold=%d hold_next=%d", who, g_logic_pass, g_hold, g_hold_next);
    }
}
bool hold_pass() { return g_hold; }
// full (30 Hz) logic steps so far: test scenarios run on game time, so frame-time hitches (which
// slow the frame-locked game down) don't shift the input against the game
uint64_t logic_steps() { return g_logic_steps; }
void record_executed_step() { g_executed_steps.fetch_add(1, std::memory_order_relaxed); }
uint64_t executed_steps() { return g_executed_steps.load(std::memory_order_relaxed); }
// pass state for the effect blending (interp_fx.cpp)
bool logic_pass() { return g_logic_pass; }
// inside the loop body of a blended pass (logic pass, or a blended hold pass at 120/240 fps)
bool blend_draw() { return g_blend_draw; }
bool in_execute() { return g_in_execute; }
// record passes so far: the generation of the step-stamped histories (models, effects); at 60 fps
// every hold pass is a record pass
uint64_t hold_pass_count() { return g_record_passes; }
uint64_t pass_count() { return g_passes; }  // every pass (per-pass bookkeeping)
const char* phase_name() { return !enabled() ? "interp off" : g_hold ? "IN-BETWEEN" : g_logic_pass ? "logic" : "other"; }
// true 60: the sticks are read fresh on every pass (60 Hz processes use them on the half passes);
// buttons still change on full passes only, so every press reaches the 30 Hz processes and menus
bool fresh_sticks() { return true60::enabled(); }
bool repeat_input() {
    static const bool off = getenv("WWHD_INTERP_NO_REPEAT") != nullptr;  // debug
    return enabled() && g_hold_next && !off;
}
}  // namespace interp

// Sound effect starts (JAIZelBasic::seStart core and the mDoAud_* start wrappers). Some sounds are
// started from drawing code (animation-linked effects); an in-between frame only redraws, so a
// sound started there would play a second time. Suppressed while the hold pass draws, and with true
// 60 on the whole half pass: its executes are previews that the next full pass takes back and runs
// again (true60.cpp), so every sound starts once, on the full pass of the 30 fps game's step.
// debug: WWHD_SE_STATS=1 logs calls per second by frame phase every 5 s
// debug: WWHD_SE_TRACE=path logs every start that is not suppressed: logic step, full pass (1/0),
// wrapper index, r4 (the sound id for the seStart wrappers), caller (true60 comparisons)
static void se_trace(int fn, Cpu* c) {
    static FILE* f = getenv("WWHD_SE_TRACE") ? fopen(getenv("WWHD_SE_TRACE"), "w") : nullptr;
    if (!f || interp::g_hold) return;
    fprintf(f, "%llu %d %d %08X %08X\n", (unsigned long long)interp::g_logic_steps, interp::g_hold ? 0 : 1, fn, c->r[4], c->lr);
    fflush(f);
}
static void se_stat(int fn) {
    static const bool on = getenv("WWHD_SE_STATS") != nullptr;
    if (!on) return;
    static uint32_t n[7][4];
    int ph = !interp::enabled() ? 0 : interp::g_hold ? 1 : interp::g_logic_pass ? 2 : 3;
    n[fn][ph]++;
    static uint64_t t0 = timebase::now();
    uint64_t t = timebase::now();
    if (t - t0 > 5 * timebase::kTicksPerSec) {
        static const char* fns[7] = {"core", "w1988", "w19CC", "w1A04", "w1A40", "w1A7C", "w1AA4"};
        char buf[700];
        int k = snprintf(buf, sizeof buf, "[se] starts per 5 s [off/in-between/logic/other]:");
        for (int f = 0; f < 7; f++)
            if (n[f][0] + n[f][1] + n[f][2] + n[f][3])
                k += snprintf(buf + k, sizeof buf - k, " %s=%u/%u/%u/%u", fns[f], n[f][0], n[f][1], n[f][2], n[f][3]);
        LOG("%s", buf);
        memset(n, 0, sizeof n);
        t0 = t;
    }
}
extern "C" void hook_0201EBA0(Cpu* c) { se_stat(0); se_trace(0, c); if (interp::g_hold) { c->r[3] = 0; return; } f_0201EBA0_orig(c); }
extern "C" void hook_025E1988(Cpu* c) { se_stat(1); se_trace(1, c); if (interp::g_hold) { c->r[3] = 0; return; } f_025E1988_orig(c); }
extern "C" void hook_025E19CC(Cpu* c) { se_stat(2); se_trace(2, c); if (interp::g_hold) { c->r[3] = 0; return; } f_025E19CC_orig(c); }
extern "C" void hook_025E1A04(Cpu* c) { se_stat(3); se_trace(3, c); if (interp::g_hold) { c->r[3] = 0; return; } f_025E1A04_orig(c); }
extern "C" void hook_025E1A40(Cpu* c) { se_stat(4); se_trace(4, c); if (interp::g_hold) { c->r[3] = 0; return; } f_025E1A40_orig(c); }
extern "C" void hook_025E1A7C(Cpu* c) { se_stat(5); se_trace(5, c); if (interp::g_hold) { c->r[3] = 0; return; } f_025E1A7C_orig(c); }
extern "C" void hook_025E1AA4(Cpu* c) { se_stat(6); se_trace(6, c); if (interp::g_hold) { c->r[3] = 0; return; } f_025E1AA4_orig(c); }

// The sound engine takes its listener from camera_draw. Feeding it the halfway camera as well as the
// exact one makes the listener hop every frame (Doppler/panning wobble: doubled-sounding effects),
// so only the exact camera (record pass, once per logic step) updates it.
// mDoAud_getCameraInfo(eye, viewMtx, id): the sound engine keeps these POINTERS and reads them from
// its own thread (panning/volume of positional sounds such as the waves). The camera's eye and
// j3dSys's view matrix alternate between the halfway and the exact camera at 60 fps, so the engine
// gets private copies instead, refreshed only from the exact camera.
// At 40 fps two steps of three have no exact frame (blended record passes): their camera, drawn
// blended, updates the listener (one update a step, each drawn 3/4 of a step after the previous
// one, as the frames).
extern "C" void hook_025E1B44(Cpu* c) {
    if (interp::g_cam_blended && !interp::record_blended()) return;
    if (!interp::enabled()) {
        f_025E1B44_orig(c);
        return;
    }
    const uint32_t eye = mem::fixed_slot(mem::kFixInterpEye), mtx = mem::fixed_slot(mem::kFixInterpMtx);
    for (int i = 0; i < 3; i++) st32(eye + 4 * i, ld32(c->r[3] + 4 * i));
    for (int i = 0; i < 12; i++) st32(mtx + 4 * i, ld32(c->r[4] + 4 * i));
    c->r[3] = eye;
    c->r[4] = mtx;
    f_025E1B44_orig(c);
}
extern "C" void hook_02027754(Cpu* c) { if (!interp::g_cam_blended) f_02027754_orig(c); }

// WWHD's audio frame callback (020315CC, from the same frame-callback table as the per-frame
// function) advances the sound engine one game step (JAIZelBasic::gframeProcess: sequences, effect
// processing). The frame loop calls it every displayed frame, so at 60 fps the engine ran at double
// rate: sequences such as the waves triggered twice as often and overlapped. Once per logic step.
extern "C" void hook_020315CC(Cpu* c) {
    using namespace interp;
    static uint64_t done_for = ~0ull;
    if (enabled()) {
        if (done_for == g_logic_steps) return;
        done_for = g_logic_steps;
    }
    f_020315CC_orig(c);
}

// The night sky's stars (dKyr_drawStar 02574144, from the star packet's draw, which runs when the
// pass's draw lists are painted, after the per-frame function) are placed around the camera's eye
// as it is at that moment: the exact step, as camera_draw put it back. The pass was drawn with the
// blended camera, so on blended frames the stars sat around another eye than the one they were seen
// from and jumped back and forth while the camera moved (issue #68, "stars on the sky at night").
// They are placed with the camera as the pass drew it. The same for the other sky sprites placed
// around the eye at paint time: the sun and moon (dKyr_drawSun 0256F1C4), the lens flare
// (dKyr_drawLenzflare 02570210, dKy_set_eyevect_calc2 from the eye) and the sea spray in the rain
// (dKyr_drawSibuki 02571B1C): they moved at 30 fps against the blended view.
// The sun and moon (the sun packet's position +0x98, the argument of dKyr_drawSun) and the lens
// flare's elements (the sunlenz packet's 9 positions +0xA4, and the sun's position for its rays) are
// placed by the environment's logic (dKyr_sun_move, 8000 units from the eye) around the eye of the
// step; painted with the pass's camera they sat a varying part of the step's camera motion off their
// place, a 30 Hz judder while Link moves or sails (issue #320). While they are painted they are put
// around the drawn eye with their offset from the exact eye (camera::sky_follow_eye), that offset
// drawn between the record pass's and the step's (camera::sky_offset_blend), and put back after.
namespace {
struct SkyPoints { uint32_t at; int n; };
constexpr int kSkyPoints = 16;
// per caller: the offsets from the eye on the last record pass (stamp: its hold pass count)
struct SkyHistory { uint64_t stamp = ~0ull; int n = 0; float offset[kSkyPoints][3]; };
// g_env_light (10475A68): the sun packet +0xA34, the sunlenz packet +0xA38 (GameCube offsets + 0x80)
uint32_t sun_packet() { return ld32(GD(0x10475A68) + 0xA34); }
uint32_t lenz_packet() { return ld32(GD(0x10475A68) + 0xA38); }
}  // namespace
static void with_drawn_camera(Cpu* c, void (*orig)(Cpu*), const char* what, std::initializer_list<SkyPoints> points = {},
                              SkyHistory* history = nullptr) {
    using namespace interp;
    if (!enabled()) {
        orig(c);
        return;
    }
    // debug: WWHD_INTERP_SKY_TRACE=n logs n draws whose camera was swapped (eye drawn vs exact)
    static int trace = getenv("WWHD_INTERP_SKY_TRACE") ? atoi(getenv("WWHD_INTERP_SKY_TRACE")) : 0;
    CamState exact[kCamSlots];
    bool swapped[kCamSlots] = {};
    // the game's camera (dComIfGp_getCamera(0): game info 1046F0B0 +0x5AF8), whose eye the sky's logic used
    const uint32_t game_cam = ld32(GD(0x1046F0B0) + 0x5AF8);
    int sky_slot = -1;
    for (int i = 0; i < kCamSlots; i++) {
        Prev& p = g_prev[i];
        if (!p.cam || p.drawn_pass != g_passes) continue;
        exact[i] = read_cam(p.cam);
        write_cam(p.cam, p.drawn);
        swapped[i] = true;
        if (p.cam == game_cam) sky_slot = i;
        if (trace > 0 && camera::dist(exact[i].eye, p.drawn.eye) > 1.0f) {
            trace--;
            LOG("[interp] %s: pass %llu drawn with eye %.1f %.1f %.1f (exact %.1f %.1f %.1f)", what, (unsigned long long)g_passes, p.drawn.eye[0],
                p.drawn.eye[1], p.drawn.eye[2], exact[i].eye[0], exact[i].eye[1], exact[i].eye[2]);
        }
    }
    uint32_t saved[kSkyPoints * 3];
    int n_saved = 0;
    if (sky_slot >= 0) {
        const float* drawn_eye = g_prev[sky_slot].drawn.eye;
        const float* exact_eye = exact[sky_slot].eye;
        // the step's record pass (not a 40 fps blended one) keeps its offsets; the passes of the next
        // step until its record pass (same hold pass count) draw between them and their own
        const bool record = display_record() && !display_record_blended();
        const bool blend = history && !record && !display_record_blended() && history->stamp == g_record_passes;
        const float t = display_t();
        int k = 0;
        for (const SkyPoints& sp : points)
            for (int j = 0; j < sp.n && k < kSkyPoints; j++, k++) {
                const uint32_t a = sp.at + 12 * j;
                float v[3];
                for (int i = 0; i < 3; i++) {
                    saved[n_saved++] = ld32(a + 4 * i);
                    v[i] = (float)ldf32(a + 4 * i) - exact_eye[i];  // the offset from the exact eye
                }
                if (record && history) for (int i = 0; i < 3; i++) history->offset[k][i] = v[i];
                else if (blend && k < history->n) camera::sky_offset_blend(v, history->offset[k], t);
                for (int i = 0; i < 3; i++) v[i] += exact_eye[i];
                camera::sky_follow_eye(v, drawn_eye, exact_eye);
                for (int i = 0; i < 3; i++) stf32(a + 4 * i, v[i]);
            }
        if (record && history) {
            history->stamp = g_record_passes;
            history->n = k;
        }
    }
    orig(c);
    n_saved = 0;
    if (sky_slot >= 0) {
        int k = 0;
        for (const SkyPoints& sp : points)
            for (int j = 0; j < sp.n && k < kSkyPoints; j++, k++)
                for (int i = 0; i < 3; i++) st32(sp.at + 12 * j + 4 * i, saved[n_saved++]);
    }
    for (int i = kCamSlots - 1; i >= 0; i--)
        if (swapped[i]) write_cam(g_prev[i].cam, exact[i]);
}
extern "C" void f_02574144_orig(Cpu* c);
extern "C" void f_0256F1C4_orig(Cpu* c);
extern "C" void f_02570210_orig(Cpu* c);
extern "C" void f_02571B1C_orig(Cpu* c);
extern "C" void hook_02574144(Cpu* c) { with_drawn_camera(c, f_02574144_orig, "stars"); }
// dKyr_drawSun(view, &sun packet +0x98, ...) from the sun packet's draw
extern "C" void hook_0256F1C4(Cpu* c) {
    const uint32_t sun = sun_packet();
    if (sun && c->r[4] == sun + 0x98) {
        static SkyHistory history;
        with_drawn_camera(c, f_0256F1C4_orig, "sun/moon", {{c->r[4], 1}}, &history);
        return;
    }
    static bool logged = false;
    if (!logged && interp::enabled()) {
        logged = true;
        LOG("[interp] sun/moon: drawn from %08X, not the sun packet's position (packet %08X): not moved with the drawn eye", c->r[4], sun);
    }
    with_drawn_camera(c, f_0256F1C4_orig, "sun/moon");
}
// dKyr_drawLenzflare(view, &sunlenz packet +0xA4 (9 positions), ...) from the sunlenz packet's draw;
// its rays start at the sun's position
extern "C" void hook_02570210(Cpu* c) {
    const uint32_t sun = sun_packet(), lenz = lenz_packet();
    static SkyHistory history;
    if (sun && lenz && c->r[4] == lenz + 0xA4) with_drawn_camera(c, f_02570210_orig, "lens flare", {{c->r[4], 9}, {sun + 0x98, 1}}, &history);
    else with_drawn_camera(c, f_02570210_orig, "lens flare");
}
extern "C" void hook_02571B1C(Cpu* c) { with_drawn_camera(c, f_02571B1C_orig, "sea spray"); }

// Screen fade (mDoGph_calcFade 025F06A8): called once per drawn frame, so every fade (stage entry,
// scene changes) ran 2x/4x/8x too fast with frame interpolation. Advanced once per logic step and
// shown at the drawn pass's fraction (interp_fade.h). l_graphics (101F4808): +8 mFadeRate, +0xC
// mFadeSpeed, +0x1F mFade.
// debug: WWHD_INTERP_FADE_TRACE=n logs n calls (step, pass fraction, rate before and after)
extern "C" void f_025F06A8_orig(Cpu* c);
extern "C" void hook_025F06A8(Cpu* c) {
    using namespace interp;
    if (!enabled()) {
        f_025F06A8_orig(c);
        return;
    }
    const uint32_t g = GD(0x101F4808);
    static fade::Gate gate;
    fade::State s{(float)ldf32(g + 8), (float)ldf32(g + 0xC), (uint8_t)ld8(g + 0x1F)};
    const float before = s.rate;
    const float t = display_t();
    fade::call(gate, g_logic_steps, t, s, [&](fade::State& v) {
        stf32(g + 8, v.rate);
        stf32(g + 0xC, v.speed);
        st8(g + 0x1F, v.fade);
        f_025F06A8_orig(c);
        v.rate = (float)ldf32(g + 8);
        v.speed = (float)ldf32(g + 0xC);
        v.fade = (uint8_t)ld8(g + 0x1F);
    });
    stf32(g + 8, s.rate);
    stf32(g + 0xC, s.speed);
    st8(g + 0x1F, s.fade);
    static int trace = getenv("WWHD_INTERP_FADE_TRACE") ? atoi(getenv("WWHD_INTERP_FADE_TRACE")) : 0;
    static uint64_t calls_step = ~0ull;
    static int calls = 0, calls_logged = 0;
    if (calls_step != g_logic_steps) {
        if (trace > 0 && calls_step != ~0ull && calls_logged < 10) {
            calls_logged++;
            LOG("[interp] fade: mDoGph_calcFade called %d times in step %llu", calls, (unsigned long long)calls_step);
        }
        calls_step = g_logic_steps;
        calls = 0;
    }
    calls++;
    if (trace > 0 && s.fade) {
        trace--;
        LOG("[interp] fade: step %llu pass %llu t %.3f rate %.4f -> %.4f speed %.4f alpha %u", (unsigned long long)g_logic_steps,
            (unsigned long long)g_passes, t, before, s.rate, s.speed, ld8(g + 0x17));
    }
}
// The GameCube fade (0252F5A0, the snapshot fade and the event director's fades; drawn as the HD
// wipe's texture scale): called from child 13 on every drawn pass, so it ran 2x/4x/8x too fast and
// the logic waiting for it went on earlier than at 30 fps. Advanced on the logic pass only, shown at
// the pass's fraction (interp_fade.h rate_call). 101D6160 rate, +4 speed, +C on.
// debug: WWHD_INTERP_WIPE_FADE=0 turns this gate (and the fader's start delay per step) off
extern "C" void f_0252F5A0_orig(Cpu* c);
extern "C" void hook_0252F5A0(Cpu* c) {
    using namespace interp;
    static const bool off = getenv("WWHD_INTERP_WIPE_FADE") && !atoi(getenv("WWHD_INTERP_WIPE_FADE"));
    const uint32_t w = GD(0x101D6160);
    // test aid: WWHD_TEST_WIPE=step:speed starts this fade at a logic step as 0252F54C does (rate from
    // the end it starts at, the speed, on), for a check of its time and frames at every frame rate
    static uint64_t test_step = 0;
    static float test_speed = 0;
    static const bool test = getenv("WWHD_TEST_WIPE") && sscanf(getenv("WWHD_TEST_WIPE"), "%llu:%f", (unsigned long long*)&test_step, &test_speed) == 2;
    if (test && !g_hold_frame && g_logic_steps == test_step) {
        test_step = 0;
        stf32(w, test_speed >= 0.0f ? (float)ldf32(GD(0x1004CA34)) : (float)ldf32(GD(0x1004CA30)));
        stf32(w + 4, test_speed);
        st8(w + 0xC, 1);
        LOG("[test] WWHD_TEST_WIPE: the GC fade starts at logic step %llu, speed %.4f (bounds %.4f %.4f)", (unsigned long long)g_logic_steps, test_speed, (float)ldf32(GD(0x1004CA34)), (float)ldf32(GD(0x1004CA30)));
    }
    // debug: WWHD_INTERP_WIPE_TRACE=n logs n calls while it is on (step, pass, fraction, rate shown)
    static int trace = getenv("WWHD_INTERP_WIPE_TRACE") ? atoi(getenv("WWHD_INTERP_WIPE_TRACE")) : 0;
    struct Trace {
        int& n;
        uint32_t w;
        ~Trace() {
            if (n > 0 && ld8(w + 0xC)) {
                n--;
                LOG("[wipe] step %llu pass %llu %s t %.3f on %u rate %.4f speed %.4f", (unsigned long long)g_logic_steps,
                    (unsigned long long)g_passes, g_hold_frame ? "hold " : "logic", display_t(), ld8(w + 0xC), (float)ldf32(w),
                    (float)ldf32(w + 4));
            }
        }
    } tr{trace, w};
    if (!enabled() || off) {
        f_0252F5A0_orig(c);
        return;
    }
    static fade::RateGate gate;
    fade::State s{(float)ldf32(w), (float)ldf32(w + 4), (uint8_t)ld8(w + 0xC)};
    fade::rate_call(gate, g_logic_steps, !g_hold_frame, display_t(), s, [&](fade::State& v) {
        stf32(w, v.rate);
        stf32(w + 4, v.speed);
        st8(w + 0xC, v.fade);
        f_0252F5A0_orig(c);
        v.rate = (float)ldf32(w);
        v.speed = (float)ldf32(w + 4);
        v.fade = (uint8_t)ld8(w + 0xC);
    });
    stf32(w, s.rate);
    stf32(w + 4, s.speed);
    st8(w + 0xC, s.fade);
    if (!(s == gate.after)) {
        uint32_t bits;
        const float r = gate.after.rate;
        memcpy(&bits, &r, 4);
        exact_after_pass(w, bits, 4);
        exact_after_pass(w + 0xC, gate.after.fade, 1);
    }
}
// mDoGph_calcMonotone (025F0840, from the environment's execute, once per logic step): its change of
// the grey-scale rate is drawn blended (interp_fx.cpp fx_monotone_step). debug: WWHD_INTERP_FADE_TRACE
namespace interp { void fx_monotone_step(int16_t before, int16_t after); }
extern "C" void f_025F0840_orig(Cpu* c);
extern "C" void hook_025F0840(Cpu* c) {
    static int trace = getenv("WWHD_INTERP_FADE_TRACE") ? atoi(getenv("WWHD_INTERP_FADE_TRACE")) : 0;
    const uint32_t g = GD(0x101F4808);
    const int16_t before = (int16_t)ld16(g + 0x18);
    f_025F0840_orig(c);
    if (interp::enabled() && !interp::g_hold) interp::fx_monotone_step(before, (int16_t)ld16(g + 0x18));
    if (trace > 0 && before != (int16_t)ld16(g + 0x18)) {
        trace--;
        LOG("[interp] monotone: step %llu pass %llu rate %d -> %d", (unsigned long long)interp::g_logic_steps,
            (unsigned long long)interp::g_passes, before, (int16_t)ld16(g + 0x18));
    }
}
