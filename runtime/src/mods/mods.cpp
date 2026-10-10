// Gameplay mods: switches and shared helpers. See mods.h.
//
// Start-up / test switches (all off by default, also switchable from the Gameplay menu):
//   WWHD_MOD_DIRECT_CAMERA=1   direct right-stick camera (WWHD_MOD_CAMERA_SPEED=1.5 multiplier)
//   WWHD_MOD_MOUSE_CAMERA=1    mouse camera (WWHD_MOD_MOUSE_SENS=0.15 degrees per point)
//   WWHD_MOD_FIRST_PERSON=1    first person on R3 / mouse wheel
//   WWHD_MOD_QUICK_DOORS=1     quick doors
//   WWHD_MOD_FAST_SCENES=1     fast scene changes
//   WWHD_MODS_TRACE=path       log of mod decisions and timing events (door events, scene changes,
//                              Link's control), one line per event with the logic step
#include "mods.h"
#include "move_speed.h"
#include "sprint_pose.h"
#include "guest_addr.h"
#include "../input.h"
#include "../true60.h"  // dt(): the per-step tick must advance once per original 30 Hz step

#include <atomic>
#include <cstdarg>
#include <cstdlib>
#include <mutex>

#include "runtime.h"

extern "C" void f_023DE788_orig(Cpu* c);  // daPy_lk_c::setFrameCtrl(frameCtrl, attribute, start, end, rate, frame)
extern "C" void f_023E048C_orig(Cpu* c);  // daPy_lk_c::getAnmData(anmId) -> resource index
extern "C" void f_023D6B30_orig(Cpu* c);  // daPy_lk_c::jointBeforeCB(joint, transform, quaternion)

namespace interp { uint64_t logic_steps(); }

namespace mods {
namespace {
bool env_on(const char* n) {
    const char* e = getenv(n);
    return e && atoi(e) != 0;
}
float env_f(const char* n, float d) {
    const char* e = getenv(n);
    return e ? (float)atof(e) : d;
}
std::atomic<bool> g_direct{env_on("WWHD_MOD_DIRECT_CAMERA")};
std::atomic<float> g_speed{env_f("WWHD_MOD_CAMERA_SPEED", 1.0f)};
std::atomic<bool> g_mouse{env_on("WWHD_MOD_MOUSE_CAMERA")};
std::atomic<float> g_sens{env_f("WWHD_MOD_MOUSE_SENS", 0.15f)};
std::atomic<bool> g_fp{env_on("WWHD_MOD_FIRST_PERSON")};
std::atomic<bool> g_doors{env_on("WWHD_MOD_QUICK_DOORS")};
std::atomic<bool> g_move{env_on("WWHD_MOD_MOVE_SPEED")};
std::atomic<int> g_move_mode{(int)MoveMode::kHold};
// WWHD_MOD_MOVE_ANIM=native|dash|sprint is the animation test aid.
std::atomic<int> g_move_anim{[]{
    const char* e = getenv("WWHD_MOD_MOVE_ANIM");
    return (int)move_anim_from_id(e ? e : "native");
}()};
std::atomic<float> g_move_land{clamp_factor(env_f("WWHD_MOD_MOVE_FACTOR", kDefaultLandFactor))};
// before the split there was one factor: WWHD_MOD_MOVE_FACTOR still feeds swimming when the
// swimming variable is not given
std::atomic<float> g_move_swim{clamp_factor(env_f("WWHD_MOD_MOVE_SWIM", env_f("WWHD_MOD_MOVE_FACTOR", kDefaultSwimFactor)))};
std::atomic<float> g_move_stamina_seconds{clamp_stamina_seconds(env_f("WWHD_MOD_MOVE_STAMINA", kDefaultStaminaSeconds))};
std::atomic<float> g_move_cooldown_seconds{clamp_cooldown_seconds(env_f("WWHD_MOD_MOVE_COOLDOWN", kDefaultCooldownSeconds))};
std::atomic<uint32_t> g_move_button{input::kStickL};
std::atomic<uint32_t> g_move_buttons{0};  // the live sample, from input::read()
// Live tick state. Only link_move_factor (the game's logic thread) writes these; the HUD reads the
// atomic ones from the render thread (stage 3).
std::atomic<float> g_move_ramp{1.f};
std::atomic<float> g_move_stamina{1.f};  // 0..1
std::atomic<bool> g_move_exhausted{false};
bool g_move_engaged = false;   // hold: mirrors the button; toggle: the cycle is running
bool g_move_armed = false;     // toggle: a press armed a cycle that has not started moving yet
bool g_move_held_prev = false;
bool g_move_cycle_started = false; // a roll suspends movement, but does not start a second cycle
bool g_move_dust_emitted = false;
uint32_t g_move_dust_pending = 0;
std::atomic<bool> g_move_boosted{false};
std::atomic<bool> g_move_swimming{false};
std::atomic<float> g_move_hud_alpha{0.f};  // the bar's fade (climb.cpp's pattern)
float g_move_hud_hold = 0.f;               // seconds the full bar stays visible
constexpr float kRampTau = 0.15f;  // seconds; the ramp's time constant
std::atomic<bool> g_scenes{env_on("WWHD_MOD_FAST_SCENES")};

// ---- the animation cadence follows the boost (daPy_lk_c::setFrameCtrl 023DE788) ----
// The rate the game stores is the authored one: true60 only scales the advance inside
// J3DFrameCtrl::update, transiently, so scaling the stored rate composes with true 60. The hook
// records what the game asked for, the per-step tick follows the ramp, and the authored rate goes
// back when the boost ends. An entry the game overwrote itself is dropped, so the mod never fights
// the game for a field it owns.
struct AnimCtrl { uint32_t ctrl; float authored, written; };
AnimCtrl g_anim[4];
int g_anim_n = 0;

void anim_forget(uint32_t ctrl) {
    for (int i = 0; i < g_anim_n; ++i)
        if (g_anim[i].ctrl == ctrl) { g_anim[i] = g_anim[--g_anim_n]; return; }
}
void anim_scale(uint32_t ctrl, float authored, float factor) {
    for (int i = 0; i < g_anim_n; ++i)
        if (g_anim[i].ctrl == ctrl) { g_anim[i] = {ctrl, authored, authored * factor}; return; }
    if (g_anim_n < (int)(sizeof g_anim / sizeof g_anim[0])) g_anim[g_anim_n++] = {ctrl, authored, authored * factor};
}
void apply_anim_ramp() {
    const float ramp = animation_factor(g_move_ramp.load(std::memory_order_relaxed));
    for (int i = 0; i < g_anim_n;) {
        AnimCtrl& a = g_anim[i];
        if (u32_as_f32(ld32(a.ctrl)) != a.written) { a = g_anim[--g_anim_n]; continue; }
        const float next = a.authored * ramp;
        st32(a.ctrl, f32_as_u32(next));
        a.written = next;
        if (ramp == 1.f) { a = g_anim[--g_anim_n]; continue; }
        ++i;
    }
}

void note(const char* what, bool on) { LOG("[mods] %s %s", what, on ? "on" : "off"); }
}  // namespace

bool direct_camera() { return g_direct.load(std::memory_order_relaxed); }
void set_direct_camera(bool on) { g_direct = on; note("direct right-stick camera", on); }
float camera_speed() { return g_speed.load(std::memory_order_relaxed); }
void set_camera_speed(float s) {
    g_speed = s;
    LOG("[mods] camera speed x%.2f", s);
}
bool mouse_camera() { return g_mouse.load(std::memory_order_relaxed); }
void set_mouse_camera(bool on) {
    g_mouse = on;
    note("mouse camera", on);
    if (!on) mouse_release();
}
float mouse_sensitivity() { return g_sens.load(std::memory_order_relaxed); }
void set_mouse_sensitivity(float s) {
    g_sens = s;
    LOG("[mods] mouse sensitivity %.3f degrees per point", s);
}
bool first_person_wheel() { return g_fp.load(std::memory_order_relaxed); }
void set_first_person_wheel(bool on) { g_fp = on; note("first person on R3 / mouse wheel", on); }
bool quick_doors() { return g_doors.load(std::memory_order_relaxed); }
void set_quick_doors(bool on) { g_doors = on; note("quick doors", on); }
bool fast_scenes() { return g_scenes.load(std::memory_order_relaxed); }
void set_fast_scenes(bool on) { g_scenes = on; note("fast scene changes", on); }

bool move_speed() { return g_move.load(std::memory_order_relaxed); }
void set_move_speed(bool on) { g_move = on; note("run/swim speed", on); }
MoveMode move_speed_mode() { return (MoveMode)g_move_mode.load(std::memory_order_relaxed); }
void set_move_speed_mode(MoveMode mode) {
    g_move_mode = (int)mode;
    LOG("[mods] run/swim boost is %s", mode == MoveMode::kToggle ? "a toggle" : "held");
}
MoveAnim move_speed_anim() { return (MoveAnim)g_move_anim.load(std::memory_order_relaxed); }
void set_move_speed_anim(MoveAnim anim) {
    g_move_anim = (int)anim;
    LOG("[mods] run/swim boost animation: %s", move_anim_label(anim));
}
uint32_t move_boost_anim(uint32_t anm) {
    if (!move_speed() || !g_move_boosted.load(std::memory_order_relaxed)) return anm;
    return boost_anim(anm, move_speed_anim(), true);
}
float move_speed_land_factor() { return g_move_land.load(std::memory_order_relaxed); }
void set_move_speed_land_factor(float factor) { g_move_land = clamp_factor(factor); }
float move_speed_swim_factor() { return g_move_swim.load(std::memory_order_relaxed); }
void set_move_speed_swim_factor(float factor) { g_move_swim = clamp_factor(factor); }
float move_speed_stamina_seconds() { return g_move_stamina_seconds.load(std::memory_order_relaxed); }
void set_move_speed_stamina_seconds(float seconds) { g_move_stamina_seconds = clamp_stamina_seconds(seconds); }
float move_speed_cooldown_seconds() { return g_move_cooldown_seconds.load(std::memory_order_relaxed); }
void set_move_speed_cooldown_seconds(float seconds) { g_move_cooldown_seconds = clamp_cooldown_seconds(seconds); }
uint32_t move_speed_button() { return g_move_button.load(std::memory_order_relaxed); }
void set_move_speed_button(uint32_t button) {
    constexpr uint32_t allowed = input::kStickL | input::kStickR | input::kL | input::kR | input::kZL | input::kZR;
    if (button && !(button & (button - 1)) && (button & allowed)) g_move_button = button;
}
void move_speed_input(uint32_t buttons) { g_move_buttons.store(buttons, std::memory_order_relaxed); }

MoveHud move_hud() {
    return {g_move_stamina.load(std::memory_order_relaxed), g_move_hud_alpha.load(std::memory_order_relaxed),
            g_move_boosted.load(std::memory_order_relaxed), g_move_swimming.load(std::memory_order_relaxed),
            g_move_exhausted.load(std::memory_order_relaxed)};
}

// One logic step of the boost. Called from the 023FD39C site (true60_link.cpp) on every pass; the
// state advances once per original 30 Hz step (previews only reuse the ramp). Returns the factor
// the site multiplies the horizontal speed by, which is exactly 1 whenever the mod is off.
float link_move_factor(uint32_t link) {
    if (!move_speed() || !link) {
        g_move_engaged = false;
        g_move_armed = false;
        g_move_held_prev = false;
        g_move_cycle_started = false;
        g_move_dust_emitted = false;
        g_move_dust_pending = 0;
        g_move_ramp = 1.f;
        g_move_stamina = 1.f;
        g_move_exhausted = false;
        g_move_boosted = false;
        g_move_swimming = false;
        g_move_hud_alpha = 0.f;
        g_move_hud_hold = 0.f;
        apply_anim_ramp();  // ramp is 1: put the authored animation rates back
        return 1.f;
    }
    const uint32_t held = g_move_buttons.load(std::memory_order_relaxed) & move_speed_button();
    const uint32_t proc = ld32(link + 0x65F0);
    if (true60::dt() >= 1.0f) {  // a full step, not a true60 preview
        const float dt = 1.f / 30.f;
        const float seconds = move_speed_stamina_seconds();
        const bool was_held = g_move_held_prev;
        g_move_held_prev = held != 0;
        if (move_speed_mode() == MoveMode::kToggle) {
            // One press starts one cycle and another press stops it. The cycle also ends by itself the
            // moment he stops running or swimming (a forward roll only suspends the boost), or when
            // the bar runs out, so cycles never queue up
            // behind each other: boosting again always takes a new press.
            if (held && !was_held) {
                if (g_move_armed || g_move_engaged) g_move_armed = g_move_engaged = false;
                else g_move_armed = true;
            }
            if (g_move_armed && is_move_proc(proc)) g_move_engaged = true;
            if (g_move_exhausted || (g_move_engaged && !is_move_proc(proc) && proc != kProcFrontRoll))
                g_move_armed = g_move_engaged = false;
        } else {
            g_move_engaged = held != 0 && (proc != kProcFrontRoll || g_move_cycle_started);
        }
        // A roll keeps consuming the same cycle's bar, without speeding up its native movement.
        const bool boosting = g_move_engaged && !g_move_exhausted && (is_move_proc(proc) || proc == kProcFrontRoll);
        if (boosting) {
            g_move_stamina = drain_stamina(g_move_stamina, seconds, dt);
            if (g_move_stamina.load(std::memory_order_relaxed) <= 0.f) g_move_exhausted = true;
        } else {
            // the cooldown is the bar's refill time: 0 fills it at once (the boost is always ready)
            g_move_stamina = refill_stamina_over(g_move_stamina, move_speed_cooldown_seconds(), dt);
            if (g_move_stamina.load(std::memory_order_relaxed) >= 1.f) g_move_exhausted = false;
        }
        const bool active = g_move_engaged && !g_move_exhausted;
        g_move_boosted = active && (is_move_proc(proc) || proc == kProcFrontRoll);
        g_move_swimming = active && proc == kProcSwimMove;
        if (!g_move_boosted) {
            g_move_cycle_started = false;
            g_move_dust_emitted = false;
            g_move_dust_pending = 0;
        }
        if (active && is_move_proc(proc) && move_target(true, proc, move_speed_land_factor(), move_speed_swim_factor()) > 1.f)
            g_move_cycle_started = true;
        if (active && proc == kProcMove && move_speed_land_factor() > 1.f && !g_move_dust_emitted) {
            g_move_dust_pending = link;
        }
        // The loop is over the moment he stops running or swimming: the factor and the animation go
        // back to normal at once instead of easing out, so nothing stays boosted while he stands.
        g_move_ramp = is_move_proc(proc)
                          ? ramp_towards(g_move_ramp, move_target(active, proc, move_speed_land_factor(),
                                                                  move_speed_swim_factor()),
                                         kRampTau, dt)
                          : 1.f;
        apply_anim_ramp();  // the legs follow the ramp, so they do not skate at 2x
        // HUD: shown while boosting or while the bar refills, held a second, then faded out
        const bool show = boosting || g_move_stamina.load(std::memory_order_relaxed) < 1.f;
        if (show) g_move_hud_hold = 1.f;
        else if (g_move_hud_hold > 0.f) g_move_hud_hold -= dt;
        const float target = (show || g_move_hud_hold > 0.f) ? 1.f : 0.f;
        float alpha = g_move_hud_alpha.load(std::memory_order_relaxed);
        alpha += (target - alpha) * std::min(1.f, dt * 8.f);
        if (target == 0.f && alpha < 0.01f) alpha = 0.f;
        g_move_hud_alpha = alpha;
        if (trace_on()) {
            static uint32_t previous_proc = UINT32_MAX;
            if (proc != previous_proc) {
                trace("move-speed proc=%X cycle=%d stamina=%.3f factor=%.3f", proc,
                      g_move_boosted.load() ? 1 : 0, g_move_stamina.load(), g_move_ramp.load());
                previous_proc = proc;
            }
        }
    }
    return g_move_ramp.load(std::memory_order_relaxed);
}

void move_start_effect(Cpu* c, uint32_t link) {
    if (!c || !link || g_move_dust_pending != link || true60::dt() < 1.f) return;
    if (move_speed_land_factor() <= 1.f) { g_move_dust_pending = 0; return; }
    // Reuse the native one-shot land-smoke helper, which stops emission after one particle step.
    // Keep the ordinary foot emitters untouched. No dust in water, midair, lock-on or a demo.
    const uint32_t play = GD(0x1046F0B0), manager = ld32(play + 0x5AB0);
    if (!manager || !move_speed() || ld32(link + 0x65F0) != kProcMove ||
        !(ld32(link + 0x834) & 0x20) || (ld32(link + 0x6A70) & (1u | 2u | 0x40000u)) ||
        ld32(link + 0x69D4) == 0x13 || ld16(link + 0x420) || ld8(play + 0x5292)) return;
    g_move_dust_pending = 0;
    g_move_dust_emitted = true;
    const Cpu saved = *c;
    const uint32_t scratch = mem::fixed_slot(mem::kFixLinkScratch), effect_id = scratch + 0x40;
    st32(effect_id, 0);
    const float angle = (int16_t)ld16(link + 0x32A) * 0.00009587379924f;
    const float s = std::sin(angle), co = std::cos(angle);
    const float x = u32_as_f32(ld32(link + 0x314)), y = u32_as_f32(ld32(link + 0x318)),
                z = u32_as_f32(ld32(link + 0x31C));
    uint32_t first_emitter = 0, puffs = 0;
    // One brief fan behind the feet reads as a burst rather than an ordinary single footstep.
    for (unsigned i = 0; i < 3; ++i) {
        const float side = ((int)i - 1) * 18.f;
        const uint32_t pos = scratch + 0x50 + i * 0x10;
        st32(pos, f32_as_u32(x - 18.f * s + side * co));
        st32(pos + 4, f32_as_u32(y + 2.f));
        st32(pos + 8, f32_as_u32(z - 18.f * co - side * s));
        c->f[1].ps0 = 2.4f; c->f[2].ps0 = 1.f; c->f[3].ps0 = 1.f;
        const uint32_t emitter = guest_call(c, GC(0x025A87C0),
            {manager, 0, pos, link + 0x328, link + 0x110, effect_id, 0x11});
        *c = saved;
        if (emitter) {
            st8(emitter + 0x247, 0xC0); // emitter alpha; the native default is only half opaque
            if (!first_emitter) first_emitter = emitter;
            ++puffs;
        }
    }
    if (!first_emitter) g_move_dust_emitted = false;
    if (trace_on()) trace("sprint-dust link=%08X emitter=%08X puffs=%u", link, first_emitter, puffs);
}

// daPy_lk_c::setFrameCtrl(frameCtrl, attribute, start, end, rate, frame): r4 = frameCtrl, f1 = rate.
// While a boost runs on Link in a movement procedure the rate follows the ramp, so the legs keep up
// with the ground instead of skating. Scaling the argument also covers the next animation the
// procedure starts; apply_anim_ramp keeps a running one in step while the ramp moves.
extern "C" void hook_023DE788(Cpu* c) {
    const float factor = animation_factor(g_move_ramp.load(std::memory_order_relaxed));
    if (factor != 1.f && move_speed() && is_move_proc(ld32(c->r[3] + 0x65F0))) {
        anim_scale(c->r[4], (float)c->f[1].ps0, factor);
        c->f[1].ps0 = (float)(c->f[1].ps0 * factor);
    } else {
        anim_forget(c->r[4]);
    }
    f_023DE788_orig(c);
}

// daPy_lk_c::getAnmData(anmId): r4 is the animation id. The draw of the locomotion clip happens here
// (setBlendMoveAnime resolves ANM_WALK through it), so the boost can swap it for the game's own dash
// motion without touching the blend itself.
extern "C" void hook_023E048C(Cpu* c) {
    c->r[4] = move_boost_anim(c->r[4]);
    f_023E048C_orig(c);
}

extern "C" void hook_023D6B30(Cpu* c) {
    const uint32_t link = c->r[3], joint = c->r[4], quaternion = c->r[6];
    f_023D6B30_orig(c);
    if (!link || !quaternion || !move_speed() || move_speed_anim() != MoveAnim::kSprint) return;
    // Ordinary forward locomotion only: leave upper-body actions, lock-on, demos and special walks
    // to their own animation. The resource index (not ANM id) is what the live heap stores.
    // Free running retains DIR_NONE (4). DIR_FORWARD (0) is the explicit directional case.
    const uint8_t direction = ld8(link + 0x68D4);
    if (ld32(link + 0x65F0) != kProcMove || (direction != 0 && direction != 4) ||
        (ld32(link + 0x6A70) & (1u | 2u | 0x40000u)) || ld16(link + 0x420) != 0 ||
        ld8(GD(0x1046F0B0) + 0x5292) || ld16(link + 0x5888) != 0xFFFF) return;
    const uint32_t table = GD(0x100366A0);
    const uint16_t clip = ld16(link + 0x5858);
    if (clip != ld16(table + kAnmWalk * 8) && clip != ld16(table + kAnmDash * 8)) return;
    const float weight = sprint::weight(g_move_ramp.load(std::memory_order_relaxed), move_speed_land_factor(),
                                         u32_as_f32(ld32(link + 0x6A14)), u32_as_f32(ld32(link + 0x3C4)));
    const uint32_t ctrl = link + 0x58A8;  // mFrameCtrlUnder[1], the native walk/run cycle
    const int start = (int16_t)ld16(ctrl + 8), end = (int16_t)ld16(ctrl + 10);
    const float phase = end > start ? (u32_as_f32(ld32(ctrl + 4)) - start) / (end - start) : 0.f;
    const float angle = weight * sprint::angle_degrees(joint, phase);
    if (angle == 0.f) return;
    const sprint::Quaternion q{u32_as_f32(ld32(quaternion)), u32_as_f32(ld32(quaternion + 4)),
                                u32_as_f32(ld32(quaternion + 8)), u32_as_f32(ld32(quaternion + 12))};
    if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w)) return;
    // jointAfterCB restores the quaternion via Link's per-joint flag/backup. If the game already
    // saved it, preserve that original; otherwise ask the same native path to restore our edit.
    const uint32_t flags = link + 0x68E2 + joint, backup = link + 0x6AB0 + joint * 16;
    if (!(ld8(flags) & 1)) {
        for (unsigned i = 0; i < 4; ++i) st32(backup + i * 4, ld32(quaternion + i * 4));
        st8(flags, ld8(flags) | 1);
    }
    const auto posed = sprint::rotate_local_z(q, angle);
    st32(quaternion, f32_as_u32(posed.x)); st32(quaternion + 4, f32_as_u32(posed.y));
    st32(quaternion + 8, f32_as_u32(posed.z)); st32(quaternion + 12, f32_as_u32(posed.w));
    if (joint == sprint::kStomach && trace_on())
        trace("sprint-pose link=%08X weight=%.3f lean=%.2f phase=%.3f", link, weight, angle, phase);
}

uint64_t step() { return interp::logic_steps(); }double game_time() { return (double)interp::logic_steps() / 30.0; }

static FILE* g_trace = [] {
    const char* p = getenv("WWHD_MODS_TRACE");
    return p ? fopen(p, "w") : nullptr;
}();
bool trace_on() { return g_trace != nullptr; }
void trace(const char* fmt, ...) {
    if (!g_trace) return;
    static std::mutex mu;
    std::lock_guard<std::mutex> lk(mu);
    fprintf(g_trace, "%llu ", (unsigned long long)step());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_trace, fmt, ap);
    va_end(ap);
    fputc('\n', g_trace);
    fflush(g_trace);
}

}  // namespace mods
