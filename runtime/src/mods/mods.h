// Optional gameplay mods (Gameplay menu). All off by default; each can be switched live.
//   camera.cpp  direct right-stick camera, mouse camera, first person on R3 / mouse wheel
//   mouse.mm    mouse capture in the game window (macOS events)
//   turbo.cpp   quick doors and fast scene changes (extra logic steps while they run)
//   cheats.cpp  items, sword/shield, stats, infinite health/magic/ammo, songs, Triforce, dungeon items
// Test/start-up switches: WWHD_MOD_<NAME>=1 (see mods.cpp).
#pragma once
#include <cstdint>

struct Cpu;
namespace input { struct PadState; }

namespace mods {

// ---- switches (menu + env) ----
bool direct_camera();
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

bool move_speed();
void set_move_speed(bool on);
float move_speed_factor();
void set_move_speed_factor(float factor);
uint32_t move_speed_button();
void set_move_speed_button(uint32_t button);
void move_speed_input(uint32_t buttons); // actual active-controller sample, including replay/held half steps
float link_move_factor(uint32_t link);

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
};
void request_cheat(int which);  // any thread
enum Infinite { kInfHealth = 1, kInfMagic = 2, kInfAmmo = 4 };
bool infinite(int which);
void set_infinite(int which, bool on);
void cheats_service();          // game main thread, frame start (interp.cpp)

// shared helpers (mods.cpp)
uint64_t step();    // full logic steps so far (interp::logic_steps)
double game_time(); // seconds of game time (steps / 30)
bool trace_on();    // WWHD_MODS_TRACE: log mod decisions
void trace(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

}  // namespace mods
