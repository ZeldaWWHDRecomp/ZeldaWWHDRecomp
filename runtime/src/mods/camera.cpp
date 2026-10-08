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
//   while the mouse moves, a small right-stick x (0.45, above the 0.3 threshold, below the 0.5
//   stick-as-button threshold) is fed to the game (filter_pad) to keep it in the manual mode.
//   In first person (dCamera_c::subjectCamera 025071FC ran) the mouse drives both right-stick axes
//   instead (rate control), which the game uses to look around / aim.
//   Left click: while Link aims an item (bow, boomerang, hookshot, rope; procedures 0x76, 0x80-0x8D,
//   0x94-0x95) it presses the item's button (daPy_lk_c+0x68D9 mReadyItemBtn: 0 X, 1 Y, 2 R; read
//   from daPy_lk_c::itemTrigger 023EAB40). Otherwise left click does nothing.
// First person: R3 already toggles the game's first-person look (measured: R3 at rest -> camera
//   distance 250 -> 20 in 7 steps, R3 again -> back). The mod adds the mouse wheel: forward enters
//   first person, backward leaves it, as a one-step R3 press.
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

#include "input.h"
#include "mods.h"
#include "../motion/motion.h"
#include "runtime.h"

extern "C" {
void f_0250FDC8_orig(Cpu* c);  // dCamera_c::manualCamera
void f_025071FC_orig(Cpu* c);  // dCamera_c::subjectCamera
void f_0240EBB0_orig(Cpu* c);  // daPy_Execute
}

namespace mods {
namespace {
constexpr uint32_t kCamSpeed = 0x154;   // dCamera_c: eased turning speed (manual camera)
constexpr uint32_t kCamStickX = 0x170;  // dCamera_c: right stick x as the camera sees it (negated)
constexpr float kSpeedToDeg = 0.92f;    // manualCamera: yaw degrees per step = speed * 0.92 * f25
constexpr uint32_t kCurProc = 0x65F0;   // daPy_lk_c::mCurProc
constexpr uint32_t kReadyItemBtn = 0x68D9;  // daPy_lk_c::mReadyItemBtn

std::mutex g_mu;
float g_dx = 0, g_dy = 0;       // mouse movement not yet used by the camera (points)
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
bool aiming_item(uint32_t p) { return p == 0x76 || (p >= 0x80 && p <= 0x8D) || p == 0x94 || p == 0x95; }

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
    std::lock_guard<std::mutex> lk(g_mu);
    g_dx += dx;
    g_dy += dy;
    g_move_step = step();
}
void mouse_wheel(float dy) {
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
    // mouse camera
    bool active = mouse_camera() && (mouse_captured() || test_mouse()) && !motion::mouse_drives_gyro();
    bool stick_free = std::fabs(s.rx) < 0.2f && std::fabs(s.ry) < 0.2f;
    if (active && stick_free && recent(g_move_step, 8)) {
        if (in_first_person() || aiming_item(link_proc())) {
            // first person / aiming: the game looks around with the right stick (rate control); the
            // movement since the last step sets the deflection
            static uint64_t taken = ~0ull;
            static float rx = 0, ry = 0;
            if (taken != now) {
                taken = now;
                const float k = 0.08f;  // stick per point per step
                rx = std::fmax(-1.0f, std::fmin(1.0f, g_dx * k));
                ry = std::fmax(-1.0f, std::fmin(1.0f, -g_dy * k));
                g_dx = g_dy = 0;
            }
            s.rx = rx;
            s.ry = ry;
        } else {
            // keep the camera in its manual mode; the manualCamera sites take the movement itself
            static float sign = 1.0f;
            if (g_dx != 0) sign = g_dx > 0 ? 1.0f : -1.0f;
            s.rx = 0.45f * sign;
            s.ry = 0.0f;
        }
        g_synth = true;
        g_synth_step = now;
    } else {
        g_synth = false;
    }
    // left click while aiming an item: that item's button
    if (active && g_left) {
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
            dx = g_dx;
            g_dx = 0;
        }
    }
    float speed;
    if (mouse && mouse_camera()) {
        double f25 = c->f[25].ps0;
        if (!(f25 > 0.01)) f25 = 8.0;
        // mouse right = stick right = negative speed (the camera stores stick x negated)
        speed = (float)(-dx * mouse_sensitivity() / (kSpeedToDeg * f25));
    } else if (direct_camera()) {
        float x = (float)ldf32(cam + kCamStickX);
        speed = std::fmax(-1.0f, std::fmin(1.0f, x)) * camera_speed();
    } else {
        return;
    }
    stf32(cam + kCamSpeed, speed);
    static uint64_t n = 0;
    if (trace_on() && (n++ % 30) == 0) trace("manual camera speed %.3f (%s)", speed, mouse ? "mouse" : "stick");
}
extern "C" void site_02510448(Cpu* c) { manual_speed(c); }
extern "C" void site_025104A0(Cpu* c) { manual_speed(c); }

static bool mouse_step() { return mouse_camera() && g_synth && g_synth_step + 1 >= step(); }

// f28: vertical stick rate (+1 = stick up = V decreases by [sp+0x14] degrees per step)
extern "C" void site_0251053C(Cpu* c) {
    if (!mouse_step()) return;
    float dy;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        dy = g_dy;
        g_dy = 0;
    }
    double per = ldf32(c->r[1] + 0x14);
    if (!(std::fabs(per) > 0.01)) return;
    // mouse down (dy > 0) looks down: V grows, so f28 < 0
    c->f[28].ps0 = -dy * mouse_sensitivity() / per;
}
// V smoothing (cSAngle * f1, f1 = [sp+0x18]) and U smoothing (f1 = [sp+0x1C])
extern "C" void site_02510EA0(Cpu* c) {
    if (mouse_step()) c->f[1].ps0 = 1.0;
}
extern "C" void site_02510EE4(Cpu* c) {
    if (mouse_step() || direct_camera()) c->f[1].ps0 = 1.0;
}

// which camera mode ran (for the mouse: manual camera vs first person)
extern "C" void hook_0250FDC8(Cpu* c) {
    g_manual_step = step();
    f_0250FDC8_orig(c);
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
