// Game-free check of the actual HLE writers and guest-endian controller packets.
#include "runtime.h"
#include "input.h"
#include "motion/motion.h"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

static input::PadState sample;
static bool pro=false, repeat=false, held=false, held_gamepad=false, live_pro=false;
HleReg::HleReg(const char*,const char*,PpcFunc){}
void log_msg(const char*,...){}
bool g_trace_hle=false;
namespace input { PadState read(){return sample;} bool pro_controller(){return pro;} }
namespace controller_reapply { bool holds_pro_controller(){return held;} bool holds_gamepad(){return held_gamepad;} bool pro_controller_live(){return live_pro;} }
namespace crashrec { input::PadState read(int){return sample;} }
namespace interp { bool repeat_input(){return repeat;} void trace_read(const char*){} uint64_t logic_steps(){return 0;} }
namespace mods { void move_speed_input(uint32_t){} void jump_input(uint32_t){} void fast_forward_input(uint32_t&){} void moon_jump_input(const input::PadState&){} void pad_play_rules(input::PadState&){} void item_ring_pad(input::PadState&, bool){} void context_touch_pad(input::PadState&, bool){} void touch_talk_pad(input::PadState&, bool){} void one_screen_pad(input::PadState&){} void guest_pad_filter(Cpu*, input::PadState&, bool){} }
namespace motion { void right_stick(float,float){} VpadMotion vpad(bool){return {};} }
namespace threads { void park_sleep_until(std::chrono::steady_clock::time_point,bool,void (*)()){} }
namespace story_notebook { void pad(input::PadState&, Cpu*){} void mute(input::PadState&){} }  // the notebook page (not under test)
namespace story_scenes {void mute(input::PadState&){} }
namespace story_bag { void pad(input::PadState&, Cpu*){} void mute(input::PadState&){} }  // the bag pages (not under test)
namespace game_settings_page { void pad(input::PadState&, Cpu*){} void mute(input::PadState&){} }  // the settings page (not under test)
namespace menu_sounds { void service(Cpu*){} }  // the Settings overlay's menu sounds (not under test)
namespace player_name { void service(Cpu*){} }  // a new name for Link (not under test)
namespace long_name { void service(Cpu*){} }    // save file names over 8 characters (not under test)
extern "C" void f_0203DEEC_orig(Cpu*){}
extern "C" void imp_vpad_VPADRead(Cpu*);
extern "C" void imp_padscore_KPADReadEx(Cpu*);

int main(){
 constexpr uint32_t at=0x10000,err=0x10200;
#ifdef _WIN32
 void* memory=VirtualAlloc(PPC_MEM_BASE,0x20000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
 assert(memory==PPC_MEM_BASE);
 memory=PPC_MEM_BASE+at;
#else
 void* memory=mmap(PPC_MEM_BASE+at,0x1000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON|MAP_FIXED,-1,0);
#endif
 assert(memory==PPC_MEM_BASE+at);
 auto read=[&](bool kpad){Cpu c{};c.r[3]=0;c.r[4]=at;c.r[5]=1;c.r[6]=err;
  (kpad?imp_padscore_KPADReadEx:imp_vpad_VPADRead)(&c);assert(c.r[3]==1&&ld32(err)==0);};
 const uint32_t bits[]={input::kUp,input::kDown,input::kLeft,input::kRight};
 const uint32_t urcc[]={1,0x4000,2,0x8000};
 for(pro=false;;pro=true){
  const auto hold=pro?0x60:0,trigger=hold+4,release=hold+8,stick=pro?0x6C:0x0C;
  for(int d=0;d<4;++d){
   sample={};read(pro);assert(ld32(at+hold)==0);
   sample.buttons=bits[d];read(pro);const uint32_t expected=pro?urcc[d]:bits[d];
   assert(ld32(at+hold)==expected&&ld32(at+trigger)==expected&&ld32(at+release)==0);
   repeat=true;sample={};read(pro);assert(ld32(at+hold)==expected&&ld32(at+trigger)==0);
   repeat=false;read(pro);assert(ld32(at+release)==expected&&ld32(at+hold)==0);
   sample.lx=d==2?-1.f:d==3?1.f:0.f;sample.ly=d==0?1.f:d==1?-1.f:0.f;
   read(pro);assert(ldf32(at+stick)==sample.lx&&ldf32(at+stick+4)==sample.ly);
   if(!pro){const uint32_t analogBits[]={0x10000000,0x08000000,0x40000000,0x20000000};assert(ld32(at)==analogBits[d]);}
   else assert(ld8(at+0x5C)==31&&ld8(at+0x5F)==22);

   // Interpolation's repeated read reuses the previous analog sample (fresh input is not read).
   const auto prior=sample;sample={};repeat=true;read(pro);
   assert(ldf32(at+stick)==prior.lx&&ldf32(at+stick+4)==prior.ly);
   read(pro);assert(ldf32(at+stick)==prior.lx&&ldf32(at+stick+4)==prior.ly);
   repeat=false;sample={};read(pro);
   printf("mode=%s direction=%d packet/edges/repeat/sticks passed\n",pro?"pro":"gamepad",d);
  }
  if(pro)break;
 }
 // Both analog sticks preserve the down sign in both packets.
 for(bool mode:{false,true}){
  pro=mode;sample={};sample.ry=-1;read(pro);
  assert(ldf32(at+(pro?0x78:0x18))==-1);
  if(!pro)assert(ld32(at)==0x00800000);
  sample={};read(pro);
 }
 pro=true;
 // In Pro mode the GamePad remains idle while KPAD receives the actual controls.
 sample.buttons=input::kDown;sample.ly=-1;read(false);
 assert(ld32(at)==0&&ldf32(at+0x10)==0);
 read(true);assert(ld32(at+0x60)==0x4000&&ldf32(at+0x70)==-1);
 // A controller switch after a state load keeps the Pro Controller connected but idle; the GamePad has the controls.
 pro=false;held=true;read(true);assert(ld8(at+0x5C)==31&&ld32(at+0x60)==0&&ldf32(at+0x70)==0);
 read(false);assert(ld32(at)&0x100);
 // ... unless the game still reads it (Pro Controller -> GamePad in the pause menu): it keeps its controls
 sample={};read(true);live_pro=true;sample.buttons=input::kB;sample.ly=-1;
 read(true);assert(ld32(at+0x60)==0x40&&ld32(at+0x64)==0x40&&ldf32(at+0x70)==-1);
 live_pro=false;read(true);assert(ld32(at+0x60)==0&&ld32(at+0x68)==0x40&&ldf32(at+0x70)==0);
 held=false;{Cpu c{};c.r[3]=0;c.r[4]=at;c.r[5]=1;c.r[6]=err;imp_padscore_KPADReadEx(&c);assert(c.r[3]==0);}
 // Issue #330: switched to the Pro Controller while the game still reads the GamePad (a menu, the file
 // select): the GamePad keeps its buttons and sticks until the game's mode has followed, then goes idle.
 pro=true;held_gamepad=true;sample={};read(false);
 sample.buttons=input::kB;sample.ly=-1;read(false);assert(ld32(at)&input::kB&&ld32(at+4)&input::kB&&ldf32(at+0x10)==-1);
 read(true);assert(ld32(at+0x60)==0x40);  // the Pro Controller has the same press (the game reads one of them)
 held_gamepad=false;read(false);assert(ld32(at)==0&&ldf32(at+0x10)==0&&ld32(at+8)&input::kB);
#ifdef _WIN32
 VirtualFree(PPC_MEM_BASE,0,MEM_RELEASE);
#else
 munmap(memory,0x1000);
#endif
 puts("pad_delivery_test passed");
}
