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
static bool enabled=true,captured=true,gyro_mouse=true,aiming=false,look=false;
static float sensitivity=.15f;
static bool inverted=false,look_pitch=false;
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
bool camera_stick_pitch(){return look_pitch;}
}
namespace motion {
void set_aiming(bool on){aiming=on;} bool mouse_drives_gyro(){return gyro_mouse&&aiming&&!look;}
void set_mouse_look(bool on){look=on;}  // as motion.cpp: the mouse camera aims itself, the mouse source waits
void mouse_motion(float,float){}  // the WWHD_TEST_MOUSE test aid (camera.cpp); not under test
}
namespace editor {
bool take_mouse(float,float){return false;} bool take_wheel(float){return false;}
void filter_pad(input::PadState&){}
}
namespace touch_options { touch_style::Options get(){return {};} }
namespace test_aids { uint64_t origin_step(){return 0;} }
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
void site_02506CEC(Cpu*); void site_02508380(Cpu*); void site_025083C4(Cpu*);
void site_0251053C(Cpu*); void site_02510DEC(Cpu*);
}
static constexpr uint32_t kLinkBase=0x20000000, kCamBase=kLinkBase+0x8000;
static void map(uint32_t at,size_t size){
    void* address=mem::ptr(at);
#ifdef _WIN32
    assert(VirtualAlloc(address,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==address);
#else
    assert(mmap(address,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==address);
#endif
}
static void procedure(uint32_t proc,bool ship=true){
    st32(kLinkBase+0x65F0,proc);
    st32(kLinkBase+mods::playctx::kLinkModeFlg,ship?mods::playctx::kModeInShip:0);
}
static bool close(float a,float b){return std::fabs(a-b)<.00001f;}
static input::PadState sample(float dx,float dy,input::PadState pad={}){
    ++clock_step;mods::mouse_add(dx,dy);mods::filter_pad(pad);return pad;
}
int main(){
    map(kLinkBase,0x10000);
    map(0x10040000,0x10000);  // the camera styles (subject style 0 here: no flags)
    map(0x10470000,0x10000);  // gameInfo's player status (not in first person: no zoom slow-down applies anyway)
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
    // Aimed items with the mouse camera: no synthetic stick, the subject angle gets the mouse's degrees (issue #558).
    procedure(0x94,false);p=sample(5,-5);assert(aiming&&look&&p.lx==0&&p.rx==0&&p.ry==0);
    // CalcSubjectAngle's gyro branch with R3 look's style (speed 0.04, yaw range 90, pitch range 70), some gyro input
    auto subject=[](double x,double y){Cpu s{};s.r[27]=kCamBase;s.f[31].ps0=.04;s.f[28].ps0=90;s.f[27].ps0=70;
                                       s.f[22].ps0=x;s.f[0].ps0=y;site_02506CEC(&s);return s;};
    // the game's turn (its arithmetic: range x speed x input, truncated to s16 units), within a unit of the mouse's
    auto turned=[](const Cpu& s,double x0,double y0,double right,double down){
        const float m384=std::fmax(-1.f,std::fmin(1.f,-(float)(s.f[22].ps0-x0)*.04f)),m388=(float)(s.f[0].ps0-y0)*.04f;
        const double r=-(int16_t)(int32_t)((float)(90*m384)*182.04445f)*(360.0/65536);
        const double d=(int16_t)(int32_t)((float)(70*m388)*182.04445f)*(360.0/65536);
        return std::fabs(r-right)<.0111&&std::fabs(d-down)<.0111&&s.f[22].ps1==s.f[22].ps0&&s.f[0].ps1==s.f[0].ps0;};
    p=sample(10,-4);Cpu s=subject(.25,-.5);assert(turned(s,.25,-.5,10*.15,-4*.15));  // added to the gyro's own turn
    s=subject(0,0);assert(s.f[22].ps0==0&&s.f[0].ps0==0);  // its second call in the step (camera after Link): nothing
    // the drawn view follows at once while the mouse looks around, eased again 8 steps after it stopped
    Cpu e{};e.r[1]=kLinkBase+0x4000;e.f[1].ps0=.45;site_02508380(&e);assert(e.f[1].ps0==1);e.f[1].ps0=.45;
    // U: Link turned himself (no own subject angle this step): the view goes to his facing, not past it by angU
    st16(e.r[1]+0x12,(uint16_t)(int16_t)-8192);st16(e.r[1]+0x08,(uint16_t)(int16_t)-4096);
    site_025083C4(&e);assert(e.f[1].ps0==1&&(int16_t)ld16(e.r[1]+0x12)==-4096);
    // sensitivity, inversion (the port's setting), a big flick: linear, no stick saturation
    sensitivity=.3f;p=sample(200,0);s=subject(0,0);assert(turned(s,0,0,60,0));
    // faster than the game's 90 degrees per step: the rest follows in the next step, nothing is lost
    p=sample(400,0);s=subject(0,0);assert(turned(s,0,0,90,0));p=sample(0,0);s=subject(0,0);assert(turned(s,0,0,30,0));
    sensitivity=.15f;
    inverted=true;p=sample(0,10);s=subject(0,0);assert(turned(s,0,0,0,-1.5));inverted=false;
    // movement the game did not take (a first-person transition) is dropped, never added to a later step
    p=sample(20,0);++clock_step;++clock_step;s=subject(0,0);assert(s.f[22].ps0==0);
    p=sample(0,0);p=sample(2,0);s=subject(0,0);assert(turned(s,0,0,.3,0));
    clock_step+=9;e.f[1].ps0=.45;site_02508380(&e);assert(e.f[1].ps0==.45);
    // Mouse camera off: the configured Mouse aiming gyro source gets the mouse again, nothing reaches the subject angle.
    enabled=false;p=sample(5,-5);assert(aiming&&!look&&p.rx==0);s=subject(0,0);assert(s.f[22].ps0==0&&s.f[0].ps0==0);
    enabled=true;
    // Not aiming (on foot): the follow camera keeps its synthetic stick.
    procedure(0x0,false);clock_step+=10;p=sample(5,0);assert(close(p.rx,.4f));s=subject(0,0);assert(s.f[22].ps0==0);
    // Right stick up/down: Look up and down (issue #661). manualCamera's vertical rate f28 leaves the centre height,
    // radius and field of view (it becomes 0) and goes into the latitude delta f1 alone, -(rate x latitude step).
    {
        clock_step+=20;  // no mouse or swipe this step: the stick's own rate
        Cpu m{};m.r[1]=kLinkBase+0x5000;stf32(m.r[1]+0x14,3.5f);
        auto rate=[&](double r){m.f[28].ps0=r;site_0251053C(&m);return m.f[28].ps0;};
        auto lat=[&](double d,double lo,double hi){m.f[1].ps0=d;m.f[2].ps0=lo;m.f[3].ps0=hi;site_02510DEC(&m);return m.f[1].ps0;};
        // Zoom (original): both untouched
        assert(rate(.7)==.7&&close((float)lat(-.7*3.5,-10,60),-2.45f));
        look_pitch=true;
        assert(rate(.7)==0&&close((float)lat(-0.0,-10,60),-2.45f));      // stick up: V decreases, the camera looks up
        assert(rate(-1)==0&&close((float)lat(-0.0,-10,60),3.5f));        // stick down
        assert(rate(.5)==0&&close((float)lat(-0.0,60,-10),1.75f));       // descending limits: the same look direction
        assert(lat(-0.0,-10,60)==0&&std::signbit(m.f[1].ps0));           // used once: a later call keeps the game's delta
        assert(rate(0)==0&&!std::signbit(m.f[28].ps0)&&std::signbit(lat(-0.0,-10,60)));  // at rest: nothing changes
        look_pitch=false;
        assert(rate(.7)==.7&&lat(-0.0,-10,60)==0&&std::signbit(m.f[1].ps0));
    }
    puts("Cannon mouse: routing, gyro exclusion, priority, sensitivity, repeated reads and mode transitions PASS");
    puts("Look up and down: the vertical rate goes to the latitude alone, once per manualCamera run, off by default PASS");
    puts("First-person mouse look: degrees per view, once per step, ease, inversion, stale movement, mouse camera off PASS");
}
