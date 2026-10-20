#pragma once
// Standalone host-side tests; no game files, player settings or guest code needed.
#include "mods/manager.h"
#include "mods/mods.h"
#include "mods/climb.h"
#include "overlay/hostui.h"
#include "platform/process.h"
#include "mods/catalogue_client.h"
#include "mods/catalogue_setup.h"
#include <cassert>
#include <cstdlib>
#include <cmath>
#include <map>
#include <string>

namespace {
bool state[7]{};
float speed = 1, sensitivity = .15f;
std::map<std::string,std::string> preferences;
int reads = 0, writes = 0;
void env(const char* key, const char* value) {
#ifdef _WIN32
    _putenv_s(key, value ? value : "");
#else
    if (value) setenv(key,value,1); else unsetenv(key);
#endif
}
}
namespace mods {
static bool target_letterbox = true;
bool letterbox_while_targeting() { return target_letterbox; }
void set_letterbox_while_targeting(bool on) { target_letterbox = on; }
static bool ff_on = false, ff_mute = true;
static unsigned ff_rate = 2;
static uint32_t ff_button = 0x40;
bool fast_forward() { return ff_on; } void set_fast_forward(bool on) { ff_on = on; }
unsigned fast_forward_rate() { return ff_rate; }
void set_fast_forward_rate(unsigned r) { if (r >= 2 && r <= 4) ff_rate = r; }
uint32_t fast_forward_button() { return ff_button; }
void set_fast_forward_button(uint32_t b) { if (valid_fast_forward_button(b)) ff_button = b; }
bool fast_forward_mute() { return ff_mute; } void set_fast_forward_mute(bool on) { ff_mute = on; }

static bool lock_on_enabled = false, lock_on_cycle = true;
static float lock_on_multiplier = 1.f;
bool extended_lock_on() { return lock_on_enabled; }
void set_extended_lock_on(bool on) { lock_on_enabled = on; }
float lock_on_range() { return lock_on_multiplier; }
void set_lock_on_range(float f) { if (std::isfinite(f) && f >= 1 && f <= 3) lock_on_multiplier = f; }
bool lock_on_cycling() { return lock_on_cycle; }
void set_lock_on_cycling(bool on) { lock_on_cycle = on; }

static bool move_on = false;
static float move_land = 1.5f, move_swim = 1.25f, move_seconds = 5.f, move_cooldown = 3.f;
static int move_mode = (int)MoveMode::kHold;
static int move_anim = (int)MoveAnim::kNative;
static uint32_t move_button = 0x40000;
bool move_speed() { return move_on; } void set_move_speed(bool on) { move_on = on; }
float move_speed_land_factor() { return move_land; } void set_move_speed_land_factor(float f) { if (f >= 1 && f <= 4) move_land = f; }
float move_speed_swim_factor() { return move_swim; } void set_move_speed_swim_factor(float f) { if (f >= 1 && f <= 4) move_swim = f; }
float move_speed_stamina_seconds() { return move_seconds; } void set_move_speed_stamina_seconds(float s) { if (s >= 0 && s <= 60) move_seconds = s; }
float move_speed_cooldown_seconds() { return move_cooldown; } void set_move_speed_cooldown_seconds(float s) { if (s >= 0 && s <= 60) move_cooldown = s; }
MoveMode move_speed_mode() { return (MoveMode)move_mode; } void set_move_speed_mode(MoveMode m) { move_mode = (int)m; }
MoveAnim move_speed_anim() { return (MoveAnim)move_anim; } void set_move_speed_anim(MoveAnim a) { move_anim = (int)a; }
uint32_t move_speed_button() { return move_button; } void set_move_speed_button(uint32_t b) { if (b && !(b & (b - 1))) move_button = b; }
static bool move_bar = true;
bool move_speed_bar() { return move_bar; } void set_move_speed_bar(bool on) { move_bar = on; }

static bool camera_items = false;
bool camera_directed_items() { return camera_items; } void set_camera_directed_items(bool b) { camera_items=b; }
bool direct_camera() { return state[0]; } void set_direct_camera(bool b) { state[0]=b; }
bool mouse_camera() { return state[1]; } void set_mouse_camera(bool b) { state[1]=b; }
bool first_person_wheel() { return state[2]; } void set_first_person_wheel(bool b) { state[2]=b; }
bool climb_enabled() { return state[3]; } void set_climb_enabled(bool b) { state[3]=b; }
bool quick_doors() { return state[4]; } void set_quick_doors(bool b) { state[4]=b; }
bool fast_scenes() { return state[5]; } void set_fast_scenes(bool b) { state[5]=b; }
bool quick_use() { return state[6]; } void set_quick_use(bool b) { state[6]=b; }
static float quick_speed = 1.f;
float quick_use_speed() { return quick_speed; }
void set_quick_use_speed(float f) { if (f == 1.f || f == 1.25f || f == 1.5f) quick_speed = f; }
float camera_speed() { return speed; }
void set_camera_speed(float f) { speed=f; }
float mouse_sensitivity() { return sensitivity; }
void set_mouse_sensitivity(float f) { sensitivity=f; }
}
namespace hostui {
bool get(const char* k, std::string& value) {
    ++reads; auto it=preferences.find(k);
    if(it==preferences.end()) return false;
    value=it->second;return true;
}
void set(const char* k, const std::string& value) { ++writes;preferences[k]=value; }
}
