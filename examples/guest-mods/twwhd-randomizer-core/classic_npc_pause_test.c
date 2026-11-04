/* Synthetic callback/byte-view tests; no executable/resource bytes. */
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef uint16_t u16;typedef uint8_t u8;typedef int8_t s8;typedef int16_t s16;typedef int32_t s32;typedef float f32;
static unsigned char mem[0x100000];
static unsigned z_accesses;
static __attribute__((unused)) void *at(u32 a){if(a==0x7031C)z_accesses++;return mem+(a&0xFFFFF);}
typedef uint32_t m_u32;typedef uint16_t m_u16;typedef uint8_t m_u8;typedef int8_t m_s8;typedef int16_t m_s16;typedef float m_f32;
#define R_CLASSIC_UI(t,a) (*(volatile m_##t*)at(a))
#define R_CLASSIC_ABE(t,a) R_CLASSIC_UI(t,a)
#define CLASSIC_UI_DATA(a) ((u32)(a))
#define CLASSIC_ABE_DATA(a) ((u32)(a))
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) enum{n##_descriptor=__COUNTER__};r n p
static u32 song_call(u32 target,u32 self);
#define CLASSIC_UI_SONG(t,s) song_call(t,s)
enum{before=__COUNTER__};
#include "classic_abe.h"
#include "classic_pause.h"
enum{after=__COUNTER__};
#if WWHD_RANDOMIZER_CLASSIC_MODE
static const u32 actor=0x70000,ui=0x80000,play=0x90000;
static unsigned gates,kind,song,ready,pro,busy,sound_calls,begin_calls,guide_kind,gameover_calls,photo_calls,mode_calls,song_calls;
static u32 selected;static unsigned trace[128],ntrace;
static void log_event(unsigned x){assert(ntrace<128);trace[ntrace++]=x;}
void classic_ui_event(u32 out,u32 queue){assert(queue==0x1230);log_event(1);memset((void*)out,0,40);((u32*)out)[0]=kind;}
void classic_ui_state(u32 self,u32 state){assert(self==ui+0x10);log_event(2);selected=state;R_CLASSIC_UI(u8,ui+0x23C)=77;}
u32 classic_ui_play(void){log_event(3);return play;}
u32 classic_ui_gameover(u32 self){assert(self==ui);log_event(4);gameover_calls++;return gates&1;}
u32 classic_ui_password(u32 self){assert(self==ui);log_event(5);return gates&64;}
u32 classic_ui_fishman(u32 self){assert(self==ui);log_event(6);return gates&128;}
u32 classic_ui_drc(u32 self){assert(self==ui);log_event(7);return gates&512;}
u32 classic_ui_mode(u32 self){assert(self==ui);log_event(8);mode_calls++;return gates&1024;}
void classic_ui_show_guide(u32 guide){assert(guide==0x1111);log_event(9);guide_kind=1;}
void classic_ui_hide_guide(u32 guide){assert(guide==0x1111);log_event(10);guide_kind=2;}
void classic_ui_set_guide(u32 guide,u32 value){assert(guide==0x1111&&!value);log_event(11);guide_kind=3;}
void classic_ui_photo(u32 window){assert(window==0x2222);log_event(12);photo_calls++;}
u32 classic_ui_tv(u32 controller){assert(controller==0x3333);log_event(13);return gates&2048;}
u32 classic_ui_pro(u32 controller){assert(controller==0x3333);log_event(14);return pro;}
u32 classic_ui_busy(u32 framework){assert(framework==0x4444);log_event(15);return busy;}
u32 classic_ui_wind_ready(void){log_event(16);return ready;}
void classic_ui_sound(u32 name){assert(name==0x1013F068);log_event(17);assert(selected==0x1049EE10);sound_calls++;}
void classic_ui_sound_begin(void){log_event(18);assert(sound_calls==1);begin_calls++;}
static u32 song_call(u32 target,u32 self){assert(target==0xDEAD&&self==0xB000);log_event(19);song_calls++;return song;}
static void reset_ui(void){memset(mem,0,sizeof mem);ntrace=0;selected=0;sound_calls=begin_calls=guide_kind=gameover_calls=photo_calls=mode_calls=song_calls=0;
 R_CLASSIC_UI(u32,0x101F4FF0)=0x1200;R_CLASSIC_UI(u32,0x101F5088)=0x3333;R_CLASSIC_UI(u32,0x1018F4AC)=0x4444;
 R_CLASSIC_UI(u32,ui+0x1E0)=0x1111;R_CLASSIC_UI(u32,ui+0x180)=0x2222;R_CLASSIC_UI(u8,ui+0x23C)=99;
 R_CLASSIC_UI(u32,play+0x5B2C)=0xB000;R_CLASSIC_UI(u32,0xB0B4)=0xC000;R_CLASSIC_UI(u32,0xC02C)=0xDEAD;
}
static u32 menu_state(unsigned k){switch(k){case 0x2A:return 0x1049ED08;case 0x2B:return 0x1049ED60;case 0x62:return 0x1049EC84;case 0x66:return busy?0:0x1049EFF4;case 0x67:return busy?0:0x1049EF70;case 0x68:return busy?0:0x1049EF44;case 0x69:return 0x1049F04C;case 0x6A:return 0x1049EF9C;case 0x6D:return 0x1049F2E0;default:return 0;}}
static void test_ui(void){
 for(kind=0;kind<256;kind++)for(gates=0;gates<4096;gates++)for(busy=0;busy<2;busy++){
  reset_ui();R_CLASSIC_UI(u8,play+0x5BB0)=!!(gates&2);R_CLASSIC_UI(u8,0x1047B097)=!!(gates&4);R_CLASSIC_UI(u8,play+0x5BE8)=gates&8?2:1;R_CLASSIC_UI(u32,play+0x5CD8)=gates&16?0x200000:0;R_CLASSIC_UI(u32,play+0x5CDC)=gates&32?8:0;R_CLASSIC_UI(u8,ui+0x243)=!!(gates&256);
  /* mMenuAllowed intentionally stays zero: branch is removed, not initial death check. */
  randomizer_classic_play_execute(ui);
  const u32 expected=gates&1?0x1049F46C:gates&2?0x1049EC2C:gates&4?0x1049EDB8:gates&8?0x1049F288:gates&16?0x1049F128:gates&32?0x1049F180:gates&64?0x1049F338:gates&128?0x1049F390:gates&256?0x1049F0D0:gates&512?0x1049EC2C:gates&1024?0:gates&2048?(kind==0x62?0x1049EC84:0):menu_state(kind);
  assert(selected==expected&&gameover_calls==1&&trace[0]==1&&trace[1]==4);
  if(!(gates&1023)&&(gates&1024))assert(!R_CLASSIC_UI(u8,ui+0x23C));
  assert(photo_calls==(!(gates&511)&&(gates&512)&&kind==0x68));
 }
 for(kind=0;kind<256;kind++)for(song=0;song<9;song++)for(unsigned flags=0;flags<32;flags++){
  reset_ui();ready=flags&1;pro=flags&2;R_CLASSIC_UI(u8,ui+0x242)=!!(flags&4);R_CLASSIC_UI(u8,0x1047B097)=!!(flags&8);R_CLASSIC_UI(u8,play+0x5292)=!!(flags&16);
  /* No songs owned; opening list must still work and reset after callbacks. */
  randomizer_classic_takt_execute(ui);
  u32 expected=kind==0x62?0x1049EE10:song==0&&(flags&4)?0x1049EEC0:song==1&&(flags&4)?0x1049EEEC:song<=5?0x1049EE94:flags&8?0:flags&16?0x1049EC2C:0x1049EBD4;
  assert(selected==expected&&trace[0]==1&&song_calls==(kind!=0x62));
  if(kind==0x62)assert(sound_calls==1&&begin_calls==1&&!R_CLASSIC_UI(u8,ui+0x23C));
  if(kind!=0x62&&song==0&&(flags&4))assert(guide_kind==(ready&&!pro?2:3));
 }
}
static unsigned init_return,init_staff,staff_arg,abe_calls,final_staff;
void classic_abe_staff(u32 self,u32 name,u32 owner){assert(self==actor+0x3E0&&owner==actor);assert(name==0xE000+staff_arg);abe_calls++;}
static u32 init(u32 self,unsigned index){assert(self==actor&&index==init_staff);abe_calls++;R_CLASSIC_ABE(s8,self+0xA21)=final_staff;R_CLASSIC_ABE(u16,self+0x320)=0xAAAA;R_CLASSIC_ABE(u16,self+0x322)=0xBBBB;R_CLASSIC_ABE(u16,self+0x324)=0xCCCC;return init_return;}
u32 classic_abe_init0(u32 s){return init(s,0);}u32 classic_abe_init1(u32 s){return init(s,1);}u32 classic_abe_init2(u32 s){return init(s,2);}u32 classic_abe_init3(u32 s){return init(s,3);}u32 classic_abe_init4(u32 s){return init(s,4);}u32 classic_abe_init5(u32 s){return init(s,5);}u32 classic_abe_init_error(u32 s){return init(s,6);}
void classic_abe_status(u32 self,u32 a,u32 b,u32 owner){assert(self==actor+0x654&&a==255&&b==255&&owner==actor);abe_calls++;}
void classic_abe_cylinder(u32 self,u32 source){assert((self==actor+0x690||self==actor+0x874)&&source==0x101EA190);abe_calls++;}
void classic_abe_translate(u32 m,f32 x,f32 y,f32 z){assert(m==0x1048D0CC&&x==-202066&&y==33&&z==320123);abe_calls++;}
void classic_abe_rotate(u32 m,s32 a){assert(m==0x1048D0CC&&a==(s16)0xBBBB);abe_calls++;}
void classic_abe_mult(u32 m,u32 a,u32 b){assert(m==0x1048D0CC&&((f32*)a)[0]==0&&((f32*)a)[1]==0&&((f32*)a)[2]==80);((f32*)b)[0]=1;((f32*)b)[1]=2;((f32*)b)[2]=3;abe_calls++;}
void classic_abe_center(u32 a,u32 b){assert(a==actor+0x98C&&((f32*)b)[0]==1&&((f32*)b)[2]==3);abe_calls++;}
void classic_abe_radius(u32 a,f32 b){assert(a==actor+0x98C&&b==50);abe_calls++;}
void classic_abe_height(u32 a,f32 b){assert(a==actor+0x98C&&b==30);abe_calls++;}
u32 classic_abe_play(void){abe_calls++;return play;}
void classic_abe_mass(u32 a,u32 b,u32 c){assert(a==play+0x4EF8&&b==actor+0x874&&c==3);abe_calls++;}
void classic_abe_animation(u32 s){assert(s==actor);abe_calls++;}
void classic_abe_correct(u32 a,u32 b){assert(a==actor+0x450&&b==play+0x12A0);abe_calls++;}
u32 classic_abe_room(u32 bg,u32 poly){assert(bg==play+0x12A0&&poly==actor+0x538);abe_calls++;return 0x123;}
u32 classic_abe_color(u32 bg,u32 poly){assert(bg==play+0x12A0&&poly==actor+0x538);abe_calls++;return 0x456;}
void classic_abe_morph(u32 m,f32 zero){assert(m==0xD000&&zero==0);abe_calls++;}
void classic_abe_matrix(u32 s,u32 enabled){assert(s==actor&&enabled==1);abe_calls++;}
static void test_abe(void){for(unsigned staff=0;staff<8;staff++)for(unsigned subtype=0;subtype<4;subtype++)for(init_return=0;init_return<2;init_return++)for(final_staff=0;final_staff<3;final_staff++){
 memset(mem,0,sizeof mem);abe_calls=0;init_staff=staff<6?staff:6;staff_arg=staff;
 R_CLASSIC_ABE(s8,actor+0xA21)=staff;R_CLASSIC_ABE(s8,actor+0xA20)=subtype;R_CLASSIC_ABE(u32,actor+0x44C)=0xD000;
 R_CLASSIC_ABE(u32,0x101C6A38+staff*4)=0xE000+staff;R_CLASSIC_ABE(u8,0x101C6A30+subtype*2)=subtype+40;R_CLASSIC_ABE(u8,0x101C6A31+subtype*2)=subtype+80;
 R_CLASSIC_ABE(f32,actor+0x314)=-202066;R_CLASSIC_ABE(f32,actor+0x318)=33;R_CLASSIC_ABE(f32,actor+0x31C)=320123;
 R_CLASSIC_ABE(f32,0x100235F4)=-202066;R_CLASSIC_ABE(f32,0x100235F8)=320123;R_CLASSIC_ABE(f32,0x100235F0)=10;R_CLASSIC_ABE(f32,0x10023608)=-4.5;R_CLASSIC_ABE(f32,0x1002360C)=80;R_CLASSIC_ABE(f32,0x10023610)=50;R_CLASSIC_ABE(f32,0x100235CC)=30;
 unsigned char positions[36];memcpy(positions,at(actor+0x2FC),36);
 assert(randomizer_classic_abe_init(actor)==init_return);assert(!memcmp(positions,at(actor+0x2FC),36));
 assert(R_CLASSIC_ABE(u8,actor+0x389)==subtype+40&&R_CLASSIC_ABE(u8,actor+0x38B)==subtype+80&&R_CLASSIC_ABE(u32,actor+0x39C)==10);
 if(!init_return){assert(abe_calls==2);continue;}
 assert(R_CLASSIC_ABE(u16,actor+0x9B6)==0xAAAA&&R_CLASSIC_ABE(u16,actor+0x9B8)==0xBBBB&&R_CLASSIC_ABE(u16,actor+0x9BA)==0xCCCC);
 assert(R_CLASSIC_ABE(u8,actor+0x1C9)==0x23&&R_CLASSIC_ABE(u8,actor+0x1CA)==0x56&&R_CLASSIC_ABE(f32,actor+0x374)==-4.5f);
 }
 /* Native BGE is !LT: unordered X/threshold must skip the Z read. */
 for(unsigned nan=0;nan<3;nan++){
  memset(mem,0,sizeof mem);init_return=0;init_staff=staff_arg=final_staff=1;
  R_CLASSIC_ABE(s8,actor+0xA21)=1;R_CLASSIC_ABE(u32,0x101C6A3C)=0xE001;
  R_CLASSIC_ABE(u32,actor+0x314)=nan==1?0x7FC01234:0;
  R_CLASSIC_ABE(u32,0x100235F0)=nan==2?0x7FC05678:0x41200000;
  z_accesses=0;assert(!randomizer_classic_abe_init(actor));assert(z_accesses==(nan==0));
 }
}
#else
static u32 song_call(u32 a,u32 b){return a+b;}
#endif
int main(void){assert(after-before-1==(WWHD_RANDOMIZER_CLASSIC_MODE?3:0));
#if WWHD_RANDOMIZER_CLASSIC_MODE
 test_ui();test_abe();
#else
 (void)song_call;
#endif
 return 0;}
