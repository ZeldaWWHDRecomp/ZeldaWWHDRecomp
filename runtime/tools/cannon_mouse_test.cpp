// SPDX-License-Identifier: MPL-2.0
// Exercise the production input filter and procedure hook with a mapped guest Link.
#include "runtime.h"
#include "input.h"
#include "input_map.h"
#include "mods/mods.h"
#include "mods/play_context.h"
#include "platform/touch_options.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

static uint64_t clock_step=100;
static bool enabled=true,captured=true,gyro_mouse=true,aiming=false;
static float sensitivity=.15f;
static bool inverted=false;
void log_msg(const char*, ...) {}
namespace mods {
void filter_pad(input::PadState&);
bool mouse_camera(){return enabled;} bool mouse_captured(){return captured;}
float mouse_sensitivity(){return sensitivity;}
bool direct_camera(){return false;} bool direct_boat_speed(){return false;}
float camera_speed(){return 1;} bool first_person_wheel(){return false;}
uint64_t step(){return clock_step;} double game_time(){return double(clock_step)/30;}
bool trace_on(){return false;} void trace(const char*,...){}
void link_executed(uint32_t){} void lock_on_filter_pad(input::PadState&){} void leaf_filter_pad(input::PadState&){}
float steer_swing_degrees(bool){return 0;}
}
namespace motion {
void set_aiming(bool on){aiming=on;} bool mouse_drives_gyro(){return gyro_mouse&&aiming;}
}
namespace editor {
bool take_mouse(float,float){return false;} bool take_wheel(float){return false;}
void filter_pad(input::PadState&){}
}
namespace touch_options { touch_style::Options get(){return {};} }
namespace input_map {
std::array<ShortcutBinding,kShortcutCount> shortcut_defaults(){return {};}
Mapping current(){return {};}
std::vector<int> key_users(const Mapping&,int,int){return {};}
bool invert_camera_y(){return inverted;}
}
extern "C" {
void f_0240EBB0_orig(Cpu*){} void f_024FA9C8_orig(Cpu*){}
void f_0250D4E8_orig(Cpu*){} void f_0250FDC8_orig(Cpu*){} void f_025071FC_orig(Cpu*){}
void hook_0240EBB0(Cpu*); void hook_025071FC(Cpu*);
}
static constexpr uint32_t kLinkBase=0x20000000;
static void procedure(uint32_t proc,bool ship=true){
    st32(kLinkBase+0x65F0,proc);
    st32(kLinkBase+mods::playctx::kLinkModeFlg,ship?mods::playctx::kModeInShip:0);
}
static bool close(float a,float b){return std::fabs(a-b)<.00001f;}
static input::PadState sample(float dx,float dy,input::PadState pad={}){
    ++clock_step;mods::mouse_add(dx,dy);mods::filter_pad(pad);return pad;
}
int main(){
    void* address=mem::ptr(kLinkBase);
#ifdef _WIN32
    assert(VirtualAlloc(address,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==address);
#else
    assert(mmap(address,0x10000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==address);
#endif
    Cpu c{};c.r[3]=kLinkBase;hook_0240EBB0(&c);procedure(0x8E);
    auto p=sample(5,-5);assert(close(p.lx,.4f)&&close(p.ly,.4f)&&p.rx==0&&p.ry==0);
    assert(!aiming); // mouse gyro must not consume the cannon's input
    input::PadState repeat;mods::filter_pad(repeat);assert(close(repeat.lx,p.lx)&&close(repeat.ly,p.ly));
    p=sample(0,0);assert(p.lx==0&&p.ly==0); // no retained rate after stopping
    p=sample(-5,5);assert(close(p.lx,-.4f)&&close(p.ly,-.4f));
    sensitivity=.3f;p=sample(5,0);assert(close(p.lx,.8f));sensitivity=.15f;
    inverted=true;p=sample(0,-5);assert(close(p.ly,-.4f));inverted=false;
    p=sample(100,-100);assert(p.lx==1&&p.ly==1);
    input::PadState physical;physical.lx=-1;physical.ly=.5f;physical.rx=.7f;
    p=sample(5,-5,physical);assert(p.lx==-1&&p.ly==.5f&&p.rx==.7f);
    p=sample(0,0);assert(p.lx==0&&p.ly==0); // blocked deltas never accumulate
    captured=false;p=sample(5,-5);assert(p.lx==0&&p.ly==0&&p.rx==0);captured=true;
    enabled=false;p=sample(5,-5);assert(p.lx==0&&p.ly==0);enabled=true;
    // Even a recent subject-camera call cannot hand the cannon's mouse to gyro.
    c.r[3]=kLinkBase;hook_025071FC(&c);p=sample(5,0);assert(close(p.lx,.4f)&&!aiming);
    clock_step+=10;
    // Sailing and salvage retain right-stick mouse look, including a stale cannon proc off-boat.
    for(auto proc:{0x88u,0x89u,0x8Fu}){
        procedure(proc);p=sample(5,-5);assert(p.lx==0&&p.ly==0&&close(p.rx,.4f)&&close(p.ry,.4f));
    }
    procedure(0x8E,false);p=sample(5,-5);assert(p.lx==0&&close(p.rx,.4f));
    procedure(0x8E);p=sample(5,0);procedure(0x88);p=sample(0,0);assert(p.lx==0&&p.rx==0);
    // Ordinary aimed items still give the mouse to the configured gyro source.
    procedure(0x94,false);p=sample(5,-5);assert(aiming&&p.lx==0&&p.rx==0);
    puts("Cannon mouse: routing, gyro exclusion, priority, sensitivity, repeated reads and mode transitions PASS");
}
