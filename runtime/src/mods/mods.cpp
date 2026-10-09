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
#include "../input.h"
#include "../true60.h"  // dt(): the per-step tick must advance once per original 30 Hz step

#include <atomic>
#include <cstdarg>
#include <cstdlib>
#include <mutex>

#include "runtime.h"

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
std::atomic<float> g_move_land{clamp_factor(env_f("WWHD_MOD_MOVE_FACTOR", 1.5f))};
// before the split there was one factor: WWHD_MOD_MOVE_FACTOR still feeds swimming when the
// swimming variable is not given
std::atomic<float> g_move_swim{clamp_factor(env_f("WWHD_MOD_MOVE_SWIM", env_f("WWHD_MOD_MOVE_FACTOR", 1.5f)))};
std::atomic<float> g_move_stamina_seconds{clamp_stamina_seconds(env_f("WWHD_MOD_MOVE_STAMINA", 4.f))};
std::atomic<uint32_t> g_move_button{input::kStickL};
std::atomic<uint32_t> g_move_buttons{0};  // the live sample, from input::read()
// Live tick state. Only link_move_factor (the game's logic thread) writes these; the HUD reads the
// atomic ones from the render thread (stage 3).
std::atomic<float> g_move_ramp{1.f};
std::atomic<float> g_move_stamina{1.f};  // 0..1
std::atomic<bool> g_move_exhausted{false};
bool g_move_engaged = false;   // hold: mirrors the button; toggle: latched on press
bool g_move_held_prev = false;
std::atomic<bool> g_move_boosted{false};
std::atomic<bool> g_move_swimming{false};
constexpr float kRampTau = 0.15f;  // seconds; the ramp's time constant
std::atomic<bool> g_scenes{env_on("WWHD_MOD_FAST_SCENES")};

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
float move_speed_land_factor() { return g_move_land.load(std::memory_order_relaxed); }
void set_move_speed_land_factor(float factor) { g_move_land = clamp_factor(factor); }
float move_speed_swim_factor() { return g_move_swim.load(std::memory_order_relaxed); }
void set_move_speed_swim_factor(float factor) { g_move_swim = clamp_factor(factor); }
float move_speed_stamina_seconds() { return g_move_stamina_seconds.load(std::memory_order_relaxed); }
void set_move_speed_stamina_seconds(float seconds) { g_move_stamina_seconds = clamp_stamina_seconds(seconds); }
uint32_t move_speed_button() { return g_move_button.load(std::memory_order_relaxed); }
void set_move_speed_button(uint32_t button) {
    constexpr uint32_t allowed = input::kStickL | input::kStickR | input::kL | input::kR | input::kZL | input::kZR;
    if (button && !(button & (button - 1)) && (button & allowed)) g_move_button = button;
}
void move_speed_input(uint32_t buttons) { g_move_buttons.store(buttons, std::memory_order_relaxed); }

// One logic step of the boost. Called from the 023FD39C site (true60_link.cpp) on every pass; the
// state advances once per original 30 Hz step (previews only reuse the ramp). Returns the factor
// the site multiplies the horizontal speed by, which is exactly 1 whenever the mod is off.
float link_move_factor(uint32_t link) {
    if (!move_speed() || !link) {
        g_move_engaged = false;
        g_move_held_prev = false;
        g_move_ramp = 1.f;
        g_move_stamina = 1.f;
        g_move_exhausted = false;
        g_move_boosted = false;
        g_move_swimming = false;
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
            if (held && !was_held) g_move_engaged = !g_move_engaged;
        } else {
            g_move_engaged = held != 0;
        }
        // the bar only runs while actually moving under the boost, so standing still is free
        const bool boosting = g_move_engaged && !g_move_exhausted && is_move_proc(proc);
        if (boosting) {
            g_move_stamina = drain_stamina(g_move_stamina, seconds, dt);
            if (g_move_stamina.load(std::memory_order_relaxed) <= 0.f) g_move_exhausted = true;
        } else {
            g_move_stamina = refill_stamina(g_move_stamina, seconds, dt);
            if (g_move_stamina.load(std::memory_order_relaxed) >= 1.f) g_move_exhausted = false;
        }
        const bool active = g_move_engaged && !g_move_exhausted;
        g_move_boosted = active && is_move_proc(proc);
        g_move_swimming = active && proc == kProcSwimMove;
        g_move_ramp = ramp_towards(g_move_ramp, move_target(active, proc, move_speed_land_factor(),
                                                            move_speed_swim_factor()),
                                   kRampTau, dt);
    }
    return g_move_ramp.load(std::memory_order_relaxed);
}

uint64_t step() { return interp::logic_steps(); }
double game_time() { return (double)interp::logic_steps() / 30.0; }

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
