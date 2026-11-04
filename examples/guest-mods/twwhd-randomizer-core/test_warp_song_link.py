"""Complete Link song initialization/replay consumers and callback capture contracts."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

class WarpSongLink(unittest.TestCase):
    def test_complete_link_consumers(self):
        source=r'''
#define ID_ship_item 1
#define ID_common_init 2
#define ID_volume 3
#define ID_reset_upper 4
#define ID_animation 5
#define ID_init_ship_item 6
#define ID_create_actor 7
#define ID_emitter 8
#define ID_ship_position 9
#define ID_reset 10
#define ID_texture 11
#define ID_usable 12
#define ID_sound 13
#define ID_end_init 14
#define ID_ambient 15
#define ID_message_ended 16
#define ID_camera 17
#define ID_camera_end 18
#define ID_end_demo 19
#define ID_save_result 20
#define ID_melody 21
#define ID_cut_end 22
#define ID_message_status 23
#define ID_message_set_status 24
#define ID_find_actor 25

#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <stdio.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint16_t u16;typedef int16_t s16;typedef uint8_t u8;typedef int8_t s8;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 memory[0xC0000],actor_before[0x7000],play_before[0x7000],save_before[0x1000];
static const u32 self=0x10000,info=0x20000,ship=0x30000,system1=0x40000,system2=0x41000,manager=0x50000,manager2=0x51000,stage_info=0x60000,model=0x70000;
#define DATA(a) (0x80000u+((a)&0x1FFFFu))
static void *at(u32 a){return a<sizeof memory?memory+a:(void*)a;}
#define R_SONG_u32(a) (*(uint32_t*)at(a))
#define R_SONG_s32(a) (*(int32_t*)at(a))
#define R_SONG_u16(a) (*(uint16_t*)at(a))
#define R_SONG_s16(a) (*(int16_t*)at(a))
#define R_SONG_u8(a) (*(uint8_t*)at(a))
#define R_SONG_s8(a) (*(int8_t*)at(a))
#define R_SONG_f32(a) (*(float*)at(a))
#define R_SONG(t,a) R_SONG_##t(a)
#define WWHD_GAME_DATA(a,t) R_SONG(t,DATA(a))
static unsigned trace[128],n,created,mutation,checks;static s32 cfg_use,cfg_usable,cfg_ended;static u32 cfg_status,cfg_partner;static s32 last_animation,last_end;
static void record(unsigned x){assert(n<128);trace[n++]=x;}
static u32 wwhd_dComIfG_getGameInfo(void){record(100);if(mutation==9)R_SONG(s32,self+0x69DC)--;return info;}
static u32 fake_stage_query(u32 p){record(101);assert(p==info+0x5150);return stage_info;}
#define SONG_STAGE_INFO(p) fake_stage_query(p)
#include "warp_song_link.h"
s32 song_ship_item(u32 p0,s32 p1){record(1);(void)p0;(void)p1;assert(p0==self&&p1==0);return cfg_use;}
s32 song_common_init(u32 p0,s32 p1){record(2);(void)p0;(void)p1;assert(p0==self&&p1==0x9B);return 1;}
void song_volume(f32 p0){record(3);(void)p0;assert(p0==0);}
s32 song_reset_upper(u32 p0,s32 p1,f32 p2){record(4);(void)p0;(void)p1;(void)p2;assert(p0==self&&(p1==1||p1==2)&&p2==-1);return 1;}
s32 song_animation(u32 p0,s32 p1,f32 p2,f32 p3,s32 p4,f32 p5){record(5);(void)p0;(void)p1;(void)p2;(void)p3;(void)p4;(void)p5;assert(p0==self&&p3==0&&p4==-1);last_animation=p1;if(p1==0xA8)assert(p2==0.8f&&p5==10);else assert(p2==1&&p5==3);return 1;}
void song_init_ship_item(u32 p0,s32 p1,s32 p2){record(6);(void)p0;(void)p1;(void)p2;assert(p0==self&&p1==cfg_use&&p2==2);if(mutation==1)R_SONG(s32,self+0x69C0)=1;}
u32 song_create_actor(s32 p0,u32 p1,u32 p2,s32 p3,u32 p4,u32 p5,s32 p6,u32 p7){record(7);(void)p0;(void)p1;(void)p2;(void)p3;(void)p4;(void)p5;(void)p6;(void)p7;assert(p0==0x1BB&&p1==1&&p2==self+0x314&&p3==-128&&!p4&&!p5&&p6==-1&&!p7);created++;if(mutation==2)R_SONG(u32,self+0x4440)=0;return 0x12345678;}
void song_emitter(u32 p0,u32 p1,u32 p2,u32 p3,u32 p4){record(8);(void)p0;(void)p1;(void)p2;(void)p3;(void)p4;assert(p0==self+0x66F8&&p1==0x51&&p2==(R_SONG(u32,self+0x4440)?model+0xC8:0)&&p3==self+0x314&&!p4);}
void song_ship_position(u32 p0,s32 p1){record(9);(void)p0;(void)p1;assert(p0==self&&p1==1);if(mutation==7)R_SONG(s16,self+0x32A)=3000;}
void song_reset(void){record(10);}
void song_texture(u32 p0,u32 p1,s32 p2){record(11);(void)p0;(void)p1;(void)p2;assert(p0==self&&p1==6&&p2==0);}
s32 song_usable(u32 p0){record(12);(void)p0;assert(p0==self);if(mutation==3)R_SONG(s32,self+0x69C0)=7;return cfg_usable;}
void song_sound(u32 p0){record(13);(void)p0;assert(p0==0x871);if(mutation==8)WWHD_GAME_DATA(0x101F4B5C,u32)=manager2;}
s32 song_end_init(u32 p0,s32 p1){record(14);(void)p0;(void)p1;assert(p0==self);last_end=p1;return 17;}
void song_ambient(void){record(15);}
s32 song_message_ended(u32 p0,u32 p1){record(16);(void)p0;(void)p1;assert(p0==self&&p1==0x1234);return cfg_ended;}
u32 song_camera(void){record(17);if(mutation==4)R_SONG(u32,self+4)=0xDEAD;return 0xABCD;}
void song_camera_end(u32 p0,u32 p1){record(18);(void)p0;(void)p1;assert(p0==0xABCD&&p1==R_SONG(u32,self+4));}
void song_end_demo(u32 p0){record(19);(void)p0;assert(p0==self);if(mutation==5)WWHD_GAME_DATA(0x101F8344,u32)=system2;}
void song_save_result(u32 p0,u32 p1){record(20);(void)p0;(void)p1;assert(p0==WWHD_GAME_DATA(0x101F8344,u32)&&p1==(u8)R_SONG(s32,self+0x69C0));}
void song_melody(s32 p0){record(21);(void)p0;assert(p0==R_SONG(s32,self+0x69C0));}
void song_cut_end(u32 p0,s32 p1){record(22);(void)p0;(void)p1;assert(p0==info+0x52C4&&p1==(mutation==9?R_SONG(s32,self+0x69DC)+1:-2));}
u32 song_message_status(u32 p0){record(23);(void)p0;assert(p0==manager);if(mutation==6)WWHD_GAME_DATA(0x101F4B5C,u32)=manager2;return cfg_status;}
void song_message_set_status(u32 p0,s32 p1){record(24);(void)p0;(void)p1;assert(p0==manager&&p1==0x13);}
u32 song_find_actor(u32 p0,u32 p1){record(25);(void)p0;(void)p1;assert(p0==0x025E1234&&*(u32*)p1==7);return cfg_partner?0x9999:0;}
static void reset(void){memset(memory,0,sizeof memory);n=created=mutation=0;cfg_use=cfg_usable=cfg_ended=0;cfg_status=cfg_partner=0;last_animation=last_end=-99;
 WWHD_GAME_DATA(0x101F8344,u32)=system1;WWHD_GAME_DATA(0x101F4B5C,u32)=manager;
 R_SONG(u32,info+0x5B3C)=ship;R_SONG(u32,self+0x4440)=model;R_SONG(s8,self+0x326)=-128;R_SONG(s32,self+0x69DC)=-2;
 R_SONG(u32,self+0x6A84)=0xFFFFFFFF;R_SONG(f32,self+0x5898)=1;R_SONG(u32,self+0x6A70)=0xA5A5FFFF;R_SONG(u32,self+4)=0x9988;
 R_SONG(u32,stage_info+0xC)=7<<16;
}
static void no_extra_writes(unsigned init,unsigned on_ship){
 for(unsigned i=0;i<sizeof actor_before;i++){
  const int allowed=init?((i>=0x69C0&&i<0x69C8)||(i>=0x6A80&&i<0x6A84)||(i>=0x6920&&i<0x6922)||(i>=0x691E&&i<0x6920)||(i>=0x6A7C&&i<0x6A80)||(i>=0x6916&&i<0x6918)):
  ((on_ship&&i>=0x32A&&i<0x32C)||(i>=0x6920&&i<0x6922)||(i>=0x6A80&&i<0x6A84)||(i>=0x6916&&i<0x6918)||(i>=0x6A70&&i<0x6A74));
  assert(allowed||actor_before[i]==memory[self+i]);
 }
 for(unsigned i=0;i<sizeof play_before;i++)if(!((init&&i>=0x5CDC&&i<0x5CE0)||(!init&&(i==0x5BD1||(i>=0x52B8&&i<0x52BA)))))assert(play_before[i]==memory[info+i]);
 assert(!memcmp(save_before,memory+0x78000,sizeof save_before));
}
static uint32_t rng=0x1A2B3C4D;
static unsigned next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(void){
 const s32 args[]={-1,0,1,65536};
 for(s32 selected=-2;selected<258;selected++)for(unsigned use=0;use<2;use++)for(unsigned w=0;w<4;w++)for(unsigned msg=0;msg<4;msg++){
  reset();cfg_use=use;R_SONG(u8,system1+0x241)=(u8)selected;memcpy(actor_before,memory+self,sizeof actor_before);memcpy(play_before,memory+info,sizeof play_before);memcpy(save_before,memory+0x78000,sizeof save_before);
  assert(randomizer_song_play_init(self,selected,args[w],args[msg])==1);
  assert(R_SONG(s32,self+0x69C0)==selected&&R_SONG(s32,self+0x69C4)==args[w]&&R_SONG(s16,self+0x691E)==(s16)args[msg]);
  assert(R_SONG(u32,self+0x6A80)==0xFFFFFFFF&&R_SONG(s16,self+0x6920)==10&&R_SONG(s16,self+0x6916)==0&&R_SONG(u32,self+0x6A7C)==0);
  assert(last_animation==(selected==0?0xE4:selected==1?0xE5:selected==5?0xE6:selected==4?0xE7:selected==2?0xE8:0xE9));
  const unsigned create=use&&selected==1&&args[w]!=0;assert(created==create);if(create)assert(R_SONG(u32,ship+0x714)==0x12345678);
  unsigned expected[]={ID_ship_item,ID_common_init,100,ID_volume,ID_reset_upper,ID_reset_upper,ID_animation,ID_init_ship_item};assert(n>=9&&!memcmp(trace,expected,sizeof expected));
  unsigned pos=8;if(use&&selected==1){assert(trace[pos++]==100&&trace[pos++]==101);if(create)assert(trace[pos++]==100&&trace[pos++]==ID_create_actor);}assert(trace[pos++]==ID_emitter&&pos==n);
  no_extra_writes(1,0);checks++;
 }
 // Virtual stage result, cached warp argument and post-callback actor model.
 for(unsigned type=0;type<8;type++)for(unsigned last=0;last<3;last++){
  reset();cfg_use=1;R_SONG(u32,stage_info+0xC)=type<<16;R_SONG(u8,self+0x6911)=last;assert(randomizer_song_play_init(self,1,1,0)==1&&created==(type==7&&last!=1));checks++;
 }
 reset();cfg_use=1;mutation=1;assert(randomizer_song_play_init(self,0,1,0)==1&&created==1&&last_animation==0xE4);checks++;
 reset();cfg_use=1;mutation=2;assert(randomizer_song_play_init(self,1,1,0)==1&&created==1&&R_SONG(u32,self+0x4440)==0);checks++;
 // Independent path oracle covers ordinary/manual/partner branches after the
 // two replay gates, with both learned-byte states and NaN/signed-zero rates.
 const s32 songs[]={-1,0,1,255};const u32 statuses[]={0,1,0x12};const f32 rates[]={-0.0f,0.0f,0.009f,0.01f,1,INFINITY,NAN};
 for(unsigned sample=0;sample<14000;sample++){
  reset();const s32 selected=songs[next()%4];const s16 no_message=(s16)(next()%3)-1;const s32 warp=(s32)(next()%3)-1;const unsigned active=next()%2,has_message=next()%2,valid_partner=next()%2,learned=next()%2,partner_type=next()%3,on_ship=next()%2,partner_event=next()%2;const s16 timer=(s16)(next()%3);const f32 rate=rates[next()%7];
  cfg_usable=next()%2;cfg_ended=next()%2;cfg_status=statuses[next()%3];cfg_partner=next()%2;
  R_SONG(s32,self+0x69C0)=selected;R_SONG(s32,self+0x69C4)=warp;R_SONG(s16,self+0x691E)=no_message;R_SONG(s16,self+0x6916)=active;R_SONG(u32,self+0x6A7C)=has_message?0x1234:0;
  R_SONG(u32,self+0x6A84)=valid_partner?7:0xFFFFFFFF;R_SONG(u8,self+0x6911)=partner_type;R_SONG(u32,self+0x68AC)=partner_event;R_SONG(s16,self+0x6920)=timer;R_SONG(f32,self+0x5898)=rate;R_SONG(u32,info+0x5CD8)=on_ship?0x10000:0;R_SONG(u8,system1+0x241)=learned?255:0;
  memcpy(actor_before,memory+self,sizeof actor_before);memcpy(play_before,memory+info,sizeof play_before);memcpy(save_before,memory+0x78000,sizeof save_before);
  unsigned expected[128],length=0;expected[length++]=100;if(on_ship)expected[length++]=ID_ship_position;
  const unsigned replay=no_message==0&&warp!=0&&(!valid_partner||(u32)partner_type!=(u32)selected);
  s32 expected_return=1,expected_end=-99;unsigned waiting=0,save_calls=0;
  if(replay){expected[length++]=ID_reset;expected[length++]=100;expected[length++]=ID_animation;expected[length++]=ID_texture;expected[length++]=ID_usable;waiting=1;if(cfg_usable){expected[length++]=ID_sound;expected[length++]=ID_end_init;expected_return=17;expected_end=selected;}}
  if(expected_return==1){
   expected[length++]=ID_ambient;
   if(has_message){expected[length++]=ID_message_ended;if(cfg_ended){expected[length++]=ID_reset_upper;expected[length++]=100;expected[length++]=ID_camera;expected[length++]=ID_camera_end;expected[length++]=ID_end_demo;expected[length++]=ID_save_result;save_calls++;}}
   else {
    if(timer==1)expected[length++]=ID_melody;
    if(active){
     if(no_message){expected[length++]=100;expected[length++]=100;expected[length++]=ID_cut_end;}
     else {expected[length++]=ID_message_status;if(cfg_status==0||cfg_status==0x12){expected[length++]=100;if(cfg_status==0x12)expected[length++]=ID_message_set_status;
      const unsigned queried=valid_partner&&(u32)partner_type==(u32)selected;if(queried)expected[length++]=ID_find_actor;
      if(!warp){expected[length++]=100;expected[length++]=ID_cut_end;}
      else if(queried&&cfg_partner){if(!partner_event){expected[length++]=ID_reset_upper;expected[length++]=100;expected[length++]=ID_camera;expected[length++]=ID_camera_end;expected[length++]=ID_end_demo;}else{expected[length++]=ID_end_init;expected_return=17;expected_end=-1;}}
      else {expected[length++]=ID_usable;if(cfg_usable){expected[length++]=ID_save_result;save_calls++;expected[length++]=ID_end_init;expected_return=17;expected_end=selected;}}
     }}
    }else if(!(rate>=0.01f)){expected[length++]=ID_sound;expected[length++]=ID_reset;expected[length++]=ID_animation;expected[length++]=ID_texture;waiting=1;}
   }
  }
  assert(randomizer_song_play(self)==expected_return&&last_end==expected_end);assert(n==length&&!memcmp(trace,expected,length*sizeof *expected));
  if(waiting)assert(last_animation==0xA8&&R_SONG(u32,self+0x6A70)==((0xA5A5FFFFu|0x100)&~0x400u));
  if(on_ship)assert((u16)R_SONG(s16,self+0x32A)==0xC000);
  (void)save_calls;no_extra_writes(0,on_ship);checks++;
 }
 // Reevaluate values after native callbacks, including result propagation,
 // global system replacement and captured message-manager identity.
 reset();R_SONG(s32,self+0x69C4)=1;cfg_usable=1;mutation=3;assert(randomizer_song_play(self)==17&&last_end==7);checks++;
 reset();R_SONG(u32,self+0x6A7C)=0x1234;cfg_ended=1;mutation=5;assert(randomizer_song_play(self)==1&&WWHD_GAME_DATA(0x101F8344,u32)==system2);checks++;
 reset();R_SONG(s16,self+0x6916)=1;cfg_status=0x12;mutation=6;assert(randomizer_song_play(self)==1&&WWHD_GAME_DATA(0x101F4B5C,u32)==manager2);checks++;
 reset();R_SONG(u32,self+0x6A7C)=0x1234;cfg_ended=1;mutation=4;assert(randomizer_song_play(self)==1&&R_SONG(u32,self+4)==0xDEAD);checks++;
 reset();R_SONG(u32,info+0x5CD8)=0x10000;mutation=7;assert(randomizer_song_play(self)==1&&R_SONG(s16,self+0x32A)==(s16)(3000-0x4000));checks++;
 reset();R_SONG(f32,self+0x5898)=NAN;mutation=8;assert(randomizer_song_play(self)==1&&R_SONG(u8,manager2+0x921)==1&&R_SONG(u8,manager+0x921)==0);checks++;
 reset();R_SONG(s16,self+0x6916)=1;R_SONG(s16,self+0x691E)=1;mutation=9;assert(randomizer_song_play(self)==1);checks++;
 printf("Link song consumers: %u complete path cases passed\n",checks);return 0;
}

'''
        with tempfile.TemporaryDirectory() as directory:
            c,exe=Path(directory)/'test.c',Path(directory)/'test'
            c.write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
