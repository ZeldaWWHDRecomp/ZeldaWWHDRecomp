// Camera mods: direct right-stick camera, mouse camera, first person on R3 / mouse wheel.
//
// How the game turns the camera with the right stick (WWHD, measured and read from the code):
// pushing the stick puts the player camera into its manual mode (dCamera_c::nextMode: stick value
// above 0.3 and not pushed up), run by dCamera_c::manualCamera (0250FDC8). Per logic step it
//   1. shapes |stick x| with a bezier curve (024F6F9C) into a target turning speed,
//   2. eases the turning speed this+0x154 towards it with cLib_chaseF (+0.25 per step; 0.1/0.18 when
//      slowing down or reversing): the acceleration ramp,
//   3. turns the target yaw by speed * 0.92 * f25 degrees (f25: camera parameter, 8 here),
//   4. moves the drawn direction (mViewCache.mDirection, this+0x3C: R f32, V s16 +0x40, U s16 +0x42)
//      towards the target with a smoothing factor (U: [sp+0x1C], V: [sp+0x18]; about 0.66).
// Measured on Outset with the stick held fully right: 0.48, 1.13, 1.84, 2.56 ... 7.36 degrees per
// step after 10 steps (220.8 degrees/s at full speed), and an ease-out of 4 steps plus a tail on release.
//
// Direct camera (instruction hooks, tools/recomp/hooks_mods.txt):
//   @02510448 / @025104A0  right after the two cLib_chaseF calls: the speed is set to
//                          stick x * camera speed (linear, no ramp), so the yaw rate is
//                          x * 7.36 degrees/step * speed from the first step to the last
//   @02510EE4              the U smoothing factor (argument of cSAngle * f32) becomes 1
// Mouse camera: the same two places take the mouse movement instead (degrees = points x
//   sensitivity, converted to the speed unit with the live f25), @0251053C replaces the vertical
//   stick rate f28 by the mouse's vertical movement (V += -f28 * [sp+0x14] degrees) and @02510EA0
//   removes the V smoothing. The camera only runs manualCamera while the right stick is pushed, so
//   nextMode (024FA9C8) sees a small stick magnitude (0.45) only during its mode decision, for
//   styles that support manualCamera. The pad itself always gets a proportional mouse rate, so
//   other modes have a fallback without a fixed stick being switched on and off.
//   Boat and BoatBattle free look use this same manualCamera (style 118), including sailing,
//   paddling and salvage. rideCamera (0250D4E8) follows the ship while not looking around; it has
//   no right-stick yaw/pitch or cLib_chaseF sites of its own. Sailing/paddling procedures 0x88/0x89
//   are not item aiming: classifying them as aiming consumed the deltas before these sites ran.
//   In first person (dCamera_c::subjectCamera 025071FC ran) the mouse drives both right-stick axes
//   instead (rate control), which the game uses to look around / aim.
//   Left click: while Link aims an item (bow, boomerang, hookshot, rope; procedures 0x76, 0x7D, 0x80-0x81,
//   0x83-0x84, 0x8A-0x8D, 0x94-0x95) it presses the item's button (daPy_lk_c+0x68D9 mReadyItemBtn: 0 X, 1 Y, 2 R; read
//   from daPy_lk_c::itemTrigger 023EAB40). Otherwise left click does nothing.
// Swipe camera (Android touch controls, Flexible style; camera_swipe.h): a finger on the right half
//   turns the camera through this same path, in degrees from the finger's physical distance, times the
//   boat factor while Link sails (mModeFlg ModeFlg_IN_SHIP). It needs neither the mouse camera option
//   nor a captured mouse. Idea from the GreenNaugahyde Android fork (MPL-2.0), reimplemented.
// First person: R3 already toggles the game's first-person look (measured: R3 at rest -> camera
//   distance 250 -> 20 in 7 steps, R3 again -> back). The mod adds the mouse wheel: forward enters
//   first person, backward leaves it, as a one-step R3 press.
//
// Invert camera up/down (Controls; issue #167): the camera's own reads of the right stick's Y are
//   negated (instruction hooks right after each call, camera_invert.h lists every reader and why),
//   so first person, aiming and the follow/manual camera invert while the baton does not. The mouse
//   follows: in first person through the synthetic right stick, in the follow camera through the
//   sign of its pitch rate (site_0251053C).
//   Manual styles can have descending latitude limits: limited_range_addition then reverses its
//   delta. @02510DEC cancels that reversal for both inputs at the latitude call only, without
//   changing the style's limits or its centre-height, radius and field-of-view input.
//
// Logic only runs on logic passes: the sites run inside the camera's execute, and the camera's
// manual and subject modes run at 30 Hz in both 60 fps modes (true60 converts the follow camera only),
// so the per-step amounts above hold in all modes; interpolation blends the drawn camera.
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#include "../camera_invert.h"
#include "camera_mouse.h"
#include "camera_swipe.h"
#include "play_context.h"
#include "../platform/touch_options.h"
#include "guest_addr.h"
#include "../input_map.h"
#include "input.h"
#include "mods.h"
#include "../motion/motion.h"
#include "runtime.h"
#include "../editor/editor_game.h"

extern "C" {
void f_024FA9C8_orig(Cpu* c);  // dCamera_c::nextMode
void f_0250D4E8_orig(Cpu* c);  // dCamera_c::rideCamera
void f_0250FDC8_orig(Cpu* c);  // dCamera_c::manualCamera
void f_025071FC_orig(Cpu* c);  // dCamera_c::subjectCamera
void f_0240EBB0_orig(Cpu* c);  // daPy_Execute
}

namespace mods {
namespace {
constexpr uint32_t kCamSpeed = 0x154;   // dCamera_c: eased turning speed (manual camera)
constexpr uint32_t kCamStickX = 0x170;  // dCamera_c: right stick x as the camera sees it (negated)
constexpr uint32_t kCurProc = 0x65F0;   // daPy_lk_c::mCurProc
constexpr uint32_t kReadyItemBtn = 0x68D9;  // daPy_lk_c::mReadyItemBtn

std::mutex g_mu;
float g_dx = 0, g_dy = 0;       // mouse movement awaiting the next input step (points)
float g_swipe_dx = 0, g_swipe_dy = 0;    // swipe movement awaiting the next input step (degrees, before the boat factor)
uint64_t g_swipe_step = ~0ull;  // logic step of the last swipe movement
float g_direct_dx = 0, g_direct_dy = 0;  // this step's movement in degrees, reserved for direct camera sites
float g_mouse_rx = 0, g_mouse_ry = 0;    // proportional fallback, stable across repeated pad reads
uint64_t g_sample_step = ~0ull;
uint64_t g_move_step = 0;       // logic step of the last mouse movement
float g_wheel = 0;              // wheel movement not yet used
bool g_left = false;            // left button held (captured)
uint64_t g_manual_step = ~0ull, g_subject_step = ~0ull;  // last step manualCamera / subjectCamera ran
std::atomic<uint32_t> g_link{0};
std::atomic<uint32_t> g_cam{0};   // dCamera_c of the subject camera (traces)
uint64_t g_r3_from = 0, g_r3_to = 0;  // steps during which R3 is held by the wheel
uint64_t g_r3_ready = 0;              // next step a wheel toggle may start
bool g_synth = false;                 // filter_pad fed the mouse to the right stick this step
uint64_t g_synth_step = ~0ull;

bool recent(uint64_t s, uint64_t n) { return s != ~0ull && step() <= s + n; }
bool in_first_person() { return recent(g_subject_step, 2); }
uint32_t link_proc() {
    uint32_t l = g_link.load(std::memory_order_relaxed);
    return l ? ld32(l + kCurProc) : 0xFFFFFFFFu;
}
bool aiming_item(uint32_t p) { return camera_mouse::aiming_item(p); }
bool link_in_ship() {
    uint32_t l = g_link.load(std::memory_order_relaxed);
    return l && (ld32(l + playctx::kLinkModeFlg) & playctx::kModeInShip);
}

// test aid: WWHD_TEST_MOUSE=2-3:5:0,... moves the mouse by (dx, dy) points per logic step from 2 s
// to 3 s of game time (full logic steps / 30, from boot);
// WWHD_TEST_WHEEL=4.0:1,... scrolls by dy at that time.
struct TimedMouse { double from, to; float dx, dy; };
std::vector<TimedMouse> parse_mouse(const char* var, bool range) {
    std::vector<TimedMouse> v;
    const char* e = getenv(var);
    while (e && *e) {
        double a = 0, b = 0;
        float x = 0, y = 0;
        int n = 0;
        if (range ? sscanf(e, "%lf-%lf:%f:%f%n", &a, &b, &x, &y, &n) != 4 : sscanf(e, "%lf:%f%n", &a, &y, &n) != 2) break;
        v.push_back({a, range ? b : a, x, y});
        e += n;
        if (*e != ',') break;
        e++;
    }
    return v;
}
void inject_test_mouse() {
    static const std::vector<TimedMouse> moves = parse_mouse("WWHD_TEST_MOUSE", true);
    static const std::vector<TimedMouse> wheels = parse_mouse("WWHD_TEST_WHEEL", false);
    static uint64_t last = ~0ull;
    if (moves.empty() && wheels.empty()) return;
    uint64_t s = step();
    if (s == last) return;
    last = s;
    double t = game_time();
    for (auto& m : moves)
        if (t >= m.from && t < m.to) mouse_add(m.dx, m.dy);
    for (auto& w : wheels)
        if (t >= w.from && t < w.from + 1.0 / 30.0) mouse_wheel(w.dy);
}
// test aid: WWHD_TEST_GOTO=t:x:z:x:z... from game time t (s), steers Link with the left stick through
// the world points (x, z) in turn (closed loop on his heading, stick-direction independent of the
// camera); after the last point the stick is released. Used to walk to doors in measurements.
void test_goto(input::PadState& s) {
    static std::vector<float> pts;
    static double t0 = -1;
    static size_t next = 0;
    static float phi = 0;  // stick direction, radians clockwise from up
    static uint64_t last = ~0ull;
    if (t0 < 0) {
        const char* e = getenv("WWHD_TEST_GOTO");
        if (!e) { t0 = 1e30; return; }
        t0 = atof(e);
        for (const char* p = strchr(e, ':'); p; p = strchr(p + 1, ':')) pts.push_back((float)atof(p + 1));
    }
    uint32_t l = g_link.load(std::memory_order_relaxed);
    if (game_time() < t0 || !l || next + 1 >= pts.size()) return;
    float x = (float)ldf32(l + 0x314), z = (float)ldf32(l + 0x31C);
    float tx = pts[next], tz = pts[next + 1];
    // the last point counts as reached within 40 units or when Link stops moving towards it (a door)
    static float bx = 0, bz = 0;
    static uint64_t bstep = 0;
    bool last_pt = next + 3 >= pts.size();
    if (step() >= bstep + 10) {
        bool stuck = last_pt && bstep && std::hypot(x - bx, z - bz) < 15.0f;
        bx = x, bz = z, bstep = step();
        if (stuck) {
            next = pts.size();
            trace("goto: stopped at the last point (%.0f, %.0f), %.0f units away", tx, tz, std::hypot(tx - x, tz - z));
            return;
        }
    }
    if (std::hypot(tx - x, tz - z) < 40.0f) {
        next += 2;
        trace("goto: reached point %zu (%.0f, %.0f)", next / 2, tx, tz);
        return;
    }
    if (step() != last) {
        last = step();
        float want = std::atan2(tx - x, tz - z);
        float head = (float)(int16_t)ld16(l + 0x322) * 3.14159265f / 32768.0f;
        float err = std::remainder(want - head, 6.2831853f);
        phi -= 0.5f * err;  // stick to the right turns Link clockwise (heading decreases)
    }
    s.lx = std::sin(phi);
    s.ly = std::cos(phi);
}
bool test_mouse() {
    static const bool on = getenv("WWHD_TEST_MOUSE") != nullptr;
    return on;
}
}  // namespace

void mouse_add(float dx, float dy) {
    if (editor::take_mouse(dx, dy)) return;  // the fly camera looks around with it
    std::lock_guard<std::mutex> lk(g_mu);
    g_dx += dx;
    g_dy += dy;
    g_move_step = step();
}
void swipe_add(float dx_px, float dy_px, float dpi) {
    const float speed = touch_options::get().swipe_speed;
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_swipe_step == ~0ull || step() > g_swipe_step + 30)  // a new swipe (the touch smoke test looks for this)
        LOG("[input] swipe camera: %.1f degrees per inch", camera_swipe::degrees(camera_swipe::display_dpi(dpi), dpi, speed));
    g_swipe_dx += camera_swipe::degrees(dx_px, dpi, speed);
    g_swipe_dy += camera_swipe::degrees(dy_px, dpi, speed);
    g_swipe_step = g_move_step = step();
}
void mouse_wheel(float dy) {
    if (editor::take_wheel(dy)) return;  // the fly camera's speed
    std::lock_guard<std::mutex> lk(g_mu);
    g_wheel += dy;
}
void mouse_button(int button, bool down) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (button == 0) g_left = down;
}

// end of input::read(): synthetic right stick for the mouse, R3 pulses for the wheel, left click
void filter_pad(input::PadState& s) {
    inject_test_mouse();
    test_goto(s);
    uint64_t now = step();
    std::lock_guard<std::mutex> lk(g_mu);
    // stale movement (the camera never took it, e.g. during an event) is dropped
    if (g_move_step != ~0ull && now > g_move_step + 8) g_dx = g_dy = 0;
    if (g_swipe_step != ~0ull && now > g_swipe_step + 8) g_swipe_dx = g_swipe_dy = 0;
    // gyro (motion/motion.h): the game turns its first-person (subject) camera with the GamePad's motion,
    // which every item aim uses too
    const bool aim = in_first_person() || aiming_item(link_proc());
    motion::set_aiming(aim);
    if (aim && trace_on()) {  // gyro measurements (docs/gyro.md): Link heading and body pitch, camera U / V while aiming
        static uint64_t traced = ~0ull;
        if (uint32_t l = g_link.load(std::memory_order_relaxed); l && traced != now) {
            traced = now;
            const uint32_t cam = g_cam.load(std::memory_order_relaxed);
            trace("aim: proc %X heading %d body x %d camera U %d V %d", link_proc(), (int16_t)ld16(l + 0x322), (int16_t)ld16(l + 0x3D0),
                  cam ? (int16_t)ld16(cam + 0x42) : 0, cam ? (int16_t)ld16(cam + 0x40) : 0);
        }
    }
    // the mouse as a gyro has the mouse while the game aims: no synthetic right stick then (the game
    // also ignores the gyro while the right stick is pushed)
    if (motion::mouse_drives_gyro()) g_dx = g_dy = 0;
    // mouse camera, swipe camera
    const bool mouse_active = mouse_camera() && (mouse_captured() || test_mouse()) && !motion::mouse_drives_gyro();
    const bool swipe = recent(g_swipe_step, 8);
    if (!mouse_active) g_dx = g_dy = 0;
    bool active = mouse_active || swipe;
    bool stick_free = std::fabs(s.rx) < 0.2f && std::fabs(s.ry) < 0.2f;
    if (active && stick_free && recent(g_move_step, 8)) {
        // Snapshot once per logic step. Fallback input and direct hooks share the same movement;
        // a fallback pad read must not consume the deltas before manualCamera gets them.
        if (g_sample_step != now) {
            g_sample_step = now;
            const float sens = mouse_sensitivity();
            const float boat = camera_swipe::step_factor(link_in_ship(), aim, touch_options::get().swipe_boat);
            // degrees for the direct sites; the fallback stick sees mouse points (the swipe as points at
            // the nominal sensitivity, so its stick does not depend on the mouse setting)
            g_direct_dx = g_dx * sens + g_swipe_dx * boat;
            g_direct_dy = g_dy * sens + g_swipe_dy * boat;
            const float px = g_dx + g_swipe_dx * boat / camera_swipe::kNominalSensitivity;
            const float py = g_dy + g_swipe_dy * boat / camera_swipe::kNominalSensitivity;
            g_mouse_rx = camera_mouse::fallback_stick(px);
            g_mouse_ry = camera_mouse::fallback_stick(-py);
            g_dx = g_dy = 0;
            g_swipe_dx = g_swipe_dy = 0;
            if (trace_on()) trace("%s input degrees dx=%.3f dy=%.3f rx=%.3f ry=%.3f aim=%d boat=%.2f",
                                  swipe ? "swipe" : "mouse", g_direct_dx, g_direct_dy, g_mouse_rx, g_mouse_ry, aim, boat);
        }
        s.rx = g_mouse_rx;
        s.ry = g_mouse_ry;
        g_synth = true;
        g_synth_step = now;
    } else {
        g_synth = false;
    }
    // left click while aiming an item: that item's button
    if (mouse_active && g_left && input_map::key_users(input_map::current(), input_map::kMouse1).empty()) {
        uint32_t p = link_proc();
        uint32_t l = g_link.load(std::memory_order_relaxed);
        if (l && aiming_item(p)) {
            static const uint32_t bits[3] = {input::kX, input::kY, input::kR};
            uint8_t b = ld8(l + kReadyItemBtn);
            if (b < 3) s.buttons |= bits[b];
        }
    }
    // first person on the mouse wheel: a one-step R3 press (the game toggles on R3)
    if (first_person_wheel() && std::fabs(g_wheel) >= 1.0f) {
        bool want = g_wheel > 0;  // forward: into first person, backward: out
        g_wheel = 0;
        if (now >= g_r3_ready && want != in_first_person()) {
            g_r3_from = now;
            g_r3_to = now + 2;  // held for two logic steps, so the press lands on a logic step
            g_r3_ready = now + 12;
            trace("wheel: R3 press (%s first person)", want ? "into" : "out of");
        }
    }
    if (now >= g_r3_from && now < g_r3_to) s.buttons |= input::kStickR;
    lock_on_filter_pad(s);
    editor::filter_pad(s);  // the world editor's fly camera takes the controller (the game gets nothing)
}

}  // namespace mods

using namespace mods;

// ---- dCamera_c::manualCamera sites ----
// speed for this step: mouse (degrees -> speed units with this mode's f25) or the stick, linear
static void manual_speed(Cpu* c) {
    uint32_t cam = c->r[31];
    bool mouse;
    float dx = 0;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        mouse = g_synth && g_synth_step + 1 >= step();
        if (mouse) {
            dx = g_direct_dx;
            g_direct_dx = 0;
        }
    }
    float speed;
    if (mouse) {
        // mouse right = stick right = negative speed (the camera stores stick x negated); dx in degrees
        speed = camera_mouse::yaw_speed(dx, 1.0f, c->f[25].ps0);
    } else if (direct_camera()) {
        float x = (float)ldf32(cam + kCamStickX);
        speed = std::fmax(-1.0f, std::fmin(1.0f, x)) * camera_speed();
    } else {
        return;
    }
    stf32(cam + kCamSpeed, speed);
    if (trace_on() && mouse) trace("mouse rate dx=%.3f f25=%.4f speed=%.6f", dx, c->f[25].ps0, speed);
    static uint64_t n = 0;
    if (trace_on() && (n++ % 30) == 0) trace("manual camera speed %.3f (%s)", speed, mouse ? "mouse" : "stick");
}
extern "C" void site_02510448(Cpu* c) { manual_speed(c); }
extern "C" void site_025104A0(Cpu* c) { manual_speed(c); }

// the mouse or the swipe camera fed this step (g_synth is only set while one of them is active)
static bool mouse_step() { return g_synth && g_synth_step + 1 >= step(); }

// The manual-mode wake-up belongs to the camera's decision, never to the pad: modes without
// direct mouse sites must see the proportional fallback. Let nextMode retain all its native
// restrictions (events, aiming, forced cameras); only replace its manual-start stick test.
extern "C" void hook_024FA9C8(Cpu* c) {
    const uint32_t cam = c->r[3];
    const uint32_t type = ld32(cam + 0x51C);
    bool manual = false;
    if (type < ld32(GD(0x10049348))) {  // dCamera_c::type_num
        const int16_t style = (int16_t)ld16(GD(0x10049350) + type * 0x40 + 0x18 + 12 * 2);
        manual = style >= 0 && ld32(GD(0x1004487C) + style * 0x84 + 4) == 10;  // MANUAL_CAMERA
    }
    const bool wake = manual && mouse_step() && !in_first_person() && !aiming_item(link_proc());
    const float value = (float)ldf32(cam + 0x178), y = (float)ldf32(cam + 0x174);
    if (wake) {
        stf32(cam + 0x178, 0.45f);  // above manual-start threshold, below stick-as-button threshold
        stf32(cam + 0x174, 0.0f);
    }
    f_024FA9C8_orig(c);
    if (wake) {
        stf32(cam + 0x178, value);
        stf32(cam + 0x174, y);
    }
}

// f28: vertical stick rate (+1 = stick up = V decreases by [sp+0x14] degrees per step)
extern "C" void site_0251053C(Cpu* c) {
    if (!mouse_step()) return;
    float dy;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        dy = g_direct_dy;
        g_direct_dy = 0;
    }
    double per = ldf32(c->r[1] + 0x14);
    if (!(std::fabs(per) > 0.01)) return;
    // mouse down (dy > 0) looks down: V grows, so f28 < 0 (the other way with Invert camera up/down)
    c->f[28].ps0 = -dy / per * camera_invert::mouse_pitch_sign(input_map::invert_camera_y());
}
// V smoothing (cSAngle * f1, f1 = [sp+0x18]) and U smoothing (f1 = [sp+0x1C])
extern "C" void site_02510EA0(Cpu* c) {
    if (mouse_step()) c->f[1].ps0 = 1.0;
}
extern "C" void site_02510EE4(Cpu* c) {
    if (mouse_step() || direct_camera()) c->f[1].ps0 = 1.0;
}

// manualCamera's latitude-only limited_range_addition call. f1 is the pitch delta,
// f2/f3 are the live style endpoints (after crawling/flying/lock-on adjustments).
// The game's helper reverses a descending range; cancel that for camera input.
// Keep the centre-height, radius and FOV calls, first person and aiming unchanged.
extern "C" void site_02510DEC(Cpu* c) {
    c->f[1].ps0 = camera_invert::manual_pitch_delta(c->f[1].ps0, c->f[2].ps0, c->f[3].ps0);
}

// ---- Invert camera up/down: right after the camera's CPad_GET_SUBSTICK_POS_Y calls (f1 = stick y)
static void camera_stick_y(Cpu* c, const char* site) {
    if (!input_map::invert_camera_y()) return;
    const float y = (float)c->f[1].ps0;
    c->f[1].ps0 = camera_invert::stick_y(y, true, camera_invert::Use::kCamera);
    c->f[1].ps1 = c->f[1].ps0;  // lfs result: both halves
    static uint64_t n = 0;
    if (y != 0.0f && trace_on() && (n++ % 30) == 0) trace("invert camera: %s stick y %.2f -> %.2f", site, y, -y);
}
extern "C" void site_024F9DA4(Cpu* c) { camera_stick_y(c, "024F9DA4"); }  // dCamera_c::updatePad
extern "C" void site_0250074C(Cpu* c) { camera_stick_y(c, "0250074C"); }  // dCamera_c::initPad
extern "C" void site_02506C68(Cpu* c) { camera_stick_y(c, "02506C68"); }  // dCamera_c::CalcSubjectAngle (first person, aiming)
extern "C" void site_02506C80(Cpu* c) { camera_stick_y(c, "02506C80"); }  // the same, with the game's inverted-aiming option
extern "C" void site_02171B1C(Cpu* c) { camera_stick_y(c, "02171B1C"); }  // new_himo2_move: rope-swing event camera

// Logic-step camera measurements, enabled only by WWHD_MODS_TRACE.
static void camera_trace(uint32_t cam, const char* mode, int16_t before) {
    if (!trace_on()) return;
    int16_t after = (int16_t)ld16(cam + 0x42);
    trace("camera mode=%s U=%d dU=%d V=%d manual=%llu dx=%.3f dy=%.3f synth=%d proc=%X type=%u mode=%u style=%u",
          mode, after, (int16_t)(after - before), (int16_t)ld16(cam + 0x40),
          (unsigned long long)g_manual_step, g_dx, g_dy, g_synth, link_proc(), ld32(cam + 0x51C), ld32(cam + 0x13C), ld32(cam + 0x514));
}
extern "C" void hook_0250D4E8(Cpu* c) {
    uint32_t cam = c->r[3];
    int16_t before = (int16_t)ld16(cam + 0x42);
    f_0250D4E8_orig(c);
    camera_trace(cam, "ride", before);
}

// which camera mode ran (for the mouse: manual camera vs first person)
extern "C" void hook_0250FDC8(Cpu* c) {
    g_manual_step = step();
    uint32_t cam = c->r[3];
    int16_t before = (int16_t)ld16(cam + 0x42);
    f_0250FDC8_orig(c);
    camera_trace(cam, "manual", before);
}
extern "C" void hook_025071FC(Cpu* c) {
    if (g_subject_step == ~0ull || step() > g_subject_step + 2) trace("first person (subject camera) starts");
    g_subject_step = step();
    g_cam.store(c->r[3], std::memory_order_relaxed);
    f_025071FC_orig(c);
}

// daPy_Execute: Link (left click mapping, traces)
namespace mods { void link_executed(uint32_t link); }  // turbo.cpp: control traces
extern "C" void hook_0240EBB0(Cpu* c) {
    uint32_t l = c->r[3];
    g_link.store(l, std::memory_order_relaxed);
    f_0240EBB0_orig(c);
    link_executed(l);
}
