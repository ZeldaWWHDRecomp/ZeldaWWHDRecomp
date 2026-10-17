// Traversal mods: the live switches and factors (see traversal.cpp for the hooks). No game code,
// so the mod manager tests link this file as it is.
//
// Start-up / test switches (all off by default, also switchable from the Gameplay menu):
//   WWHD_MOD_LADDER_SPEED=1    faster ladders   (WWHD_MOD_LADDER_FACTOR=1.5)
//   WWHD_MOD_SIDLE_SPEED=1     faster sidling   (WWHD_MOD_SIDLE_FACTOR=1.5)
//   WWHD_MOD_IVY_SPEED=1       faster ivy climbing (WWHD_MOD_IVY_FACTOR=1.5)
//   WWHD_MOD_HANG_SPEED=1      faster moving along a ledge while hanging (WWHD_MOD_HANG_FACTOR=1.5)
//   WWHD_MOD_SAIL_SPEED=1      sailing speed    (WWHD_MOD_SAIL_FACTOR=1.5 normal sail,
//                                                WWHD_MOD_SWIFT_SAIL_FACTOR=1.5 Swift Sail)
//   WWHD_MOD_AUTO_WIND=1       the wind follows the boat with the normal sail
//   WWHD_MOD_JUMP=1            jump anywhere    (WWHD_MOD_JUMP_DOUBLE=1 double jump,
//                                                WWHD_MOD_JUMP_NO_AUTO=1 no automatic ledge jumps,
//                                                WWHD_MOD_JUMP_BUTTON=0x20 the button, default L)
#include "mods.h"
#include "traversal.h"

#include <atomic>
#include <cstdlib>

namespace mods {
namespace {
bool env_on(const char* n) {
    const char* e = getenv(n);
    return e && atoi(e) != 0;
}
float env_factor(const char* n) {
    const char* e = getenv(n);
    return traversal::clamp_factor(e ? (float)atof(e) : traversal::kDefaultFactor);
}
std::atomic<bool> g_ladder{env_on("WWHD_MOD_LADDER_SPEED")};
std::atomic<float> g_ladder_factor{env_factor("WWHD_MOD_LADDER_FACTOR")};
std::atomic<bool> g_sidle{env_on("WWHD_MOD_SIDLE_SPEED")};
std::atomic<float> g_sidle_factor{env_factor("WWHD_MOD_SIDLE_FACTOR")};
std::atomic<bool> g_ivy{env_on("WWHD_MOD_IVY_SPEED")};
std::atomic<float> g_ivy_factor{env_factor("WWHD_MOD_IVY_FACTOR")};
std::atomic<bool> g_hang{env_on("WWHD_MOD_HANG_SPEED")};
std::atomic<float> g_hang_factor{env_factor("WWHD_MOD_HANG_FACTOR")};
std::atomic<bool> g_sail{env_on("WWHD_MOD_SAIL_SPEED")};
std::atomic<float> g_sail_normal{env_factor("WWHD_MOD_SAIL_FACTOR")};
std::atomic<float> g_sail_swift{env_factor("WWHD_MOD_SWIFT_SAIL_FACTOR")};
std::atomic<bool> g_auto_wind{env_on("WWHD_MOD_AUTO_WIND")};
std::atomic<bool> g_jump{env_on("WWHD_MOD_JUMP")};
std::atomic<bool> g_jump_double{env_on("WWHD_MOD_JUMP_DOUBLE")};
std::atomic<bool> g_jump_no_auto{env_on("WWHD_MOD_JUMP_NO_AUTO")};
std::atomic<uint32_t> g_jump_button{[] {
    const char* e = getenv("WWHD_MOD_JUMP_BUTTON");
    const uint32_t b = e ? (uint32_t)strtoul(e, nullptr, 0) : 0x20;
    return valid_jump_button(b) ? b : 0x20u;
}()};
// the story system: on unless WWHD_STORY_SYSTEM=0 (or the player turns it off)
std::atomic<bool> g_story_system{[] { const char* e = std::getenv("WWHD_STORY_SYSTEM"); return !(e && *e == '0'); }()};
constexpr auto kRelaxed = std::memory_order_relaxed;
}  // namespace

bool ladder_speed() { return g_ladder.load(kRelaxed); }
void set_ladder_speed(bool on) { g_ladder = on; }
float ladder_speed_factor() { return g_ladder_factor.load(kRelaxed); }
void set_ladder_speed_factor(float f) { g_ladder_factor = traversal::clamp_factor(f); }
bool sidle_speed() { return g_sidle.load(kRelaxed); }
void set_sidle_speed(bool on) { g_sidle = on; }
float sidle_speed_factor() { return g_sidle_factor.load(kRelaxed); }
void set_sidle_speed_factor(float f) { g_sidle_factor = traversal::clamp_factor(f); }
bool ivy_speed() { return g_ivy.load(kRelaxed); }
void set_ivy_speed(bool on) { g_ivy = on; }
float ivy_speed_factor() { return g_ivy_factor.load(kRelaxed); }
void set_ivy_speed_factor(float f) { g_ivy_factor = traversal::clamp_factor(f); }
bool hang_speed() { return g_hang.load(kRelaxed); }
void set_hang_speed(bool on) { g_hang = on; }
float hang_speed_factor() { return g_hang_factor.load(kRelaxed); }
void set_hang_speed_factor(float f) { g_hang_factor = traversal::clamp_factor(f); }
bool sail_speed() { return g_sail.load(kRelaxed); }
void set_sail_speed(bool on) { g_sail = on; }
float sail_speed_normal_factor() { return g_sail_normal.load(kRelaxed); }
void set_sail_speed_normal_factor(float f) { g_sail_normal = traversal::clamp_factor(f); }
float sail_speed_swift_factor() { return g_sail_swift.load(kRelaxed); }
void set_sail_speed_swift_factor(float f) { g_sail_swift = traversal::clamp_factor(f); }
bool auto_wind() { return g_auto_wind.load(kRelaxed); }
bool story_system() { return g_story_system.load(kRelaxed); }
void set_story_system(bool on) { g_story_system = on; }
void set_auto_wind(bool on) { g_auto_wind = on; }
bool jump_anywhere() { return g_jump.load(kRelaxed); }
void set_jump_anywhere(bool on) { g_jump = on; }
bool double_jump() { return g_jump_double.load(kRelaxed); }
void set_double_jump(bool on) { g_jump_double = on; }
bool no_auto_jump() { return g_jump_no_auto.load(kRelaxed); }
void set_no_auto_jump(bool on) { g_jump_no_auto = on; }
uint32_t jump_button() { return g_jump_button.load(kRelaxed); }
void set_jump_button(uint32_t b) { if (valid_jump_button(b)) g_jump_button = b; }

bool traversal_anim() { return ladder_speed() || sidle_speed(); }
float traversal_getter_factor(int getter, uint32_t proc) {
    return traversal::anm_getter_factor(traversal::AnmGetter(getter), proc,
                                        traversal::active_factor(ivy_speed(), ivy_speed_factor()),
                                        traversal::active_factor(hang_speed(), hang_speed_factor()));
}
float traversal_anim_factor(uint32_t proc) {
    return traversal::anim_rate_factor(proc, traversal::active_factor(ladder_speed(), ladder_speed_factor()),
                                       traversal::active_factor(sidle_speed(), sidle_speed_factor()));
}

}  // namespace mods
