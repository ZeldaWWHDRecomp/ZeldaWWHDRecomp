// SPDX-License-Identifier: MPL-2.0
// Exercise the production input filter and procedure hook with a mapped guest Link.
#include "runtime.h"
#include "input.h"
#include "input_map.h"
#include "mods/mods.h"
#include "mods/play_context.h"
#include "mods/camera_mouse.h"
#include "mods/camera_zoom_hold.h"
#include "mods/mouse_wheel.h"
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
static float sensitivity=.15f,fp_scale=1.f;
static bool inverted=false,look_pitch=false,aim_left=false,zoom_held=false;
static int zoom_used=0;
static mods::playctx::PlayContext ctx;  // the last play context (issue #666)
void log_msg(const char*, ...) {}
namespace mods {
void filter_pad(input::PadState&);
bool mouse_camera(){return enabled;} bool mouse_captured(){return captured;}
float mouse_sensitivity(){return sensitivity;}
float mouse_first_person_scale(){return fp_scale;}
bool direct_camera(){return false;} bool direct_boat_speed(){return false;}
float camera_speed(){return 1;} int wheel_mode=2; int mouse_wheel_mode(){return wheel_mode;} int wheel_smooth=2; int wheel_zoom_smoothing(){return wheel_smooth;}
uint64_t step(){return clock_step;} double game_time(){return double(clock_step)/30;}
bool trace_on(){return false;} void trace(const char*,...){}
void link_executed(uint32_t){} void lock_on_filter_pad(input::PadState&){} void leaf_filter_pad(input::PadState&){}
float steer_swing_degrees(bool){return 0;}
bool camera_stick_pitch(){return look_pitch;}
bool camera_zoom_held(){return zoom_held;} void camera_zoom_used(){zoom_used++;}
bool aim_left_stick(){return aim_left;}
void play_context_want(bool){} playctx::PlayContext play_context(){return ctx;}
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
void f_0240EBB0_orig(Cpu*){}
float next_mode_value=-1,next_mode_y=-1;  // the stick nextMode saw (the wheel's wake)
uint32_t next_mode_result=12;  // the mode nextMode returns
void f_024FA9C8_orig(Cpu* c){next_mode_value=(float)ldf32(c->r[3]+0x178);next_mode_y=(float)ldf32(c->r[3]+0x174);c->r[3]=next_mode_result;}
void f_0250D4E8_orig(Cpu*){} void f_0250FDC8_orig(Cpu*){} void f_025071FC_orig(Cpu*){}
void hook_0240EBB0(Cpu*); void hook_025071FC(Cpu*); void hook_024FA9C8(Cpu*);
void site_02506CEC(Cpu*); void site_02508380(Cpu*); void site_025083C4(Cpu*);
void site_0251053C(Cpu*); void site_02510DEC(Cpu*); void site_02510448(Cpu*);
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
    // more reads in one step (the Pro Controller's KPAD read after the GamePad's, code mods, the scenes' skip check):
    // movement that arrives after the step's first read is kept for the next step, never thrown away
    {
        p=sample(4,0);mods::mouse_add(6,-2);input::PadState again;mods::filter_pad(again);
        mods::mouse_add(2,0);mods::filter_pad(again);
        s=subject(0,0);assert(turned(s,0,0,4*.15,0));                  // this step: what the first read saw
        p=sample(0,0);s=subject(0,0);assert(turned(s,0,0,8*.15,-2*.15));  // the next: the later movement, all of it
        p=sample(0,0);s=subject(0,0);assert(s.f[22].ps0==0&&s.f[0].ps0==0);
        // the sum over many split steps: exactly points x sensitivity degrees, as the follow camera
        double right=0;
        for(int i=0;i<30;i++){++clock_step;input::PadState q;mods::mouse_add(3,0);mods::filter_pad(q);mods::mouse_add(7,0);
            mods::filter_pad(q);mods::filter_pad(q);Cpu t=subject(0,0);
            right+=-(int16_t)(int32_t)((float)(90*std::fmax(-1.f,std::fmin(1.f,-(float)t.f[22].ps0*.04f)))*182.04445f)*(360.0/65536);}
        p=sample(0,0);s=subject(0,0);
        right+=-(int16_t)(int32_t)((float)(90*std::fmax(-1.f,std::fmin(1.f,-(float)s.f[22].ps0*.04f)))*182.04445f)*(360.0/65536);
        assert(std::fabs(right-300*.15)<.02);
    }
    // True 60 (decomp build, camera_mouse.h frame_part / step_speeds): the frame's movement spread over its N steps,
    // with the game's per-step arithmetic (Link's call turns him by dt of the yaw share, m388 moves by dt of the rate)
    {
        using namespace mods::camera_mouse;
        assert(frame_part(9,1,3)==3&&frame_part(6,2,3)==3&&frame_part(3,3,3)==3&&frame_part(5,1,1)==5&&frame_part(4,7,2)==4);
        auto sp=step_speeds(.04,.5,false);assert(close((float)sp.yaw,.02f)&&close((float)sp.pitch,.02f));
        sp=step_speeds(.04,.5,true);assert(close((float)sp.yaw,.04f)&&close((float)sp.pitch,.02f));
        sp=step_speeds(.04,1,false);assert(sp.yaw==.04&&sp.pitch==.04);
        for(unsigned n:{2u,4u})for(bool cam:{false,true}){
            const double dt=1.0/n,k=.04,ym=90,pm=70;double yaw_deg=0,m388=0,ry=0,rp=0;
            for(int frame=0;frame<10;frame++){
                double left_x=1.7,left_y=-.9;  // degrees this 30 Hz frame (right, down)
                for(unsigned kk=1;kk<=n;kk++){
                    const double px=frame_part(left_x,kk,n),py=frame_part(left_y,kk,n);left_x-=px,left_y-=py;
                    const auto v=step_speeds(k,dt,cam);
                    const auto st=subject_step(px,py,ry,rp,v.yaw,ym,pm,m388,false,v.pitch);ry=st.rest_yaw,rp=st.rest_pitch;
                    const float share=std::fmax(-1.f,std::fmin(1.f,-(float)st.x*(float)k));
                    const double turn=cam?share:share*dt;  // the camera's call: the whole share; Link's: dt of it
                    yaw_deg+=-(int16_t)(int32_t)((float)(ym*turn)*182.04445f)*(360.0/65536);
                    m388=(float)(st.y*k*dt+m388);
                }
            }
            const double pitch_deg=(int16_t)(int32_t)((float)(pm*m388)*182.04445f)*(360.0/65536);
            assert(std::fabs(yaw_deg-17)<.02&&std::fabs(pitch_deg+9)<.02);
        }
    }
    // First-person sensitivity: a multiplier in first person and the item aims only
    fp_scale=2.5f;p=sample(10,4);s=subject(0,0);assert(turned(s,0,0,10*.15*2.5,4*.15*2.5));
    fp_scale=.25f;p=sample(40,0);s=subject(0,0);assert(turned(s,0,0,40*.15*.25,0));
    sensitivity=1.f;fp_scale=1.f;p=sample(30,0);s=subject(0,0);assert(turned(s,0,0,30,0));  // the widened range: 1 degree per point
    sensitivity=.15f;
    // Mouse camera off: the configured Mouse aiming gyro source gets the mouse again, nothing reaches the subject angle.
    enabled=false;p=sample(5,-5);assert(aiming&&!look&&p.rx==0);s=subject(0,0);assert(s.f[22].ps0==0&&s.f[0].ps0==0);
    enabled=true;
    // Not aiming (on foot): the follow camera keeps its synthetic stick.
    procedure(0x0,false);clock_step+=10;p=sample(5,0);assert(close(p.rx,.4f));s=subject(0,0);assert(s.f[22].ps0==0);
    // ... and the First-person sensitivity leaves it alone (the follow camera's degrees: points x sensitivity)
    {
        fp_scale=3.f;clock_step+=10;p=sample(10,0);
        Cpu m{};m.r[31]=kCamBase;m.f[25].ps0=8;site_02510448(&m);
        assert(close((float)ldf32(kCamBase+0x154),mods::camera_mouse::yaw_speed(10,.15f,8)));
        fp_scale=1.f;
    }
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
        // Zoom while holding (issue #688): held, the rate stays in f28 (centre height, radius, latitude and field of
        // view: the original zoom) and the latitude delta is the game's own; let go, Look up and down again
        zoom_held=true;
        assert(rate(.7)==.7&&close((float)lat(-.7*3.5,-10,60),-2.45f)&&zoom_used==1);
        assert(rate(-1)==-1&&close((float)lat(3.5,-10,60),3.5f)&&zoom_used==2);
        assert(rate(0)==0&&zoom_used==2);  // at rest it zooms nothing
        zoom_held=false;
        assert(rate(.7)==0&&close((float)lat(-0.0,-10,60),-2.45f)&&zoom_used==2);
        look_pitch=false;
        zoom_held=true;  // Zoom (original): the hold changes nothing
        assert(rate(.7)==.7&&lat(-0.0,-10,60)==0&&std::signbit(m.f[1].ps0)&&zoom_used==2);
        zoom_held=false;
        assert(rate(.7)==.7&&lat(-0.0,-10,60)==0&&std::signbit(m.f[1].ps0));
    }
    // Zoom while holding: the button rules on the game's pad reads (camera_zoom_hold.h)
    {
        using camera_zoom_hold::State; using camera_zoom_hold::filter; using camera_zoom_hold::button;
        using camera_zoom_hold::kTapSteps; using camera_zoom_hold::kOff;
        using input::kL; using input::kZL; using input::kA;
        constexpr int CL=camera_zoom_hold::kL, CZL=camera_zoom_hold::kZL;  // the choices (the button bits: kL, kZL)
        State s;
        auto run=[&](int ch,bool look,bool play,uint32_t raw,uint32_t out,float ry,bool zoomed,uint64_t now){return filter(s,ch,look,play,raw,out,ry,zoomed,now);};
        assert(camera_zoom_hold::kDefault==CL&&button(kOff)==0&&button(CL)==kL&&button(CZL)==kZL);
        // Look up and down off: nothing held, L reaches the game at once
        assert(run(CL,false,true,kL,kL,0,false,1)==kL&&!s.held);
        // L: a tap (no zoom) is held back while held, then given to the game for kTapSteps steps after the release
        assert(run(CL,true,true,kL,kL,0,false,10)==0&&s.held);
        assert(run(CL,true,true,kL,kL,0,false,11)==0&&s.held);  // still held back (also a second read of the step)
        assert(run(CL,true,true,0,0,0,false,12)==kL&&!s.held);  // let go: the page turns
        assert(run(CL,true,true,0,0,0,false,12)==kL);           // the step's other read (VPAD and KPAD)
        assert(run(CL,true,true,0,0,0,false,13)==kL);
        assert(run(CL,true,true,0,0,0,false,12+kTapSteps)==0);  // over
        // L + the stick up or down: it zoomed, so no L at all, before or after
        assert(run(CL,true,true,kL|kA,kL|kA,0,false,20)==kA&&s.held);
        assert(run(CL,true,true,kL|kA,kL|kA,.9f,false,21)==kA);
        assert(run(CL,true,true,kL,kL,0,false,22)==0);
        assert(run(CL,true,true,0,0,0,false,23)==0&&run(CL,true,true,0,0,0,false,24)==0);
        // ... the camera's word counts too (the mouse zoomed; the stick barely moved)
        assert(run(CL,true,true,kL,kL,.1f,false,30)==0&&run(CL,true,true,kL,kL,.1f,true,31)==0&&run(CL,true,true,0,0,0,false,32)==0);
        // a small stick drift is no zoom
        assert(run(CL,true,true,kL,kL,.2f,false,40)==0&&run(CL,true,true,0,0,0,false,41)==kL);
        // an L that a jump already took (Jump anywhere on L1: out has no L): held for the camera, no tap
        assert(run(CL,true,true,kL,0,0,false,50)==0&&s.held&&run(CL,true,true,0,0,0,false,51)==0);
        // out of play (menus): L as it is, though still held for the camera
        assert(run(CL,true,false,kL,kL,0,false,60)==kL&&s.held&&run(CL,true,false,0,0,0,false,61)==0);
        // pressed in play, let go after play ended (a talk began): no page turn then
        assert(run(CL,true,true,kL,kL,0,false,70)==0&&run(CL,true,false,kL,kL,0,false,71)==0&&run(CL,true,false,0,0,0,false,72)==0);
        // ZL: held while ZL is, and ZL always reaches the game (targeting unchanged); L untouched
        assert(run(CZL,true,true,kZL,kZL,.9f,false,80)==kZL&&s.held);
        assert(run(CZL,true,true,kL,kL,0,false,81)==kL&&!s.held);
        // Off: never held
        assert(run(kOff,true,true,kL|kZL,kL|kZL,1,false,90)==(kL|kZL)&&!s.held);
    }
    // Mouse wheel: Zoom (issue #696, mouse_wheel.h): a notch is kStepsPerNotch steps of manualCamera's full vertical
    // rate f28 (+1: closer), given at once (Wheel zoom smoothing Off) or eased out, in either Right stick up/down
    // choice; first person and the other modes never take it
    {
        using namespace mouse_wheel;
        procedure(0x0,false);clock_step+=20;
        Cpu m{};m.r[1]=kLinkBase+0x5000;stf32(m.r[1]+0x14,3.5f);
        auto rate=[&](double r){++clock_step;m.f[28].ps0=r;site_0251053C(&m);return m.f[28].ps0;};
        auto lat=[&](double d){m.f[1].ps0=d;m.f[2].ps0=-10;m.f[3].ps0=60;site_02510DEC(&m);return m.f[1].ps0;};
        auto wheel=[&](float dy){mods::mouse_wheel(dy);input::PadState pad;mods::filter_pad(pad);return pad;};
        // Bindings (the default): the camera gets nothing, nothing waits
        assert(kDefault==kBindings&&owns_wheel(kFirstPerson)&&owns_wheel(kZoom)&&!owns_wheel(kBindings));
        mods::wheel_mode=kBindings;wheel(1);assert(rate(0)==0&&rate(.5)==.5);
        // Zoom, smoothing Off: a notch's 4 steps of the full rate at once, then the stick's own again
        mods::wheel_mode=kZoom;mods::wheel_smooth=kSmoothOff;
        wheel(1);assert(rate(0)==4&&rate(0)==0&&rate(-.3)==-.3);
        wheel(-1);assert(rate(0)==-4&&rate(0)==0);  // wheel down: further
        // with the stick: the larger of the two (the stick's range, the wheel's step) the same way, the sum against it
        wheel(1);assert(rate(.5)==4);wheel(1);assert(rate(-1)==3);wheel(-1);assert(rate(-3)==-4);assert(rate(0)==0);
        // a trackpad's fractions: their share of a notch (0.1 notch: 0.4 steps of the full rate)
        wheel(.1f);assert(std::fabs(rate(0)-.4)<1e-6&&rate(0)==0);
        // a fast scroll waits 4 notches at most
        wheel(10);assert(rate(0)==kMaxPending&&rate(0)==0);
        // smoothing: an exponential ease-out over its catch-up time (95 % at 3 tau), the same total; the ends snap
        assert(smooth_part(kSmoothOff)==1&&smooth_part(kSmoothCount)==1&&kSmoothDefault==2&&kSmoothSeconds[2]==.3f);
        for(int i=1;i<kSmoothCount;++i) assert(smooth_part(i)>0&&smooth_part(i)<1&&(i==1||smooth_part(i)<smooth_part(i-1)));
        for(int ch:{1,2,3,4}){
            mods::wheel_smooth=ch;wheel(1);
            const double part=smooth_part(ch);double sum=0,first=0;int n=0,at95=-1;
            for(double r;(r=rate(0))!=0;++n){if(!n) first=r;assert(r>0&&r<=first+1e-9);sum+=r;if(at95<0&&sum>=.95*4) at95=n+1;}
            assert(std::fabs(first-4*part)<1e-5&&std::fabs(sum-4)<1e-4);
            const int steps95=int(std::lround(kSmoothSeconds[ch]*30));  // the catch-up time in 30 Hz steps
            assert(std::abs(at95-steps95)<=1&&n<=steps95*2+2);
        }
        // turning the other way stops what waits first
        mods::wheel_smooth=2;wheel(2);assert(rate(0)>0);wheel(-1);assert(rate(0)<0);while(rate(0)!=0){}
        // Look up and down: the wheel's rate stays in f28 (the game's zoom, with its latitude part); the stick's own
        // rate goes into the latitude only, added to the game's delta
        mods::wheel_smooth=kSmoothOff;look_pitch=true;
        wheel(1);assert(rate(.7)==4&&close((float)lat(-4*3.5),-14.f-2.45f));
        wheel(.25f);assert(rate(0)==1&&close((float)lat(-1*3.5),-3.5f));  // the wheel alone: the game's zoom as it is
        assert(rate(.7)==0&&close((float)lat(-0.0),-2.45f));  // over: Look up and down as before
        zoom_held=true;wheel(1);assert(rate(.5)==4);zoom_held=false;  // Zoom while holding: as with Zoom
        look_pitch=false;
        // nobody takes it (no manualCamera): dropped after kDropSteps steps, it does not zoom later
        wheel(1);clock_step+=kDropSteps+1;wheel(0);assert(rate(0)==0);
        mods::wheel_smooth=4;wheel(1);assert(rate(0)>0);clock_step+=kDropSteps+1;wheel(0);assert(rate(0)==0);  // an ease too
        mods::wheel_smooth=kSmoothOff;
        // first person (the subject camera ran) and item aims: the wheel zooms nothing, also after
        c.r[3]=kLinkBase;hook_025071FC(&c);wheel(1);assert(rate(0)==0);
        clock_step+=10;procedure(0x94,false);wheel(1);procedure(0x0,false);assert(rate(0)==0);
        // nextMode: a waiting zoom wakes the manual camera in its decision only (a stick magnitude below the stick
        // as a button), for styles with manualCamera; a pushed stick keeps its own value
        st32(0x10049348,1);st16(0x10049350+0x18+24,0);st32(kCamBase+0x51C,0);  // camera type 0 of 1: style 0
        st32(0x1004487C+4,10);  // style 0: MANUAL_CAMERA
        auto next=[&](float value){++clock_step;Cpu n{};n.r[3]=kCamBase;n.r[4]=0;stf32(kCamBase+0x178,value);stf32(kCamBase+0x174,.2f);
                                   next_mode_value=-1;hook_024FA9C8(&n);return next_mode_value;};
        assert(next(0)==0);                                   // nothing waits
        wheel(1);assert(close(next(0),.45f)&&next_mode_y==0);  // woken
        assert(ldf32(kCamBase+0x178)==0&&close((float)ldf32(kCamBase+0x174),.2f));  // put back after the decision
        assert(close(next(.8f),.8f)&&close(next_mode_y,.2f));  // the stick pushed: as it is
        // the camera's hold after an event (mEventFlags 0x200000): released once nextMode chose the manual camera
        st32(kCamBase+0x510,0x200000|0x20);next_mode_result=0;next(0);assert(ld32(kCamBase+0x510)==(0x200000|0x20));
        next_mode_result=12;next(0);assert(ld32(kCamBase+0x510)==0x20);
        st32(0x1004487C+4,3);assert(next(0)==0);st32(0x1004487C+4,10);  // a style without manualCamera
        while(rate(0)!=0){}
        assert(next(0)==0);                                   // all given: no more wake
        st32(kCamBase+0x510,0x200000);next(0);assert(ld32(kCamBase+0x510)==0x200000);  // nothing waits: the hold stays
        // First person: the wheel is the one-step R3 press as before; it never zooms
        mods::wheel_mode=kFirstPerson;clock_step+=20;
        mods::mouse_wheel(1);input::PadState fp;mods::filter_pad(fp);assert(fp.buttons&input::kStickR);
        assert(rate(0)==0);
        mods::wheel_mode=kBindings;mods::wheel_smooth=kSmoothDefault;
        // the pure rules: the old switch, frame_rate for true 60 (each step of a frame: the frame's rate)
        assert(from_switch(kZoom,false)==kZoom&&from_switch(kFirstPerson,false)==kBindings&&from_switch(kBindings,true)==kFirstPerson);
        Zoom z;add(z,1,1);
        assert(frame_rate(z,7,1,.25f)==1&&frame_rate(z,7,1,.25f)==1&&z.pending==3);  // frame 7: two steps, one frame's rate
        assert(frame_rate(z,8,2,.25f)==.75f&&z.pending==2.25f);
        add(z,-0.0f,3);add(z,NAN,3);assert(z.pending==2.25f);  // nothing
        drop_stale(z,2+kDropSteps);assert(z.pending==2.25f);drop_stale(z,3+kDropSteps);assert(z.pending==0&&!wants(z));
        add(z,.25f,10);assert(frame_rate(z,9,10)==1&&z.pending==0&&wants(z));  // the frame's other steps still zoom
        assert(frame_rate(z,9,10)==1&&frame_rate(z,10,11)==0&&!wants(z));   // the next frame: over
    }
    // First-person aiming: Left stick (classic) (issue #666): the production filter, the subject-camera hook and
    // Link's live status (gameInfo + 0x5CD8 / 0x5CDC, mapped above)
    {
        using namespace mods::playctx;
        constexpr uint32_t st0=0x1046F0B0+kPlayerStatus0,st1=0x1046F0B0+kPlayerStatus1;
        ctx.flags=kLink|kInPlay;
        auto read=[](){input::PadState p;p.lx=.6f;p.ly=-.3f;p.rx=.1f;p.ry=.9f;mods::aim_stick_filter(p);return p;};
        auto as_game=[](const input::PadState& p){return p.lx==.6f&&p.ly==-.3f&&p.rx==.1f&&p.ry==.9f;};
        auto swapped=[](const input::PadState& p){return p.lx==.1f&&p.ly==.9f&&p.rx==.6f&&p.ry==-.3f;};
        clock_step+=20;procedure(0x94,false);st32(st0,kSt0BowAim);st32(st1,0);
        c.r[3]=kLinkBase;hook_025071FC(&c);  // the subject camera runs (bow in first person)
        assert(as_game(read()));                  // Right stick (HD), the default: nothing changes
        aim_left=true;
        assert(swapped(read()));                  // Left stick (classic): the left stick aims, the right one walks
        ++clock_step;assert(swapped(read()));      // the camera ran in the step before this read
        // each view: R3 look, telescope, Picto Box, the other aims (also from the boat)
        for(uint32_t w:{uint32_t(kSt0FirstPerson),uint32_t(kSt0Telescope),uint32_t(kSt0HookshotAim),uint32_t(kSt0BoomerangAim),uint32_t(kSt0RopeAim)}){
            st32(st0,w);hook_025071FC(&c);assert(swapped(read()));
        }
        st32(st0,kSt0ShipRide|kSt0BowAim);procedure(0x8B);hook_025071FC(&c);assert(swapped(read()));
        st32(st0,0);st32(st1,kSt1PictoAim);procedure(0x94,false);hook_025071FC(&c);assert(swapped(read()));
        // the album, a choice, a talk, an event or a menu take the left stick back
        for(uint32_t f:{uint32_t(kAlbum),uint32_t(kChoice),uint32_t(kEvent),uint32_t(kMenu),uint32_t(kFade)}){ctx.flags=kLink|f;assert(as_game(read()));}
        ctx.flags=kLink;ctx.mesg_status=1;assert(as_game(read()));  // a talk's text
        st32(st0,kSt0Telescope);st32(st1,0);assert(swapped(read()));  // the telescope's own message status: still aims
        ctx.mesg_status=0;ctx.flags=kLink|kInPlay;
        // the cannon keeps its own left-stick aim
        st32(st0,kSt0ShipRide);procedure(0x8E);hook_025071FC(&c);assert(as_game(read()));
        // leaving first person: Link's status ends it at once; the lock-on aim (no subject camera) never swaps
        procedure(0x94,false);st32(st0,kSt0BowAim);hook_025071FC(&c);assert(swapped(read()));
        st32(st0,0);assert(as_game(read()));
        procedure(0x95,false);st32(st0,kSt0BowAim);clock_step+=3;assert(as_game(read()));  // bow while targeting
        // on foot, no first person: nothing changes
        procedure(0x0,false);st32(st0,0);clock_step+=10;assert(as_game(read()));
        aim_left=false;ctx=mods::playctx::PlayContext{};
    }
    puts("First-person aiming: Left stick (classic) swaps the sticks only in first person, Right stick (HD) by default PASS");
    puts("Cannon mouse: routing, gyro exclusion, priority, sensitivity, repeated reads and mode transitions PASS");
    puts("Look up and down: the vertical rate goes to the latitude alone, once per manualCamera run, off by default PASS");
    puts("Zoom while holding: the original zoom while held, L's tap on release, ZL and Off, Zoom (original) unchanged PASS");
    puts("Mouse wheel: Zoom: a notch = 4 steps of the full rate, smoothing, fractions, both stick choices, wake, hold, dropped zoom PASS");
    puts("First-person mouse look: degrees per view, once per step, ease, inversion, stale movement, mouse camera off PASS");
    puts("First-person mouse look: later reads of a step keep their movement, First-person sensitivity, 1 degree per point PASS");
}
