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
std::atomic<float> g_move_factor{clamp_move_speed(env_f("WWHD_MOD_MOVE_FACTOR", 1.5f))};
std::atomic<uint32_t> g_move_button{input::kStickL};
std::atomic<uint32_t> g_move_buttons{0};
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
float move_speed_factor() { return g_move_factor.load(std::memory_order_relaxed); }
void set_move_speed_factor(float factor) { g_move_factor = clamp_move_speed(factor); }
uint32_t move_speed_button() { return g_move_button.load(std::memory_order_relaxed); }
void set_move_speed_button(uint32_t button) {
    constexpr uint32_t allowed = input::kStickL | input::kStickR | input::kL | input::kR | input::kZL | input::kZR;
    if (button && !(button & (button - 1)) && (button & allowed)) g_move_button = button;
}
void move_speed_input(uint32_t buttons) { g_move_buttons.store(buttons, std::memory_order_relaxed); }
float link_move_factor(uint32_t link) {
    if (!move_speed() || !link) return 1.f;
    return move_factor(true, ld32(link + 0x65F0), g_move_buttons.load(std::memory_order_relaxed),
                       move_speed_button(), move_speed_factor());
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
