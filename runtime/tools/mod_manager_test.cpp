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
static bool move_on = false;
bool move_speed() { return move_on; } void set_move_speed(bool on) { move_on = on; }
float move_speed_factor() { return 1.5f; } void set_move_speed_factor(float) {}
uint32_t move_speed_button() { return 0x40000; } void set_move_speed_button(uint32_t) {}

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
    assert(entries().size()==7);
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
    // Test isolation protects player settings even when toggles are exercised.
    env("WWHD_NO_HOST_INPUT","1");prior=reads;load_saved();assert(reads==prior);
    prior=writes;assert(set_enabled("quick-doors",true));assert(state[4]);assert(writes==prior);
}
