// Gameplay options (Gameplay menu). Original behavior by default; each can be switched live.
//   camera.cpp  direct right-stick camera, mouse camera, first person on R3 / mouse wheel
//   lock_on.cpp optional enemy range and R3 cycling through the native eligible list
//   mouse.mm    mouse capture in the game window (macOS events)
//   turbo.cpp   quick doors and fast scene changes (extra logic steps while they run)
//   cheats.cpp  items, sword/shield, stats, infinite health/magic/ammo, songs, Triforce, dungeon items
// Test/start-up switches: WWHD_MOD_<NAME>=1 (see mods.cpp).
#pragma once
#include <cstdint>

#include "move_speed.h"  // MoveMode (the run/swim speed mod's public vocabulary)
#include "fast_forward.h"

struct Cpu;
namespace input { struct PadState; }

namespace mods {

// ---- switches (menu + env) ----
bool direct_camera();
bool letterbox_while_targeting();
void set_letterbox_while_targeting(bool on);
void set_direct_camera(bool on);
float camera_speed();  // direct camera: multiplier of the game's own top turning speed
void set_camera_speed(float s);
bool mouse_camera();
void set_mouse_camera(bool on);
float mouse_sensitivity();  // degrees per mouse point
void set_mouse_sensitivity(float s);
bool first_person_wheel();
void set_first_person_wheel(bool on);
bool quick_doors();
void set_quick_doors(bool on);
bool fast_scenes();
void set_fast_scenes(bool on);

// Optional enemy targeting assistance. Off preserves the game's targeting unchanged.
bool extended_lock_on();
void set_extended_lock_on(bool on);
float lock_on_range(); // multiplier of native enemy acquisition/release distances, 1..3
void set_lock_on_range(float factor);
bool lock_on_cycling();
void set_lock_on_cycling(bool on);
void lock_on_filter_pad(input::PadState& s); // reserve R3 only while enemy-locked
void lock_on_reset(); // focus loss, overlays and save/load require a fresh press

bool move_speed();
void set_move_speed(bool on);
MoveMode move_speed_mode();                      // hold the button, or press it once
void set_move_speed_mode(MoveMode mode);
MoveAnim move_speed_anim();                      // native cadence, early dash, or additive sprint pose
void set_move_speed_anim(MoveAnim anim);
uint32_t move_boost_anim(uint32_t anm);          // the locomotion clip to use right now (getAnmData)
float move_speed_land_factor();                  // running (PROC_MOVE)
void set_move_speed_land_factor(float factor);
float move_speed_swim_factor();                   // swimming (PROC_SWIM_MOVE)
void set_move_speed_swim_factor(float factor);
float move_speed_stamina_seconds();              // seconds of boosting on a full bar; 0 = unlimited
void set_move_speed_stamina_seconds(float seconds);
float move_speed_cooldown_seconds();             // wait after the bar empties; 0 = none
void set_move_speed_cooldown_seconds(float seconds);
bool move_speed_bar();                           // "Show boost bar": the bar is drawn (the boost works either way)
void set_move_speed_bar(bool on);
uint32_t move_speed_button();
void set_move_speed_button(uint32_t button);
void move_speed_input(uint32_t buttons); // actual active-controller sample, including replay/held half steps
float link_move_factor(uint32_t link);
void move_start_effect(Cpu* c, uint32_t link); // consume a new grounded sprint's one-shot dust burst
void move_trail_effect(Cpu* c, uint32_t link); // small puffs behind the feet while running under boost
void move_swim_effect(Cpu* c, uint32_t link);  // splash per arm pull and a foam trail while swimming under boost

// ---- traversal mods (traversal.cpp; issues #95, #96). Factors are clamped to 1x..3x. ----
bool ladder_speed();                      // faster ladder climbing (the clips play faster)
void set_ladder_speed(bool on);
float ladder_speed_factor();
void set_ladder_speed_factor(float factor);
bool sidle_speed();                       // faster sidling along walls
void set_sidle_speed(bool on);
float sidle_speed_factor();
void set_sidle_speed_factor(float factor);
bool ivy_speed();                         // faster ivy climbing (the clips play faster)
void set_ivy_speed(bool on);
float ivy_speed_factor();
void set_ivy_speed_factor(float factor);
bool hang_speed();                        // faster moving along a ledge while hanging
void set_hang_speed(bool on);
float hang_speed_factor();
void set_hang_speed_factor(float factor);
bool sail_speed();                        // sailing speed, one factor per sail
void set_sail_speed(bool on);
float sail_speed_normal_factor();         // the normal sail
void set_sail_speed_normal_factor(float factor);
float sail_speed_swift_factor();          // the HD Swift Sail
void set_sail_speed_swift_factor(float factor);
bool auto_wind();                         // the wind follows the boat with the normal sail too
// ---- jump anywhere (jump.cpp; settings in traversal_settings.cpp) ----
bool jump_anywhere();                     // the jump button jumps from standing or running
void set_jump_anywhere(bool on);
bool double_jump();                       // once more in the air
void set_double_jump(bool on);
bool no_auto_jump();                      // the game's own jump off ledges becomes a plain fall
void set_no_auto_jump(bool on);
uint32_t jump_button();                   // one of L3, R3, L, R, ZL, ZR (default L)
void set_jump_button(uint32_t button);
inline bool valid_jump_button(uint32_t b) {
    constexpr uint32_t allowed = 0x40000 | 0x20000 | 0x20 | 0x10 | 0x80 | 0x40;
    return b && !(b & (b - 1)) && (b & allowed);
}
void jump_input(uint32_t buttons);                  // the active controller's buttons (VPAD/KPAD read)
void jump_step(Cpu* c, uint32_t link, bool before); // around daPy_lk_c::execute (climb.cpp)
void jump_reset();                                  // game thread after a state load
bool story_system();                      // letters, quests, the story notebook, mod charts (mods/required.h)
void set_story_system(bool on);
void set_auto_wind(bool on);

// hooks' helpers (traversal_settings.cpp, traversal.cpp)
bool traversal_anim();                          // a mod that scales Link's clips is on (ladders, sidling)
float traversal_anim_factor(uint32_t proc);    // setFrameCtrl: rate factor in Link's procedure `proc` (1 = stock)
float traversal_getter_factor(int getter, uint32_t proc);  // traversal::AnmGetter's rate factor (1 = stock)
// mMaxNormalSpeed x the sidle factor for the length of one call (setNormalSpeedF with its steps,
// setBlendWHideMoveAnime), while Link sidles with the mod on; a no-op otherwise
struct SidleScope { uint32_t link = 0, stock = 0, scaled = 0; };
SidleScope sidle_scope_begin(Cpu* c, bool speed_steps);
void sidle_scope_end(const SidleScope& scope);

// The boost's on-screen bar (Vulkan present.cpp, Metal mod_hud.mm), read from the render thread.
struct MoveHud {
    float stamina;    // 0..1
    float alpha;      // 0 = hidden
    bool boosted;     // a boost cycle is running (including a forward roll within that cycle)
    bool swimming;    // ... while swimming: the bar takes its colour
    bool exhausted;   // out of stamina: the bar is refilling over the cooldown
};
MoveHud move_hud();
// ---- the gameplay HUDs and the game's own screens (hud_gate.cpp) ----
// once per frame on the game thread (interp.cpp, frame start): reads whether a game menu, an event
// or a message is up and advances the HUDs' fade over it
void hud_service();
// 0..1, multiplies the boost bar's and the climb wheel's own alpha (mod_hud.mm, vulkan/present.cpp):
// 0 while a game menu, a cutscene/event or a dialogue is shown. Any thread.
float game_ui_hud_alpha();

// ---- input (input.mm) ----
// called at the end of input::read(): synthetic stick and buttons (mouse camera, R3 pulses)
void filter_pad(input::PadState& s);
// a key went down in the game window (before the game's own mapping); true = consumed
bool host_key_down(uint16_t code);

// ---- mouse (mouse.mm -> camera.cpp) ----
void mouse_add(float dx, float dy);  // points moved while captured (dy > 0 = down)
void mouse_wheel(float dy);          // wheel / scroll (dy > 0 = forward)
void mouse_button(int button, bool down);  // 0 left, 1 right
bool mouse_captured();
void mouse_init(void* tv_window);  // NSWindow* of the TV picture (menu.mm, main thread)
void mouse_release();  // mouse.mm: release the pointer (also called when the mod is switched off)

// ---- logic (interp.cpp: fpcEx_Handler) ----
// after a normal logic step: runs extra steps while a door event or a scene change is in progress
void after_execute(Cpu* c, uint32_t execute_fn);

// ---- cheats (cheats.cpp): one-shot save data edits, applied at the top of the next frame ----
enum Cheat {
    kCheatItems = 1, kCheatSword = 2, kCheatStats = 4,
    // story progress (can change or break story events)
    kCheatSongs = 8, kCheatTriforce = 16, kCheatDungeon = 32, kCheatKey = 64,
    kCheatBags = 128,  // spoils and bait bag filled (not story progress)
};
void request_cheat(int which);  // any thread
enum Infinite {
    kInfHealth = 1, kInfMagic = 2, kInfAmmo = 4, kMoonJump = 8, kMoonNoFall = 16,
    kInfRupees = 32, kInfBags = 64, kInfBreath = 128,
};
bool infinite(int which);
void set_infinite(int which, bool on);
void moon_jump_input(const input::PadState& pad);
void moon_jump_camera(uint32_t camera);
void moon_jump_reset(); // game thread after state restore
void moon_jump_step(Cpu* cpu, uint32_t link, bool before);
bool moon_jump_move(uint32_t link, float& x, float& z);
void cheats_service();          // game main thread, frame start (interp.cpp)

// shared helpers (mods.cpp)
uint64_t step();    // full logic steps so far (interp::logic_steps)
double game_time(); // seconds of game time (steps / 30)
bool trace_on();    // WWHD_MODS_TRACE: log mod decisions
void trace(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

}  // namespace mods
