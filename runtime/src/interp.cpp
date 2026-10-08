#include "interp.h"
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
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "interp_pacing.h"
#include "guest_addr.h"
#include "render_prof.h"
#include "runtime.h"
#include "savestate.h"
#include "true60.h"


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

namespace interp {

// Output frame rate with interpolation: 60, 120 or 240 (in-between frames per step: fps/30 - 1).
// WWHD_INTERP_FPS=60|120|240 sets it at start (and switches interpolation on); it wins over the
// saved choice and is not saved.
using pacing::valid_fps;
static const int g_env_fps = [] { const char* e = getenv("WWHD_INTERP_FPS"); return e ? valid_fps(atoi(e)) : 0; }();
static std::atomic<int> g_fps{g_env_fps ? g_env_fps : 60};
static std::atomic<bool> g_on{[] {
    const char* e = getenv("WWHD_INTERP");
    return (e ? atoi(e) != 0 : g_env_fps != 0) && !true60::enabled();
}()};
bool interp_on() { return g_on.load(std::memory_order_relaxed); }
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
bool present_vsync() { return g_present_vsync.load(std::memory_order_relaxed); }
int output_fps() { return pacing::cap_fps(fps(), display_hz(), present_vsync()); }
void set_display_hz(int hz) {
    if (hz < 0) hz = 0;
    const int old = g_display_hz.exchange(hz);
    if (old == hz) return;
    if (g_env_hz >= 0) LOG("[interp] display refresh rate %d Hz (WWHD_DISPLAY_HZ=%d is used)", hz, g_env_hz);
    else LOG("[interp] display refresh rate %d Hz: frame interpolation draws up to %d fps", hz, pacing::cap_fps(240, hz, present_vsync()));
}
void set_present_vsync(bool on) {
    if (g_present_vsync.exchange(on) != on) LOG("[interp] presentation %s the display's vsync", on ? "waits for" : "does not wait for");
}
// in-between frames per logic step of the current mode (true 60: its one half pass)
int in_between() { return interp_on() ? output_fps() / 30 - 1 : true60::enabled() ? 1 : 0; }
// frames drawn per logic step: 1 (30 fps), 2 (60 fps, true 60), 4 (120 fps), 8 (240 fps)
int frames_per_step() { return in_between() + 1; }
static const char* fps_name(int f) { return f == 240 ? "240 fps" : f == 120 ? "120 fps" : "60 fps"; }
void set_enabled(bool v) {
    if (v) true60::set_enabled(false);
    g_on = v;
    LOG("[interp] frame interpolation %s", v ? (fps() == 240 ? "on (240 fps)" : fps() == 120 ? "on (120 fps)" : "on (60 fps)") : "off");
}
void set_fps(int f) {
    f = valid_fps(f);
    if (g_fps.exchange(f) != f && interp_on()) LOG("[interp] frame interpolation at %s", fps_name(f));
}
// the 60 fps mode: 0 off, 1 frame interpolation (at fps()), 2 true 60 (game logic at 60 steps per second)
int mode() { return true60::enabled() ? 2 : interp_on() ? 1 : 0; }
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
// the virtual vsync itself ticks 2/4 times as fast (vsync_rate(), gx2_core.cpp) and the interval is
// halved in those ticks, so every mode keeps the formula (and 30/60 fps their exact timing).
uint32_t effective_swap_interval(uint32_t game) { return enabled() ? std::max<uint32_t>(1, game / 2) : game; }
int vsync_rate() { return interp_on() ? frames_per_step() / 2 : 1; }

namespace {
constexpr uint32_t kEye = 0xDC, kCenter = 0xE8, kUp = 0xF4, kFovy = 0xD4, kBank = 0x100;

struct CamState {
    float eye[3], center[3], up[3], fovy;
    int16_t bank;
};

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

float dist(const float* a, const float* b) {
    float d = 0;
    for (int i = 0; i < 3; i++) d += (a[i] - b[i]) * (a[i] - b[i]);
    return std::sqrt(d);
}

// The blend fraction t of a pass: 1/(N+1) on the logic pass, (k+1)/(N+1) on blended hold pass k;
// 1 = exact. The blends (interp_pacing.h) keep the original halfway arithmetic at t = 1/2, so 60 fps
// draws bit-identical frames to the halfway-only code (0.5f * (a + b), nlerp, integer halving).
using pacing::lerp_f;
using pacing::lerp_s16;

// Blended camera between the last exact step (a) and the new one (b).
// - Snaps (re-centring behind Link, doors, mode changes, cutscene cuts) are not blended: a step is a
//   snap when it is far larger than the camera's recent motion, or larger than an absolute limit.
//   Decided once per step, on the logic pass (cam_snap updates the recent motion); the step's
//   blended hold passes reuse the decision.
// - Orbits around the look-at point blend the eye's direction and distance separately, so the
//   blended eye stays on the arc instead of cutting the corner towards the target.
float g_last_step = 0;  // camera eye movement over the previous step

bool cam_snap(const CamState& a, const CamState& b) {
    static const float kCut = getenv("WWHD_INTERP_CUT") ? (float)atof(getenv("WWHD_INTERP_CUT")) : 800.0f;
    float step = std::max(dist(a.eye, b.eye), dist(a.center, b.center));
    float prev = g_last_step;
    g_last_step = step;
    return step > kCut || std::fabs(a.fovy - b.fovy) > 20.0f || (step > 60.0f && step > 4.0f * prev + 20.0f);
}

CamState blend(const CamState& a, const CamState& b, float t) {
    CamState m;
    for (int i = 0; i < 3; i++) {
        m.center[i] = lerp_f(a.center[i], b.center[i], t);
        m.up[i] = lerp_f(a.up[i], b.up[i], t);
    }
    // eye = center + direction * distance, each blended on its own
    float da[3], db[3], la = 0, lb = 0;
    for (int i = 0; i < 3; i++) {
        da[i] = a.eye[i] - a.center[i];
        db[i] = b.eye[i] - b.center[i];
        la += da[i] * da[i];
        lb += db[i] * db[i];
    }
    la = std::sqrt(la);
    lb = std::sqrt(lb);
    if (la > 1e-3f && lb > 1e-3f) {
        float cosang = 0;
        for (int i = 0; i < 3; i++) cosang += (da[i] / la) * (db[i] / lb);
        if (cosang < 0.7071f) return b;  // view turned more than 45 degrees in one step: a snap
        // the camera's collision (trees, walls, bushes) pulls it in abruptly; a blended distance could put
        // the in-between frame inside the obstacle, so abrupt distance changes are not blended
        if (std::fabs(la - lb) > 0.1f * std::max(la, lb)) return b;
        float dir[3], ld = 0;
        // direction: halfway = normalised sum; other fractions slerp (constant angular speed over
        // the step's frames; nlerp would bunch them towards the middle)
        float wa, wb;
        pacing::slerp_weights(cosang, t, wa, wb);
        for (int i = 0; i < 3; i++) {
            dir[i] = wa * da[i] / la + wb * db[i] / lb;  // blended direction (normalised below)
            ld += dir[i] * dir[i];
        }
        ld = std::sqrt(ld);
        float len = lerp_f(la, lb, t);
        for (int i = 0; i < 3; i++) m.eye[i] = m.center[i] + (ld > 1e-3f ? dir[i] / ld : db[i] / lb) * len;
    } else {
        for (int i = 0; i < 3; i++) m.eye[i] = lerp_f(a.eye[i], b.eye[i], t);
    }
    // up: normalised and made perpendicular to the blended view direction, so the in-between frame
    // gets no extra roll (matters most when looking down from above, where small differences in up
    // turn into large twists)
    float fwd[3], lf = 0, lu = 0, d = 0;
    for (int i = 0; i < 3; i++) { fwd[i] = m.center[i] - m.eye[i]; lf += fwd[i] * fwd[i]; }
    lf = std::sqrt(lf);
    if (lf > 1e-3f) {
        for (int i = 0; i < 3; i++) { fwd[i] /= lf; d += m.up[i] * fwd[i]; }
        for (int i = 0; i < 3; i++) { m.up[i] -= d * fwd[i]; lu += m.up[i] * m.up[i]; }
        lu = std::sqrt(lu);
        if (lu > 0.1f) {
            for (int i = 0; i < 3; i++) m.up[i] /= lu;
        } else {
            for (int i = 0; i < 3; i++) m.up[i] = b.up[i];  // degenerate: keep the exact up vector
        }
    }
    m.fovy = lerp_f(a.fovy, b.fovy, t);
    m.bank = lerp_s16(a.bank, b.bank, t);  // shortest way round
    return m;
}

std::atomic<uint64_t> g_executed_steps{0};
uint64_t g_logic_steps = 0; // full logic steps (all passes without a 60 fps mode)
uint64_t g_passes = 0;      // every pass of the per-frame function
bool g_hold = false;        // hold pass: draw only, no logic
bool g_cam_blended = false; // camera_draw is drawing the blended camera
bool g_logic_pass = false;  // logic pass with interpolation on: camera drawn blended
bool g_blend_draw = false;  // inside the loop body of a blended pass (logic pass or blended hold pass)
bool g_hold_next = false;   // the next pass is a hold pass
bool g_hold_frame = false;  // inside the per-frame function on a hold pass
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
struct Prev {
    uint32_t cam = 0; CamState s{}; bool valid = false; bool snap = true; uint64_t snap_step = ~0ull;
    CamState drawn{};          // the camera as camera_draw drew it on pass drawn_pass (blended or exact)
    uint64_t drawn_pass = ~0ull;
};
Prev g_prev[4];

Prev* prev_for(uint32_t cam) {
    for (auto& p : g_prev)
        if (p.cam == cam) return &p;
    for (auto& p : g_prev)
        if (!p.valid) { p.cam = cam; return &p; }
    g_prev[0] = Prev{cam};
    return &g_prev[0];
}
}  // namespace

// blend fraction of this pass: 1/(N+1) on a logic pass, (k+1)/(N+1) on blended hold pass k, 1 on
// the record pass, an exact step and without interpolation
float pass_t() {
    if (!enabled()) return 1.0f;
    return pacing::pass_fraction(g_hold ? g_phase : 0, g_step_n, g_exact_step);
}
// the step's exact state is drawn and recorded (the "before" of the next step's blended frames)
bool record_pass() { return g_hold && g_phase >= g_step_n; }

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

// camera_draw(camera_process_class*)
extern "C" void hook_024FFC40(Cpu* c) {
    using namespace interp;
    uint32_t cam = c->r[3];
    Prev* p = prev_for(cam);
    if (g_blend_draw && !true60::runs_60(cam)) {  // blended between the step drawn last (S) and the new one (S+1)
        CamState cur = read_cam(cam);
        if (p->valid) {
            if (!g_hold) {  // logic pass: the step's snap decision
                p->snap = cam_snap(p->s, cur);
                p->snap_step = g_logic_steps;
            }
            if (p->snap_step == g_logic_steps && !p->snap) write_cam(cam, blend(p->s, cur, pass_t()));
        }
        p->drawn = read_cam(cam);
        p->drawn_pass = g_passes;
        // (also on a snap: the sound listener keeps following only the record pass's camera)
        g_cam_blended = p->valid;
        cam_trace(cam, "blended");
        f_024FFC40_orig(c);
        g_cam_blended = false;
        write_cam(cam, cur);
        return;
    }
    p->s = read_cam(cam);  // exact step: remember it for the next step's blended frames
    p->valid = true;
    p->drawn = p->s;
    p->drawn_pass = g_passes;
    cam_trace(cam, "exact");
    if (g_hold && true60::enabled()) {  // true 60: the half pass's camera is a preview (true60.cpp)
        p->drawn_pass = ~0ull;  // (drawn with the preview: the stars keep the camera as it is)
        true60::camera_draw_preview(true);
        f_024FFC40_orig(c);
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

struct ModelPrev {
    uint32_t mtx = 0;
    uint64_t pass = 0;  // hold pass it was recorded on
    std::vector<uint32_t> w;  // raw guest words, 12 per matrix
};
std::unordered_map<uint32_t, ModelPrev> g_models;
struct ModelStats { uint32_t blended = 0, fresh = 0, cut = 0; } g_mstats;
// models whose UBO update of this pass already ran on the main thread (blended), per model: the
// update_ubo job skips that many of its updates
std::mutex g_ubo_mu;
std::unordered_map<uint32_t, uint32_t> g_ubo_done;

float wf(uint32_t w) { return u32_as_f32(__builtin_bswap32(w)); }
uint32_t fw(float f) { return __builtin_bswap32(f32_as_u32(f)); }

// debug: WWHD_INTERP_MODEL_TRACE=n logs n draws of one model (the first with at least
// WWHD_INTERP_MODEL_TRACE_JOINTS joints, default 40): root joint translation as drawn
void trace_model(const char* what, uint32_t jnt, uint32_t n, const uint32_t* w) {
    static int left = getenv("WWHD_INTERP_MODEL_TRACE") ? atoi(getenv("WWHD_INTERP_MODEL_TRACE")) : 0;
    static const uint32_t min_joints = getenv("WWHD_INTERP_MODEL_TRACE_JOINTS") ? atoi(getenv("WWHD_INTERP_MODEL_TRACE_JOINTS")) : 40;
    static uint32_t which = 0;
    if (left <= 0 || n < min_joints || (which && which != jnt)) return;
    which = jnt;
    left--;
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
}  // namespace interp

// J3DModel::viewCalc(J3DModel*)
extern "C" void hook_027F55FC(Cpu* c) {
    using namespace interp;
    static const bool off = getenv("WWHD_INTERP_MODELS") && !atoi(getenv("WWHD_INTERP_MODELS"));  // debug
    if (!enabled() || off || (!g_hold && (true60::drawing_60() || g_exact_step))) {
        f_027F55FC_orig(c);
        return;
    }
    const bool record = record_pass();
    uint32_t model = c->r[3];
    uint32_t jnt = ld32(model + kMdlJoints);
    uint32_t mtx = jnt ? ld32(jnt + kJntMtx) : 0;
    uint32_t n = jnt ? ld16(jnt + kJntNum) : 0;
    if (!mtx || n == 0 || n > kMaxJoints) {
        f_027F55FC_orig(c);
        return;
    }
    const uint32_t words = 12 * n;
    const uint32_t* cur = (const uint32_t*)ppc_ptr(mtx);  // guest (big-endian) words
    if (record) {  // exact step: remember it for the next step's blended frames
        ModelPrev& p = g_models[jnt];
        p.mtx = mtx;
        p.pass = g_record_passes;
        p.w.assign(cur, cur + words);
        trace_model("exact", jnt, n, cur);
        f_027F55FC_orig(c);
        return;
    }
    // logic pass or blended hold pass: between the step drawn last and the new one, at pass_t()
    auto it = g_models.find(jnt);
    if (it == g_models.end() || it->second.mtx != mtx || it->second.w.size() != words ||
        it->second.pass + 1 < g_record_passes) {  // new model, or not drawn on the last record pass
        g_mstats.fresh++;
        trace_model("new", jnt, n, cur);
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
    for (uint32_t m = 0; m < n; m++) {
        float a[12], b[12], o[12];
        for (int k = 0; k < 12; k++) { a[k] = wf(prev[12 * m + k]); b[k] = wf(saved[12 * m + k]); }
        float dx = a[3] - b[3], dy = a[7] - b[7], dz = a[11] - b[11];
        if (!(dx * dx + dy * dy + dz * dz <= kCut * kCut)) {  // teleport (or NaN): show the new step
            g_mstats.cut++;
            trace_model("cut", jnt, n, cur);
            f_027F55FC_orig(c);
            return;
        }
        blend_mtx(a, b, o, t);
        for (int k = 0; k < 12; k++) mid[12 * m + k] = fw(o[k]);
    }
    g_mstats.blended++;
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
void fx_hold_blend(float t);   // interp_fx.cpp: blended hold pass: logic-time effect blends at t
void fx_ss_reset();
// a save state was loaded: nothing may blend across the jump
void ss_reset() {
    for (auto& p : g_prev) p = Prev{};
    g_last_step = 0;
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
    true60::ss_reset();
}
// Paced interpolation (WWHD_INTERP_PACED=1, the default on Android and at 120/240 fps): an
// in-between pass is drawn only when it fits before the next logic step is due (33.3 ms after the
// last one, measured with the passes' recent durations); otherwise the rest of the step's
// in-between passes are dropped and the next step waits for its time. The game then always advances
// 30 steps a second, and the picture gets 60/120/240 frames a second where the device draws (and
// the display shows) them fast enough and fewer where it does not. Without pacing, every logic step
// is followed by all its in-between passes, and a device that draws fewer frames a second runs the
// whole game slower than real time (at 120/240 fps on a 60 Hz display with vsync: half or a
// quarter of the speed), which is why pacing is on by default there.
// At 120/240 fps the logic pass also plans how many in-between frames the step gets (as many as
// fit at the recent pass duration, at least one) and spaces them evenly over the step
// (t = k/(n+1)): a 60 Hz display then gets clean 60 fps blending instead of uneven frames and a
// dropped exact frame every step. A step whose passes all fit gets the full N (t = k/(N+1)).
// The settings overlay switches it ("Keep game speed", saved with the graphics options, one value
// for 60 fps and one for 120/240 fps); the variable sets both at start and wins over the saved
// values.
static const char* const g_env_paced = getenv("WWHD_INTERP_PACED");
static std::atomic<bool> g_paced{[] {
#ifdef __ANDROID__
    return !g_env_paced || atoi(g_env_paced) != 0;
#else
    return g_env_paced && atoi(g_env_paced) != 0;
#endif
}()};
static std::atomic<bool> g_paced_hi{!g_env_paced || atoi(g_env_paced) != 0};  // 120/240 fps
static std::atomic<bool>& paced_flag(int f) { return f > 60 ? g_paced_hi : g_paced; }
static bool paced() { return paced_flag(fps()).load(std::memory_order_relaxed); }
bool paced_interpolation() { return paced(); }  // at the current frame rate
bool paced_interpolation_at(int f) { return paced_flag(f).load(std::memory_order_relaxed); }
void set_paced_interpolation_at(int f, bool on) {
    if (paced_flag(f).exchange(on) != on)
        LOG("[interp] paced interpolation at %s %s", f > 60 ? "120/240 fps" : "60 fps", on ? "on (keeps the game's speed)" : "off");
}
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
constexpr auto kProbeAfter = std::chrono::seconds(10);
// one tick of the virtual vsync at the drawn rate (59.94 Hz x frames per step / 2)
static pace_clock::duration vsync_tick() {
    return std::chrono::nanoseconds(16'683'333LL * 2 / std::max(2, frames_per_step()));
}
// (the hold-pass estimate after a pass of the previous kind: logic or hold)
static bool g_pass_was_hold = false;
static bool g_wait_step = false;  // the next logic pass waits for its time
static uint64_t g_paced_possible = 0, g_paced_holds = 0, g_paced_steps = 0, g_paced_planned = 0;
constexpr auto kPacedStep = std::chrono::nanoseconds(33'333'333);
// a logic pass and an in-between pass both wait for the 59.94 Hz grid (2 x 16.68 = 33.37 ms, more
// than kPacedStep): 2 ms of slack
constexpr auto kPacedBudget = std::chrono::nanoseconds(kPacedStep + std::chrono::milliseconds(2));
// start of every pass: the last pass's own duration, and the wait before a logic pass that follows
// a dropped in-between pass (and at 120/240 fps before every logic pass that comes early)
static void paced_pass_start() {
    static bool previousRecord = false;
    if (!paced() || !interp_on()) { g_exact_step = false; previousRecord = false; return; }
    if (!g_hold_next) g_exact_step = !previousRecord;  // this logic pass: blend only after a record pass
    previousRecord = g_hold_next && g_phase + 1 >= g_step_n;  // this pass is the step's record pass
    const auto now = pace_clock::now();
    if (g_last_entry != pace_clock::time_point{}) {
        const auto pass = now - g_last_entry - g_slept;
        pace_clock::duration& avg = g_pass_was_hold ? g_hold_avg : g_logic_avg;
        // a probe after a long pause replaces the old value: the scene may have changed since
        avg = g_pass_was_hold && g_probe ? pass : pace_clock::duration(pacing::update_average(avg.count(), pass.count()));
        if (g_pass_was_hold) g_probe = false;
    }
    g_pass_was_hold = g_hold_next;
    g_last_entry = now;
    g_slept = {};
    if (g_hold_next) return;
    if (g_wait_step && now < g_last_logic + kPacedStep) {
        threads::park_sleep_until(g_last_logic + kPacedStep);  // the game keeps 30 steps a second
        g_slept = pace_clock::now() - now;
    }
    g_wait_step = false;
    g_last_logic = pace_clock::now();
}
// logic pass: the in-between frames of this step (pacing::plan_in_between: as many as fit at the
// recent pass duration)
static int g_step_holds = 0;  // hold passes drawn in this step (a skip to the record pass leaves some out)
static int plan_step() {
    int n = std::max(1, in_between());
    if (n > 1 && paced() && interp_on()) {
        n = pacing::plan_in_between(n, kPacedBudget.count(), std::chrono::duration_cast<std::chrono::nanoseconds>(g_logic_avg).count(),
                                    std::chrono::duration_cast<std::chrono::nanoseconds>(g_hold_avg).count());
        g_paced_planned += n;
    }
    if (g_exact_step) n = 1;  // drawn exactly; its record pass makes the next step blend again
    g_step_holds = 0;
    return n;
}
// a paced step ended after `holds` in-between frames
static void paced_step_done(int holds) {
    const int n = std::max(1, in_between());
    g_paced_holds += holds;
    g_paced_possible += n;
    g_paced_steps++;
    static unsigned recentHolds = 0, recentPossible = 0, recentN = 0;
    recentHolds += holds;
    recentPossible += n;
    if (++recentN == 60) {
        g_paced_share.store(recentHolds / float(recentPossible), std::memory_order_relaxed);
        recentHolds = recentPossible = recentN = 0;
    }
    if (g_paced_steps >= 300) {
        if (n > 1)
            LOG("[interp] paced: %.0f%% of in-between frames drawn (in-between pass %.1f ms; %.2f of %d planned per step)",
                100.0 * g_paced_holds / double(g_paced_possible), std::chrono::duration<double, std::milli>(g_hold_avg).count(),
                g_paced_planned / double(g_paced_steps), n);
        else
            LOG("[interp] paced: %.0f%% of in-between frames drawn (in-between pass %.1f ms)", 100.0 * g_paced_holds / double(g_paced_possible),
                std::chrono::duration<double, std::milli>(g_hold_avg).count());
        g_paced_possible = g_paced_holds = g_paced_steps = g_paced_planned = 0;
    }
}
// end of every pass with a 60 fps mode on (phase 0: the logic pass): g_hold_next = another hold
// pass of this step follows (always, unless paced and it does not fit before the next step is due;
// pacing::next_pass). At 60 fps (one in-between pass, the record pass) it is the original rule:
// the in-between pass is drawn when it fits, otherwise dropped.
static void after_pass(int phase) {
    const bool pacing = paced() && interp_on();
    if (phase > 0) g_step_holds++;
    if (phase >= g_step_n) {  // the record pass ended the step
        g_hold_next = false;
        if (pacing) {
            paced_step_done(g_step_holds);
            if (in_between() > 1) g_wait_step = true;  // fewer passes than planned would make the step short
        }
        return;
    }
    if (!pacing) { g_hold_next = true; g_wait_step = false; return; }
    if (phase > 0) g_last_hold = pace_clock::now();
    const auto now = pace_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - g_last_logic).count();
    // After a long time without in-between passes their cost is stale (the scene may be lighter
    // now): one probe, if one vsync tick fits; its duration replaces the estimate.
    const bool probe = phase == 0 && now - g_last_hold >= kProbeAfter;
    const auto pass = std::chrono::duration_cast<std::chrono::nanoseconds>(probe ? vsync_tick() : g_hold_avg).count();
    switch (pacing::next_pass(phase, g_step_n, elapsed, pass, kPacedBudget.count())) {
    case pacing::Next::kHold:
        g_hold_next = true;
        g_probe = probe;
        g_last_hold = now;
        break;
    case pacing::Next::kRecord:  // the blended hold passes left are skipped, the record pass is drawn
        g_hold_next = true;
        g_phase = g_step_n - 1;  // (the next hold pass increments it to the record pass)
        g_last_hold = now;
        break;
    default:  // the rest of the step's in-between passes (and its record pass) are dropped
        g_hold_next = false;
        g_wait_step = true;
        paced_step_done(g_step_holds);
        break;
    }
}
}  // namespace interp

namespace mods { void cheats_service(); }  // mods/cheats.cpp

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

extern "C" void hook_0203593C(Cpu* c) {
    using namespace interp;
    fx_pass_start();
    ss::service(c);  // save states: exact values are back in guest memory, all other threads idle
    mods::cheats_service();
    g_passes++;
    // test aid: WWHD_INTERP_AT_STEP=n switches interpolation on after n frames
    static uint64_t passes = 0;
    static const uint64_t at = getenv("WWHD_INTERP_AT_STEP") ? strtoull(getenv("WWHD_INTERP_AT_STEP"), nullptr, 10) : 0;
    if (at && ++passes == at) set_enabled(true);
    static uint64_t at60 = getenv("WWHD_TRUE60_AT_STEP") ? strtoull(getenv("WWHD_TRUE60_AT_STEP"), nullptr, 10) : 0;
    static uint64_t passes60 = 0;
    if (at60 && ++passes60 == at60) set_mode(2);
    paced_pass_start();
    true60::new_pass();
    true60::pass_begin(!enabled() || !g_hold_next);  // full pass: take back Link's half-pass preview
    if (!enabled() || !g_hold_next) g_logic_steps++;
    if (!enabled()) {
        g_hold_next = false;
        g_phase = 0;
        g_step_n = 1;
        f_0203593C_orig(c);
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
        } else {
            fx_hold_blend(pass_t());  // the effects blended at logic time, at this pass's fraction
        }
        f_0203593C_orig(c);
        g_hold_frame = false;
        after_pass(g_phase);
        g_hold = false;
        return;
    }
    g_phase = 0;
    g_step_n = plan_step();
    {
        PassTimer timer(0);
        f_0203593C_orig(c);  // logic pass (the loop body hook marks it)
    }
    after_pass(0);  // g_hold_next: an in-between pass follows (always, unless paced)
    static uint64_t n = 0, t0 = timebase::now(), f0 = 0;
    if (++n % 300 == 0) {
        uint64_t t = timebase::now();
        LOG("[interp] %.1f logic steps/s (%s, %.2f frames per step); models per step: %.1f blended, %.1f new, %.1f cut%s",
            300.0 * timebase::kTicksPerSec / (double)(t - t0), mode_name(), (frames - f0) / 300.0, g_mstats.blended / 300.0,
            g_mstats.fresh / 300.0, g_mstats.cut / 300.0, pass_cpu_report().c_str());
        t0 = t;
        f0 = frames;
        g_mstats.blended = g_mstats.fresh = g_mstats.cut = 0;
        for (auto it = g_models.begin(); it != g_models.end();)  // models no longer drawn
            it = it->second.pass + 2 < g_record_passes ? g_models.erase(it) : std::next(it);
    }
}

// main loop body (inside the per-frame function): logic pass -> camera drawn blended;
// hold pass -> only fapGm_Execute, whose logic parts are skipped (hooks below); blended hold
// passes draw the camera blended too
extern "C" void hook_025F172C(Cpu* c) {
    using namespace interp;
    if (g_hold_frame) {
        g_blend_draw = blended_hold();
        f_025D42EC(c);
        g_blend_draw = false;
        return;
    }
    g_logic_pass = enabled() && !g_exact_step;  // (an exact step draws like interpolation off)
    g_blend_draw = g_logic_pass;
    f_025F172C_orig(c);
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
    // children 7-14 (after the loop body: frame setup, lighting, display) run; 1, 4, 5 (HD menus and
    // systems) are held. Child 0 always runs: it only reads a system state, and a non-zero result
    // makes the per-frame function skip the whole frame (a skipped call would leave its argument, a
    // pointer, in r3, so hold passes drew nothing and every step was shown twice).
    // Also run: 2-3 (025EE048/02756170: heap setup the drawing needs) and 6 (0273841C: clears the
    // light counts of the HD light manager at *101F8A20 (+0x10..+0x1C), which dKy_setLight (child 8)
    // fills via 0273862C and child 9 hands to the renderer; held, every point light was added a
    // second time on hold passes: candle light twice as bright on every other frame at 60 fps).
    static const uint32_t run = (getenv("WWHD_HOLD_RUN") ? (uint32_t)strtoul(getenv("WWHD_HOLD_RUN"), nullptr, 0) : 0x7FCC) | 1;
    return interp::g_hold_frame && !(run & (1u << i)) && called_from_frame_function();
}
extern "C" void hook_02032FE8(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(0)) f_02032FE8_orig(c); }
extern "C" void hook_020357CC(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(1)) f_020357CC_orig(c); }
extern "C" void hook_025EE048(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(2)) f_025EE048_orig(c); }
extern "C" void hook_02756170(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(3)) f_02756170_orig(c); }
extern "C" void hook_02617AF4(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(4)) f_02617AF4_orig(c); }
extern "C" void hook_02614F74(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(5)) f_02614F74_orig(c); }
extern "C" void hook_0273841C(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(6)) f_0273841C_orig(c); }
extern "C" void hook_0270870C(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(7)) f_0270870C_orig(c); }
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
    g_hold_child_cpu = c;
    if (hold_skip_child(9)) return;
    uint32_t o = c->r[3];
    interp::light_trace_add(" hdl[%08X %08X %08X %08X]", ld32(o + 0x10), ld32(o + 0x14), ld32(o + 0x18), ld32(o + 0x1C));
    f_02738438_orig(c);
}
extern "C" void hook_0278FEBC(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(10)) f_0278FEBC_orig(c); }
extern "C" void hook_027199C0(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(11)) f_027199C0_orig(c); }
extern "C" void hook_02618940(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(12)) f_02618940_orig(c); }
extern "C" void hook_02728A74(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(13)) f_02728A74_orig(c); }
extern "C" void hook_02039834(Cpu* c) { g_hold_child_cpu = c; if (!hold_skip_child(14)) f_02039834_orig(c); }


// logic parts of fpcM_Management / fapGm_Execute, skipped on hold passes
// debug: WWHD_INTERP_RUN=mask runs selected parts on hold passes too
// (1 execute, 2 delete, 4 priority, 8 create, 16 fapGm_After, 32 counter, 64 HD UI screens)
static bool skip(int bit) {
    static const int run = getenv("WWHD_INTERP_RUN") ? atoi(getenv("WWHD_INTERP_RUN")) : 0;
    return interp::g_hold && !(run & bit);
}
static bool g_in_execute = false;  // inside fpcEx_Handler (actor Execute): logic, not drawing
namespace mods { void after_execute(Cpu* c, uint32_t execute_fn); }  // mods/turbo.cpp
extern "C" void hook_025DE788(Cpu* c) {
    if (skip(1) && !true60::enabled()) return;  // true 60: the per-process gate decides (true60.cpp)
    interp::record_executed_step();
    g_in_execute = true;
    uint32_t execute_fn = c->r[3];
    f_025DE788_orig(c);
    mods::after_execute(c, execute_fn);  // quick doors / fast scene changes: extra steps (full passes only)
    if (!interp::g_hold_frame) mods::packages::frame(interp::g_logic_steps);
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
extern "C" void hook_02715310(Cpu* c) { if (!skip(64)) f_02715310_orig(c); }

namespace interp {
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
extern "C" void hook_025E1B44(Cpu* c) {
    if (interp::g_cam_blended) return;
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
// They are placed with the camera as the pass drew it.
extern "C" void f_02574144_orig(Cpu* c);
extern "C" void hook_02574144(Cpu* c) {
    using namespace interp;
    if (!enabled()) {
        f_02574144_orig(c);
        return;
    }
    CamState exact[4];
    bool swapped[4] = {};
    for (int i = 0; i < 4; i++) {
        Prev& p = g_prev[i];
        if (!p.cam || p.drawn_pass != g_passes) continue;
        exact[i] = read_cam(p.cam);
        write_cam(p.cam, p.drawn);
        swapped[i] = true;
    }
    f_02574144_orig(c);
    for (int i = 3; i >= 0; i--)
        if (swapped[i]) write_cam(g_prev[i].cam, exact[i]);
}
