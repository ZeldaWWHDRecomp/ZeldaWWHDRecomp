// Exercises the traversal mods' hooks (runtime/src/mods/traversal.cpp) against a modelled guest:
// with each mod off a hook must leave guest memory and the registers exactly as the original code
// alone does (no extra writes, no guest calls); with it on, only the intended values change.
#include "mods/mods.h"
#include "mods/traversal.h"
#include "runtime.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace {
constexpr uint32_t kLink = 0x11000000, kShip = 0x11010000, kPlay = 0x1046F0B0;  // dComIfGp (GD identity)
constexpr uint32_t kProc = 0x65F0, kRate = 0x5898, kMax = 0x3C4, kTimer = 0x6916, kSide = 0x69C0;
constexpr uint32_t kTactWindFn = 0x0257E490, kChangeLadderFn = 0x0242757C;
void map(uint32_t address) {
#ifdef _WIN32
    auto p = VirtualAlloc(mem::ptr(address), 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    auto p = mmap(mem::ptr(address), 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    assert(p == mem::ptr(address));
}
float f32(uint32_t a) { return u32_as_f32(ld32(a)); }
void setf(uint32_t a, float v) { st32(a, f32_as_u32(v)); }

// guest calls the hooks make
unsigned wind_calls = 0, ladder_calls = 0;
int16_t wind_angle = 0;

// ---- modelled original code ----
// procLadderMove: rate stopped -> start the next rung (procLadderMove_init: setFrameCtrl with the
// stock rate times whatever setFrameCtrl's hook applies, modelled here as `init_rate`); else the stick
// rate is written, keeping the sign. `let_go` models doTrigger (procFall_init).
float stick_rate = 1.5f, init_rate = 1.5f;
bool let_go = false;
float max_seen = 0;  // mMaxNormalSpeed as setBlendWHideMoveAnime saw it
// the guest storage the original code itself touches, compared byte for byte
std::vector<uint8_t> snapshot(uint32_t a, size_t n) { return {mem::ptr(a), mem::ptr(a) + n}; }
}  // namespace

unsigned rungs = 0;
void next_rung(uint32_t link) {  // changeLadderMoveProc -> procLadderMove_init
    setf(link + kRate, init_rate);
    st16(link + kTimer, 1);
    ++rungs;
}
void log_msg(const char*, ...) {}
uint32_t guest_call(Cpu* c, uint32_t fn, std::initializer_list<uint32_t> args) {
    const std::vector<uint32_t> v(args);
    if (fn == kChangeLadderFn) {
        assert(v.size() == 2 && v[0] == kLink && v[1] == ld32(kLink + kSide));
        ++ladder_calls;
        next_rung(kLink);
        c->r[31] = 0xDEADBEEF;
        return 0;
    }
    assert(fn == kTactWindFn && v.size() == 2 && v[0] == 0);
    ++wind_calls;
    wind_angle = (int16_t)v[1];
    c->r[31] = 0xDEADBEEF;
    c->f[29].ps0 = -1.f;
    return 0;
}
extern "C" void f_02427CA8_orig(Cpu* c) {
    const uint32_t link = c->r[3];
    if (let_go) { st32(link + kProc, 0x27); c->r[3] = 1; return; }
    const float rate = f32(link + kRate);
    if (std::fabs(rate) < 0.01f) {
        const int16_t t = (int16_t)ld16(link + kTimer);
        if (t > 0) st16(link + kTimer, (uint16_t)(t - 1));  // the footstep
        else next_rung(link);
    } else {
        setf(link + kRate, rate < 0 ? -stick_rate : stick_rate);
    }
    c->r[3] = 1;
    c->r[4] = 0x77;  // volatile registers need not keep their values
}
extern "C" void f_0242A218_orig(Cpu* c) {
    max_seen = f32(c->r[3] + kMax);
    setf(c->r[3] + 0x6A14, 3.f);  // the blend writes some of Link's fields
    c->f[1].ps0 = 0;
}
// the clip rate getters: return a fixed rate in f1, clobber r3 as a real call may
extern "C" void f_02417FF4_orig(Cpu* c) { c->f[1].ps0 = c->f[1].ps1 = 0.8; c->r[3] = 0x77; }
extern "C" void f_024291E0_orig(Cpu* c) { c->f[1].ps0 = c->f[1].ps1 = 0.6; c->r[3] = 0x77; }
extern "C" void f_02427D8C_orig(Cpu* c) { c->f[1].ps0 = c->f[1].ps1 = 1.2; c->r[3] = 0x77; }
extern "C" void hook_02417FF4(Cpu*);
extern "C" void hook_024291E0(Cpu*);
extern "C" void hook_02427D8C(Cpu*);
extern "C" void hook_02427CA8(Cpu*);
extern "C" void hook_0242A218(Cpu*);
extern "C" void site_0247D4E8(Cpu*);
extern "C" void site_0247E988(Cpu*);
extern "C" void site_0247EB88(Cpu*);
extern "C" void site_0247ED24(Cpu*);

static void all_off() {
    mods::set_ladder_speed(false);
    mods::set_sidle_speed(false);
    mods::set_sail_speed(false);
    mods::set_auto_wind(false);
    mods::set_ivy_speed(false);
    mods::set_hang_speed(false);
}

static void test_ivy_hang() {
    auto call = [](void (*hook)(Cpu*), uint32_t proc) {
        st32(kLink + kProc, proc);
        const auto before = snapshot(kLink, 0x7000);
        Cpu c{}; c.r[3] = kLink;
        hook(&c);
        assert(snapshot(kLink, 0x7000) == before);  // the getters only change the returned rate
        return (float)c.f[1].ps0;
    };
    all_off();
    mods::set_ivy_speed_factor(3.f); mods::set_hang_speed_factor(3.f);  // dials alone change nothing
    assert(call(hook_02417FF4, 0x3F) == 0.8f && call(hook_024291E0, 0x40) == 0.6f && call(hook_02427D8C, 0x50) == 1.2f);
    mods::set_ivy_speed(true);
    mods::set_ivy_speed_factor(2.f);
    assert(call(hook_02417FF4, 0x3F) == 1.6f);   // ivy up / down
    assert(call(hook_02417FF4, 0x3C) == 0.8f);   // a ladder rung: its own path
    assert(call(hook_024291E0, 0x40) == 1.2f);   // ivy sideways
    assert(call(hook_02427D8C, 0x50) == 1.2f);   // hanging: its own switch
    mods::set_hang_speed(true);
    mods::set_hang_speed_factor(1.5f);
    assert(std::fabs(call(hook_02427D8C, 0x50) - 1.8f) < 1e-6f);
    all_off();
}

// with the mod off, hook == original alone (memory and registers), and no guest calls
static void test_off_identical() {
    all_off();
    const unsigned wind0 = wind_calls, ladder0 = ladder_calls;
    for (int timer : {0, 1})
    for (float rate : {0.f, 1.5f, -1.5f}) {
        st32(kLink + kProc, 0x3C);
        st16(kLink + kTimer, (uint16_t)timer);
        setf(kLink + kRate, rate);
        Cpu a{}; a.r[3] = kLink;
        const auto before = snapshot(kLink, 0x8000);
        f_02427CA8_orig(&a);
        const auto stock = snapshot(kLink, 0x8000);
        memcpy(mem::ptr(kLink), before.data(), before.size());
        Cpu b{}; b.r[3] = kLink;
        hook_02427CA8(&b);
        assert(snapshot(kLink, 0x8000) == stock && memcmp(&a, &b, sizeof a) == 0);
    }
    st32(kLink + kProc, 0x15);
    setf(kLink + kMax, 17.f);
    {
        Cpu a{}; a.r[3] = kLink; a.f[1].ps0 = -1.0;
        const auto before = snapshot(kLink, 0x8000);
        f_0242A218_orig(&a);
        const auto stock = snapshot(kLink, 0x8000);
        memcpy(mem::ptr(kLink), before.data(), before.size());
        Cpu b{}; b.r[3] = kLink; b.f[1].ps0 = -1.0;
        hook_0242A218(&b);
        assert(snapshot(kLink, 0x8000) == stock && memcmp(&a, &b, sizeof a) == 0 && max_seen == 17.f);
        Cpu c{}; c.r[3] = kLink; c.f[1].ps0 = 2.0; c.f[3].ps0 = 2.0; c.f[4].ps0 = .25;
        const Cpu c0 = c;
        const auto scope = mods::sidle_scope_begin(&c, true);
        assert(!scope.link && memcmp(&c, &c0, sizeof c) == 0 && f32(kLink + kMax) == 17.f);
        mods::sidle_scope_end(scope);
        assert(f32(kLink + kMax) == 17.f);
    }
    // the ship's sites: registers and memory untouched, no guest calls
    st16(kShip + 0x32A, 0x1234);
    for (auto site : {site_0247D4E8, site_0247E988, site_0247EB88, site_0247ED24}) {
        Cpu c{}; c.r[31] = kShip; c.f[29].ps0 = c.f[29].ps1 = 33.f;
        const Cpu c0 = c;
        const auto ship = snapshot(kShip, 0x1000);
        site(&c);
        assert(memcmp(&c, &c0, sizeof c) == 0 && snapshot(kShip, 0x1000) == ship);
    }
    assert(wind_calls == wind0 && ladder_calls == ladder0);
    // the setFrameCtrl hook (mods.cpp) asks traversal_anim(): off for every procedure
    assert(!mods::traversal_anim());
    for (uint32_t proc = 0; proc < 256; ++proc) assert(mods::traversal_anim_factor(proc) == 1.f);
}

static void test_ladder() {
    all_off();
    mods::set_ladder_speed(true);
    mods::set_ladder_speed_factor(2.f);
    assert(mods::traversal_anim() && mods::traversal_anim_factor(0x3C) == 2.f && mods::traversal_anim_factor(0x38) == 2.f);
    assert(mods::traversal_anim_factor(0x15) == 1.f && mods::traversal_anim_factor(0x06) == 1.f);
    st32(kLink + kProc, 0x3C);
    // a running rung clip: the stick rate the procedure wrote is doubled, climbing up and down
    for (float sign : {1.f, -1.f}) {
        setf(kLink + kRate, sign * 3.f);  // last step's (scaled) rate
        Cpu c{}; c.r[3] = kLink;
        hook_02427CA8(&c);
        assert(f32(kLink + kRate) == sign * stick_rate * 2.f);
    }
    // the clip ended: the footstep step starts the next rung at once (the game's own call), its init
    // rate came through setFrameCtrl and is kept as is
    setf(kLink + kRate, 0.f);
    st16(kLink + kTimer, 1);
    st32(kLink + kSide, 1);
    unsigned before = ladder_calls;
    { Cpu c{}; c.r[3] = kLink; c.r[31] = 0x1234; hook_02427CA8(&c); assert(c.r[31] == 0x1234 && c.r[3] == 1); }
    assert(ladder_calls == before + 1 && f32(kLink + kRate) == init_rate && ld16(kLink + kTimer) == 1);
    // with the stick released the game itself keeps calling changeLadderMoveProc (timer 0): no extra call
    setf(kLink + kRate, 0.f);
    st16(kLink + kTimer, 0);
    before = ladder_calls;
    { Cpu c{}; c.r[3] = kLink; hook_02427CA8(&c); }
    assert(ladder_calls == before && rungs > 0);
    // a whole climb: steps per rung with the 2x clip (the stub's clip: 10 frames)
    st32(kLink + kProc, 0x3C);
    setf(kLink + kRate, 0.f);
    st16(kLink + kTimer, 1);
    init_rate = 3.f;
    float frame = 9.999f;
    const unsigned r0 = rungs;
    int steps = 0;
    for (; steps < 400 && rungs - r0 < 50; ++steps) {
        if (f32(kLink + kRate) != 0.f) {  // the clip advances (J3DFrameCtrl::update), then the procedure runs
            frame = std::min(frame + f32(kLink + kRate), 9.999f);
            if (frame >= 9.999f) setf(kLink + kRate, 0.f);
        }
        Cpu c{}; c.r[3] = kLink;
        const unsigned rb = rungs;
        hook_02427CA8(&c);
        if (rungs != rb) frame = 0;
    }
    assert(steps == 1 + 49 * 4);  // the first step starts rung 1; then 4 steps per rung (8 at 1x)
    init_rate = 1.5f;
    // letting go: the procedure changed, nothing is scaled
    let_go = true;
    setf(kLink + kRate, 1.5f);
    { Cpu c{}; c.r[3] = kLink; hook_02427CA8(&c); }
    assert(ld32(kLink + kProc) == 0x27 && f32(kLink + kRate) == 1.5f);
    let_go = false;
    // the rate written does not compound over steps
    st32(kLink + kProc, 0x3C);
    for (int i = 0; i < 10; ++i) { Cpu c{}; c.r[3] = kLink; hook_02427CA8(&c); }
    assert(f32(kLink + kRate) == stick_rate * 2.f);
}

static void test_sidle() {
    all_off();
    mods::set_sidle_speed(true);
    mods::set_sidle_speed_factor(1.5f);
    assert(mods::traversal_anim_factor(0x15) == 1.5f && mods::traversal_anim_factor(0x3C) == 1.f);
    st32(kLink + kProc, 0x15);
    setf(kLink + kMax, 18.f);
    // setBlendWHideMoveAnime sees the scaled maximum, and it goes back afterwards
    { Cpu c{}; c.r[3] = kLink; hook_0242A218(&c); }
    assert(max_seen == 27.f && f32(kLink + kMax) == 18.f);
    // setNormalSpeedF: maximum and the accel/step arguments scaled, the ratio argument kept
    Cpu c{}; c.r[3] = kLink; c.f[1].ps0 = 2.0; c.f[2].ps0 = .5; c.f[3].ps0 = 2.0; c.f[4].ps0 = .25;
    auto scope = mods::sidle_scope_begin(&c, true);
    assert(f32(kLink + kMax) == 27.f && c.f[1].ps0 == 3.0 && c.f[2].ps0 == .5 && c.f[3].ps0 == 3.0 && c.f[4].ps0 == .375);
    mods::sidle_scope_end(scope);
    assert(f32(kLink + kMax) == 18.f);
    // the game set its own maximum meanwhile (checkNextMode when he leaves the wall): it stays
    scope = mods::sidle_scope_begin(&c, true);
    setf(kLink + kMax, 21.f);
    mods::sidle_scope_end(scope);
    assert(f32(kLink + kMax) == 21.f);
    // other procedures: untouched
    st32(kLink + kProc, 0x14);
    setf(kLink + kMax, 18.f);
    { Cpu d{}; d.r[3] = kLink; hook_0242A218(&d); }
    assert(max_seen == 18.f && f32(kLink + kMax) == 18.f);
}

static void test_ship() {
    all_off();
    mods::set_sail_speed(true);
    mods::set_sail_speed_normal_factor(2.f);
    mods::set_sail_speed_swift_factor(1.25f);
    for (bool swift : {false, true}) {
        st8(kPlay + 0x5D2C, swift ? 1 : 0);
        Cpu c{}; c.r[31] = kShip; c.r[3] = 0x55; c.f[29].ps0 = c.f[29].ps1 = 40.f;
        site_0247EB88(&c);
        const float want = swift ? 50.f : 80.f;
        assert(c.f[29].ps0 == want && c.f[29].ps1 == want && c.r[3] == 0x55);
        site_0247ED24(&c);  // before the jump code: the stock target again
        assert(c.f[29].ps0 == 40.f);
        // a target the game replaced meanwhile (forced movement) is left alone
        c.f[29].ps0 = 40.f;
        site_0247EB88(&c);
        c.f[29].ps0 = 30.f;
        site_0247ED24(&c);
        assert(c.f[29].ps0 == 30.f);
    }
    // The jump path reads f29 after ED24: check its eligibility threshold and height
    // clamp boundaries, including a sub-threshold target that the boost takes above 20.
    mods::set_sail_speed_normal_factor(3.f);
    mods::set_sail_speed_swift_factor(3.f);
    for (bool swift : {false, true})
    for (float stock : {0.f, 15.f, 19.f, 20.f, 30.f, 40.f, 55.f, 100.f}) {
        st8(kPlay + 0x5D2C, swift);
        Cpu c{}; c.f[29].ps0 = c.f[29].ps1 = stock;
        site_0247EB88(&c);
        assert(c.f[29].ps0 == stock * 3.f);
        site_0247ED24(&c);
        assert(c.f[29].ps0 == stock && c.f[29].ps1 == stock);
    }
    // automatic wind: the Swift Sail's call, with the same angle, registers kept
    mods::set_auto_wind(true);
    st16(kShip + 0x32A, 0x0FFF);
    Cpu c{}; c.r[31] = kShip; c.f[29].ps0 = 5.f;
    const unsigned calls = wind_calls;
    site_0247E988(&c);
    assert(wind_calls == calls + 1 && wind_angle == 0x4000 && c.r[31] == kShip && c.f[29].ps0 == 5.f);
    all_off();
}

// HD setMoveAngle (0247D4B0), m0388=0: both sails share this rudder path.
// The optional guest runner uses the recompiler's actual function instead of this model.
#ifdef TRAVERSAL_GUEST_STEERING
extern "C" void f_0247D4B0(Cpu*);
extern "C" void f_024752C4(Cpu* c) { c->r[3] = 0; }  // control-angle target: no effect on yaw
extern "C" void f_024751E0(Cpu*) {}  // control-angle spring: no effect on yaw
extern "C" void f_0247D65C(Cpu*) { assert(false); }
#else
static void f_0247D4B0(Cpu* c) {
    const uint32_t ship = c->r[3];
    if (ld32(ship + 0x704) || ld32(ship + 0x70C)) return;
    c->r[31] = ship;
    c->f[10].ps0 = f32(ship + 0x370);
    site_0247D4E8(c);
    float rate = std::clamp(4.f - std::fabs((float)c->f[10].ps0) / 100.f * 3.f, .1f, 3.6f);
    const int timer = (int16_t)ld16(ship + 0x670);
    rate = timer >= 15 ? 3.6f : rate + (3.6f - rate) * timer / 15.f;
    const int delta = (int)(-rate * ((int16_t)c->r[4] >> 6));
    st16(ship + 0x32A, ld16(ship + 0x32A) + delta);
    st16(ship + 0x322, ld16(ship + 0x32A));
}
#endif

static void test_ship_turning() {
    // Steady full rudder, 30 ship steps. Fixed wind cases use a tailwind snapshot;
    // auto wind cases invoke the normal-sail hook after steering, as procSteerMove does.
    // Sweep both signs, wraparound, every slider endpoint and the default factor.
    for (bool swift : {false, true})
    for (bool wind : {false, true})
    for (float factor : {1.f, 1.5f, 2.f, 3.f})
    for (int turn_timer : {0, 7, 15, 20})
    for (int rudder : {-8192, 8192}) {
        int stock_yaw = 0;
        for (bool speed : {false, true}) {
            all_off();
            mods::set_sail_speed(speed);
            mods::set_auto_wind(wind);
            mods::set_sail_speed_normal_factor(factor);
            mods::set_sail_speed_swift_factor(factor);
            st8(kPlay + 0x5D2C, swift);
            st8(kShip + 0x635, 1);
            st32(kShip + 0x644, 0x200);
            st16(kShip + 0x670, turn_timer);
            st16(kShip + 0x32A, 0x7FFF);
            const float stock_speed = swift ? 100.f : 55.f;
            Cpu target{}; target.f[29].ps0 = stock_speed;
            site_0247EB88(&target);
            setf(kShip + 0x370, (float)target.f[29].ps0);
            site_0247ED24(&target);
            assert(target.f[29].ps0 == stock_speed); // jump target remains stock
            int yaw = 0;
            for (int step = 0; step < 30; ++step) {
                const int16_t before = (int16_t)ld16(kShip + 0x32A);
                Cpu c{}; c.r[1] = kLink + 0xF000; c.r[3] = kShip; c.r[4] = (uint32_t)rudder;
                f_0247D4B0(&c);
                yaw += (int16_t)(ld16(kShip + 0x32A) - before);
                if (!swift) { c.r[31] = kShip; site_0247E988(&c); }
                assert(f32(kShip + 0x370) == stock_speed * (speed ? factor : 1.f));
            }
            if (!speed) {
                stock_yaw = yaw;
                if (turn_timer == 0) assert(std::abs(yaw) == 30 * (swift ? 128 : 300));
            }
            else {
#ifndef TRAVERSAL_BEFORE
                assert(yaw == stock_yaw);
#else
                assert(yaw != 0); // baseline measurement intentionally retains the regression
#endif
            }
            if (turn_timer == 0 && rudder == -8192)
                printf("sail=%s speed=%d wind=%d factor=%.1f yaw_units_per_step=%.1f\n", swift ? "swift" : "normal", speed, wind, factor, yaw / 30.f);
        }
    }
    // This shared helper must leave other modes and a lowered sail untouched.
    for (int mode : {0, 1, 2, 3, 7})
    for (uint32_t flags : {0u, 0x200u}) {
        st8(kShip + 0x635, mode); st32(kShip + 0x644, flags);
        Cpu c{}; c.r[31] = kShip; c.f[10].ps0 = -165.f; c.f[10].ps1 = 123.f;
        site_0247D4E8(&c);
        assert(c.f[10].ps0 == (mode == 1 && flags ? -55.f : -165.f));
        assert(c.f[10].ps1 == 123.f);
    }
    all_off();
}

int main() {
    map(kLink); map(kShip); map(0x10460000); map(0x10470000);
#ifdef TRAVERSAL_GUEST_STEERING
    map(0x10030000);
    setf(0x1003A484, 100.f); setf(0x1003A344, 3.f); setf(0x1003A530, 4.f);
    setf(0x1003A6F4, 3.6f); setf(0x1003A474, .1f); setf(0x1003A6F8, 15.f);
    stf64(0x1003A338, 4503601774854144.0); // 0x4330000080000000
#endif
    test_off_identical();
    test_ladder();
    test_sidle();
    test_ship();
    test_ship_turning();
    test_ivy_hang();
    test_off_identical();  // switching off again goes back to the stock path
    return 0;
}
