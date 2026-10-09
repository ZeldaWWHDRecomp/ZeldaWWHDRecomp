// Standalone host-side tests; no game files, player settings or guest code needed.
#include "mods/manager.h"
#include "mods/mods.h"
#include "mods/climb.h"
#include "overlay/hostui.h"
#include <cassert>
#include <cstdlib>
#include <map>
#include <string>

namespace {
bool state[6]{};
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
static bool ff_on = false, ff_mute = true;
static unsigned ff_rate = 2;
static uint32_t ff_button = 0x40;
bool fast_forward() { return ff_on; } void set_fast_forward(bool on) { ff_on = on; }
unsigned fast_forward_rate() { return ff_rate; }
void set_fast_forward_rate(unsigned r) { if (r >= 2 && r <= 4) ff_rate = r; }
uint32_t fast_forward_button() { return ff_button; }
void set_fast_forward_button(uint32_t b) { if (valid_fast_forward_button(b)) ff_button = b; }
bool fast_forward_mute() { return ff_mute; } void set_fast_forward_mute(bool on) { ff_mute = on; }

static bool move_on = false;
static float move_land = 1.75f, move_swim = 1.5f, move_seconds = 6.f, move_cooldown = 3.f;
static int move_mode = (int)MoveMode::kHold;
static uint32_t move_button = 0x40000;
bool move_speed() { return move_on; } void set_move_speed(bool on) { move_on = on; }
float move_speed_land_factor() { return move_land; } void set_move_speed_land_factor(float f) { if (f >= 1 && f <= 4) move_land = f; }
float move_speed_swim_factor() { return move_swim; } void set_move_speed_swim_factor(float f) { if (f >= 1 && f <= 4) move_swim = f; }
float move_speed_stamina_seconds() { return move_seconds; } void set_move_speed_stamina_seconds(float s) { if (s >= 0 && s <= 60) move_seconds = s; }
float move_speed_cooldown_seconds() { return move_cooldown; } void set_move_speed_cooldown_seconds(float s) { if (s >= 0 && s <= 60) move_cooldown = s; }
MoveMode move_speed_mode() { return (MoveMode)move_mode; } void set_move_speed_mode(MoveMode m) { move_mode = (int)m; }
uint32_t move_speed_button() { return move_button; } void set_move_speed_button(uint32_t b) { if (b && !(b & (b - 1))) move_button = b; }

bool direct_camera() { return state[0]; } void set_direct_camera(bool b) { state[0]=b; }
bool mouse_camera() { return state[1]; } void set_mouse_camera(bool b) { state[1]=b; }
bool first_person_wheel() { return state[2]; } void set_first_person_wheel(bool b) { state[2]=b; }
bool climb_enabled() { return state[3]; } void set_climb_enabled(bool b) { state[3]=b; }
bool quick_doors() { return state[4]; } void set_quick_doors(bool b) { state[4]=b; }
bool fast_scenes() { return state[5]; } void set_fast_scenes(bool b) { state[5]=b; }
void set_camera_speed(float f) { speed=f; }
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
namespace mods::packages { void remember_builtin(const std::string&,bool) {} }
int main() {
    using namespace mods::manager;
    env("WWHD_NO_HOST_INPUT",nullptr);
    for(const auto& entry:entries()) env(entry.startup_env,nullptr);
    env("WWHD_MOD_CAMERA_SPEED",nullptr);env("WWHD_MOD_MOUSE_SENS",nullptr);
    env("WWHD_MOD_MOVE_FACTOR",nullptr);env("WWHD_MOD_MOVE_SWIM",nullptr);env("WWHD_MOD_MOVE_STAMINA",nullptr);
    assert(entries().size()==8);
    load_saved(); for(bool on:state) assert(!on); // stock defaults stay off
    preferences["mod.wall-climb.enabled"]="1";
    preferences["mod.quick-doors.enabled"]="invalid";
    preferences["mod.direct-camera.speed"]="1.5";
    preferences["mod.mouse-camera.sensitivity"]="nan";
    load_saved();assert(state[3]);assert(!state[4]);assert(speed==1.5f);assert(sensitivity==.15f);
    // Explicit zero and nonzero overrides both prevent loading saved state.
    state[3]=false;env("WWHD_CLIMB","0");load_saved();assert(!state[3]);
    preferences["mod.direct-camera.enabled"]="0";
    state[0]=true;env("WWHD_MOD_DIRECT_CAMERA","1");load_saved();assert(state[0]);
    assert(set_enabled("quick-doors",true));assert(state[4]);
    assert(preferences["mod.quick-doors.enabled"]=="1");
    int prior=writes;assert(!set_enabled("unknown",true));assert(writes==prior);
    assert(set_enabled("move-speed",true));assert(mods::move_speed());
    disable_all();for(bool on:state) assert(!on);assert(!mods::move_speed());
    for(const auto& entry:entries()) assert(preferences[std::string("mod.")+entry.id+".enabled"]=="0");
    preferences["mod.fast-forward.rate"] = "3";
    preferences["mod.fast-forward.button"] = "32";
    preferences["mod.fast-forward.mute"] = "0";
    load_saved();
    assert(mods::fast_forward_rate() == 3 && mods::fast_forward_button() == 32 && !mods::fast_forward_mute());
    env("WWHD_MOD_FF_RATE", "4");
    preferences["mod.fast-forward.rate"] = "2";
    load_saved(); assert(mods::fast_forward_rate() == 3); // environment owns startup rate
    env("WWHD_MOD_FF_RATE", nullptr);
    preferences["mod.fast-forward.button"] = "32768"; // A cannot answer prompts as boost
    preferences["mod.fast-forward.rate"] = "nan";
    load_saved(); assert(mods::fast_forward_rate() == 3 && mods::fast_forward_button() == 32);
    assert(set_enabled("fast-forward", true) && mods::fast_forward());
    disable_all(); assert(!mods::fast_forward());
    // The run/swim options round-trip, and a pre-split setting's single factor feeds both states.
    preferences["mod.move-speed.land"] = "2.5";
    preferences["mod.move-speed.swim"] = "1";
    preferences["mod.move-speed.stamina"] = "8";
    preferences["mod.move-speed.cooldown"] = "5";
    preferences["mod.move-speed.mode"] = "1";
    preferences["mod.move-speed.button"] = "65536";
    load_saved();
    assert(mods::move_speed_land_factor() == 2.5f && mods::move_speed_swim_factor() == 1.f);
    assert(mods::move_speed_stamina_seconds() == 8.f && mods::move_speed_mode() == mods::MoveMode::kToggle);
    assert(mods::move_speed_cooldown_seconds() == 5.f);
    assert(mods::move_speed_button() == 65536);
    preferences.erase("mod.move-speed.land");
    preferences.erase("mod.move-speed.swim");
    preferences["mod.move-speed.factor"] = "3";
    load_saved();
    assert(mods::move_speed_land_factor() == 3.f && mods::move_speed_swim_factor() == 3.f);
    // Test isolation protects player settings even when toggles are exercised.
    env("WWHD_NO_HOST_INPUT","1");prior=reads;load_saved();assert(reads==prior);
    prior=writes;assert(set_enabled("quick-doors",true));assert(state[4]);assert(writes==prior);
}
