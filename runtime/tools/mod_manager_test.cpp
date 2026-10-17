// Standalone host-side tests; no game files, player settings or guest code needed.
#include "mods/manager.h"
#include "mods/mods.h"
#include "mods/climb.h"
#include "overlay/hostui.h"
#include <cassert>
#include <cstdlib>
#include <cmath>
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
static std::map<std::string,double> remembered;
namespace mods::packages {
void remember_builtin(const std::string&,bool) {}
void remember_option(const std::string& id,double value) { remembered[id]=value; }
void remember_option(const std::string&,const std::string&) {}
}
int main() {
    using namespace mods::manager;
    env("WWHD_NO_HOST_INPUT",nullptr);
    for(const auto& entry:entries()) env(entry.startup_env,nullptr);
    env("WWHD_MOD_CAMERA_SPEED",nullptr);env("WWHD_MOD_MOUSE_SENS",nullptr);
    env("WWHD_MOD_MOVE_FACTOR",nullptr);env("WWHD_MOD_MOVE_SWIM",nullptr);env("WWHD_MOD_MOVE_STAMINA",nullptr);
    assert(entries().size()==18);
    for(const auto& option:traversal_options()) env(option.startup_env,nullptr);
    env("WWHD_MOD_LOCK_ON_RANGE",nullptr);env("WWHD_MOD_LOCK_ON_CYCLING",nullptr);
    assert(find("target-letterbox") && find("target-letterbox")->default_enabled);
    assert(mods::letterbox_while_targeting());
    preferences["mod.target-letterbox.enabled"] = "0";
    load_saved(); assert(!mods::letterbox_while_targeting());
    assert(set_enabled("target-letterbox", true));
    assert(preferences["mod.target-letterbox.enabled"] == "1");
    env("WWHD_TARGET_LETTERBOX", "0");
    mods::set_letterbox_while_targeting(false);
    load_saved(); assert(!mods::letterbox_while_targeting());
    env("WWHD_TARGET_LETTERBOX", nullptr);
    load_saved(); assert(mods::letterbox_while_targeting());
    assert(find("lock-on") && std::string(find("lock-on")->name)=="Extended enemy lock-on");
    assert(std::string(find("lock-on")->category)=="Gameplay");
    assert(!mods::extended_lock_on() && mods::lock_on_range()==1.f && mods::lock_on_cycling());
    load_saved(); for(bool on:state) assert(!on); // stock defaults stay off
    assert(!mods::ladder_speed() && !mods::sidle_speed() && !mods::sail_speed() && !mods::auto_wind());
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
    preferences["mod.move-speed.anim"] = "dash";
    preferences["mod.move-speed.button"] = "65536";
    preferences["mod.move-speed.bar"] = "0";
    load_saved();
    assert(!mods::move_speed_bar());
    preferences["mod.move-speed.bar"] = "maybe";  // not a saved checkbox value: keeps the setting
    mods::set_move_speed_bar(true); load_saved(); assert(mods::move_speed_bar());
    preferences["mod.move-speed.bar"] = "1";
    preferences["mod.move-speed.button"] = "2147483648";  // "Roll, then hold A"
    load_saved(); assert(mods::move_speed_button() == mods::kMoveButtonRollA);
    preferences["mod.move-speed.button"] = "65536";
    load_saved();
    assert(mods::move_speed_land_factor() == 2.5f && mods::move_speed_swim_factor() == 1.f);
    assert(mods::move_speed_stamina_seconds() == 8.f && mods::move_speed_mode() == mods::MoveMode::kToggle);
    assert(mods::move_speed_cooldown_seconds() == 5.f);
    assert(mods::move_speed_anim() == mods::MoveAnim::kDash);
    assert(mods::move_speed_button() == 65536);
    preferences.erase("mod.move-speed.land");
    preferences.erase("mod.move-speed.swim");
    preferences["mod.move-speed.factor"] = "3";
    load_saved();
    assert(mods::move_speed_land_factor() == 3.f && mods::move_speed_swim_factor() == 3.f);
    // The traversal mods (#95, #96): switches and their factors round-trip; out-of-range values are ignored.
    for(const char* id:{"ladder-speed","sidle-speed","sail-speed","auto-wind"}) assert(set_enabled(id,true));
    assert(mods::ladder_speed() && mods::sidle_speed() && mods::sail_speed() && mods::auto_wind());
    disable_all(); assert(!mods::ladder_speed() && !mods::sidle_speed() && !mods::sail_speed() && !mods::auto_wind());
    assert(mods::ladder_speed_factor()==1.5f && mods::sidle_speed_factor()==1.5f);  // defaults once switched on
    assert(mods::sail_speed_normal_factor()==1.5f && mods::sail_speed_swift_factor()==1.5f);
    preferences["mod.ladder-speed.factor"]="2";
    preferences["mod.sidle-speed.factor"]="9";      // out of range: ignored
    preferences["mod.sail-speed.normal"]="1.25";
    preferences["mod.sail-speed.swift"]="nan";
    load_saved();
    assert(mods::ladder_speed_factor()==2.f && mods::sidle_speed_factor()==1.5f);
    assert(mods::sail_speed_normal_factor()==1.25f && mods::sail_speed_swift_factor()==1.5f);
    set_traversal_option("ladder-speed.factor",2.5f);
    assert(mods::ladder_speed_factor()==2.5f && remembered["ladder-speed.factor"]==2.5);
    assert(std::stof(preferences["mod.ladder-speed.factor"])==2.5f);
    set_traversal_option("sail-speed.swift",2.5f);
    assert(mods::sail_speed_swift_factor()==2.5f && remembered["sail-speed.swift"]==2.5);
    assert(std::stof(preferences["mod.sail-speed.swift"])==2.5f);
    set_traversal_option("sidle-speed.factor",7.f);  // clamped by the setter, saved as applied
    assert(mods::sidle_speed_factor()==3.f && remembered["sidle-speed.factor"]==3.0);
    env("WWHD_MOD_LADDER_FACTOR","1");preferences["mod.ladder-speed.factor"]="3";
    load_saved(); assert(mods::ladder_speed_factor()==2.5f);  // the environment owns startup values
    env("WWHD_MOD_LADDER_FACTOR",nullptr);
    // Jump anywhere: off by default; its options round-trip; invalid buttons are ignored.
    assert(!mods::jump_anywhere() && !mods::double_jump() && !mods::no_auto_jump() && mods::jump_button()==0x20);
    assert(set_enabled("jump-anywhere",true) && mods::jump_anywhere() && preferences["mod.jump-anywhere.enabled"]=="1");
    preferences["mod.jump-anywhere.double"]="1";preferences["mod.jump-anywhere.no-auto"]="1";
    preferences["mod.jump-anywhere.button"]="64";
    load_saved();assert(mods::double_jump() && mods::no_auto_jump() && mods::jump_button()==0x40);
    preferences["mod.jump-anywhere.button"]="1";  // A: not allowed
    preferences["mod.jump-anywhere.double"]="x";
    load_saved();assert(mods::jump_button()==0x40 && mods::double_jump());
    env("WWHD_MOD_JUMP_NO_AUTO","0");preferences["mod.jump-anywhere.no-auto"]="0";
    load_saved();assert(mods::no_auto_jump());  // the environment owns startup values
    env("WWHD_MOD_JUMP_NO_AUTO",nullptr);
    mods::set_double_jump(false);mods::set_no_auto_jump(false);mods::set_jump_button(0x20);
    disable_all();assert(!mods::jump_anywhere());
    preferences.erase("mod.jump-anywhere.double");preferences.erase("mod.jump-anywhere.no-auto");preferences.erase("mod.jump-anywhere.button");
    preferences["mod.lock-on.enabled"]="1";
    preferences["mod.lock-on.range"]="2.5";
    preferences["mod.lock-on.cycling"]="0";
    load_saved();
    assert(mods::extended_lock_on() && mods::lock_on_range()==2.5f && !mods::lock_on_cycling());
    for (const char* invalid : {"nan", "inf", "-inf", "0.9", "3.1", "2junk", ""}) {
        preferences["mod.lock-on.range"]=invalid;load_saved();assert(mods::lock_on_range()==2.5f);
    }
    preferences["mod.lock-on.range"]="3";preferences["mod.lock-on.cycling"]="invalid";
    load_saved();assert(mods::lock_on_range()==3.f && !mods::lock_on_cycling());
    preferences["mod.lock-on.range"]="1";preferences["mod.lock-on.cycling"]="1";
    mods::set_extended_lock_on(false);
    env("WWHD_MOD_LOCK_ON","0");env("WWHD_MOD_LOCK_ON_RANGE","2");env("WWHD_MOD_LOCK_ON_CYCLING","0");
    load_saved();assert(!mods::extended_lock_on() && mods::lock_on_range()==3.f && !mods::lock_on_cycling());
    env("WWHD_MOD_LOCK_ON",nullptr);env("WWHD_MOD_LOCK_ON_RANGE",nullptr);env("WWHD_MOD_LOCK_ON_CYCLING",nullptr);
    load_saved();assert(mods::extended_lock_on() && mods::lock_on_range()==1.f && mods::lock_on_cycling());
    assert(set_enabled("lock-on",false));assert(preferences["mod.lock-on.enabled"]=="0");
    assert(set_enabled("lock-on",true));disable_all();assert(!mods::extended_lock_on());
    assert(mods::lock_on_range()==1.f && mods::lock_on_cycling());
    // Test isolation protects player settings even when toggles are exercised.
    env("WWHD_NO_HOST_INPUT","1");prior=reads;load_saved();assert(reads==prior);
    prior=writes;assert(set_enabled("quick-doors",true));assert(state[4]);assert(writes==prior);
}
