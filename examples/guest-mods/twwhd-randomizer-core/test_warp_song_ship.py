"""Complete ship warp consumers with controlled callback and data-layout oracles."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class WarpSongShip(unittest.TestCase):
    def test_complete_ship_consumers(self):
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint16_t u16;typedef int16_t s16;typedef uint8_t u8;typedef int8_t s8;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 memory[0x80000],actor_before[0xD200],play_before[0x6000];
static const u32 self=0x10000,info=0x20000,info2=0x30000,tornado=0x40000,model=0x50000,block=0x51000,matrices=0x52000;
static void *at(u32 a){return a<sizeof memory?memory+a:(void*)a;}
#define R_WARP_SHIP_u32(a) (*(uint32_t*)at(a))
#define R_WARP_SHIP_s32(a) (*(int32_t*)at(a))
#define R_WARP_SHIP_u16(a) (*(uint16_t*)at(a))
#define R_WARP_SHIP_s16(a) (*(int16_t*)at(a))
#define R_WARP_SHIP_u8(a) (*(uint8_t*)at(a))
#define R_WARP_SHIP_s8(a) (*(int8_t*)at(a))
#define R_WARP_SHIP_f32(a) (*(float*)at(a))
#define R_WARP_SHIP(t,a) R_WARP_SHIP_##t(a)
static unsigned trace[64],n,particles,scenes,mutate,checks,initializing;
static u32 current_info,search_result;static s32 creating_answer,chase_answer;
static void record(unsigned id){assert(n<64);trace[n++]=id;}
static u32 wwhd_dComIfG_getGameInfo(void){record(1);return current_info;}
#include "warp_song_ship.h"
void warp_ship_part_off(u32 actor){record(2);assert(actor==self);}
s32 warp_ship_reverb(s32 room){record(3);assert(room==-128&&R_WARP_SHIP(f32,self+0x374)==0&&R_WARP_SHIP(f32,self+0x340)==0&&(R_WARP_SHIP(u32,self+0x644)&1));return 7;}
u32 warp_ship_camera(void){record(4);if(initializing)R_WARP_SHIP(u32,self+4)=0xDEAD;return 0xABCD;}
void warp_ship_camera_start(u32 camera,u32 mode,u32 actor,u32 flags){record(5);assert(camera==0xABCD&&mode==14&&actor==0xDEAD&&!flags);}
void warp_ship_colors(u32 ambient,u32 diffuse){record(6);*(u32*)ambient=0x12345678;*(u32*)diffuse=0xAABBCCDD;R_WARP_SHIP(s16,self+0x32A)=3000;}
u32 warp_ship_particle(u32 control,u32 group,u32 id,u32 pos,u32 angle,u32 scale,u32 alpha,u32 callback,s32 setup,u32 primary,u32 environment,u32 scale2d){
 record(7);assert(control==(particles?0x2222:0x1111)&&!group&&id==0x285&&pos==self+0x314&&angle==self+(particles?0x6A2:0x328)&&!scale&&alpha==255&&callback==self+(particles?0xD154:0xD140)&&setup==-1&&!environment&&!scale2d);
 assert(*(u32*)primary==(particles?0x87654321u:0x12345678u));assert((u16)R_WARP_SHIP(s16,self+0x6A4)==(u16)(3000+0x8000));
 *(u32*)primary=0x87654321;current_info=info2;particles++;return 0;
}
u32 warp_ship_judge(u32 judge,u32 key){record(8);assert(judge==0x025E1234&&*(u32*)key==7);return search_result;}
s32 warp_ship_creating(u32 id){record(9);assert(id==7);return creating_answer;}
void warp_ship_follow_end(u32 callback){record(10);assert(callback==self+0xD140||callback==self+0xD154);}
s32 warp_ship_paddle(u32 actor){record(11);assert(actor==self);return 1;}
void warp_ship_camera_end(u32 camera,u32 actor){record(12);assert(camera==0xABCD&&actor==0x9988);}
s32 warp_ship_chase_angle(u32 angle,s32 target,s32 step){record(13);assert(angle==self+0x68E&&target==0x1C25&&step==0x40);return chase_answer;}
s32 warp_ship_chase_float(u32 value,f32 target,f32 step){record(14);assert(step==20||step==50);R_WARP_SHIP(f32,value)=target;return 1;}
s32 warp_ship_scene(s32 exit,f32 speed,u32 mode,s32 room){record(15);assert(exit==0xC5+12&&speed==0&&!mode&&room==-1&&R_WARP_SHIP(s16,self+0x662)==2);scenes++;if(mutate)R_WARP_SHIP(s16,self+0x32A)=1234;return 1;}
void warp_ship_mute_off(void){record(16);if(mutate)R_WARP_SHIP(s16,self+0x32A)=2345;}
static void reset(void){memset(memory,0xA5,sizeof memory);n=particles=scenes=mutate=initializing=0;current_info=info;search_result=tornado;creating_answer=0;chase_answer=0;
 R_WARP_SHIP(u32,self+4)=0x9988;R_WARP_SHIP(s8,self+0x326)=-128;R_WARP_SHIP(u32,info+0x5AB0)=0x1111;R_WARP_SHIP(u32,info2+0x5AB0)=0x2222;
 R_WARP_SHIP(u32,self+0x714)=7;R_WARP_SHIP(u32,tornado+0x3B4)=0;
 R_WARP_SHIP(f32,tornado+0x314)=100;R_WARP_SHIP(f32,tornado+0x31C)=200;R_WARP_SHIP(s32,self+0x6AC)=12;
 R_WARP_SHIP(f32,self+0x6D8)=0;R_WARP_SHIP(s16,self+0x32A)=300;R_WARP_SHIP(f32,self+0x314)=0;R_WARP_SHIP(f32,self+0x31C)=0;
}
int main(void){
 for(unsigned animation=0;animation<256;animation++){
  reset();initializing=1;R_WARP_SHIP(u16,self+0x67A)=animation;memcpy(actor_before,memory+self,sizeof actor_before);
  assert(randomizer_ship_warp_init(self)==1&&particles==2);unsigned expected[10],length=0;if(animation!=10)expected[length++]=2;unsigned tail[]={3,4,5,6,1,7,1,7};memcpy(expected+length,tail,sizeof tail);length+=8;assert(n==length&&!memcmp(trace,expected,length*sizeof *expected));
  assert(R_WARP_SHIP(u8,self+0x635)==14&&R_WARP_SHIP(s16,self+0xD190)==0&&R_WARP_SHIP(s16,self+0xD192)==-1&&R_WARP_SHIP(u32,self+0xD194)==0x02482438&&R_WARP_SHIP(u32,self+0xD198)==0xA5A5A5A5u);
  for(unsigned i=0;i<sizeof actor_before;i++){const int allowed=(i>=4&&i<8)||(i>=0x32A&&i<0x32C)||(i>=0x374&&i<0x378)||(i>=0x340&&i<0x344)||i==0x635||(i>=0x644&&i<0x648)||(i>=0x662&&i<0x664)||(i>=0x68E&&i<0x690)||(i>=0x6A2&&i<0x6A8)||(i>=0xD190&&i<0xD198);assert(allowed||actor_before[i]==memory[self+i]);}checks++;
 }
 const s16 phases[]={-1,0,1,2,3};const s16 deltas[]={0,0x1000,0x1001,0x1C25,-1};const float scales[]={-0.0f,0.8f,0.9f,NAN};const float heights[]={-1000,0,5001,NAN};
 for(unsigned phase=0;phase<5;phase++)for(unsigned delta=0;delta<5;delta++)for(unsigned scale=0;scale<4;scale++)for(unsigned height=0;height<4;height++)for(s32 chase=-1;chase<=1;chase++)for(unsigned found=0;found<2;found++)for(s32 creating=-1;creating<=1;creating++){
  reset();chase_answer=chase;search_result=found?tornado:0;creating_answer=creating;R_WARP_SHIP(s16,self+0x662)=phases[phase];R_WARP_SHIP(s16,self+0x68E)=deltas[delta];R_WARP_SHIP(f32,tornado+0x744)=scales[scale];R_WARP_SHIP(f32,self+0x318)=heights[height];R_WARP_SHIP(f32,self+0x340)=49;
  memcpy(actor_before,memory+self,sizeof actor_before);memcpy(play_before,memory+info,sizeof play_before);
  const unsigned active=found&&creating==0;assert(randomizer_ship_warp(self)==(s32)active);
  assert((u16)R_WARP_SHIP(s16,self+0x32A)==(u16)(300+(u16)deltas[delta])&&R_WARP_SHIP(s16,self+0x322)==R_WARP_SHIP(s16,self+0x32A));
  assert(scenes==(active&&phases[phase]!=2));assert(!memcmp(play_before,memory+info,sizeof play_before));
  if(active){assert(R_WARP_SHIP(f32,self+0x340)==(chase&&!(scales[scale]<=0.8f)?50:49));assert(R_WARP_SHIP(s16,self+0x662)==2);assert((u16)R_WARP_SHIP(s16,self+0x6A4)==(u16)((u16)R_WARP_SHIP(s16,self+0x32A)+0x8000));}
  for(unsigned i=0;i<sizeof actor_before;i++){const int allowed=(i>=0x32A&&i<0x32C)||(i>=0x322&&i<0x324)||(active&&((i>=0x314&&i<0x320)||(i>=0x340&&i<0x344)||(i>=0x662&&i<0x664)||(i>=0x6A4&&i<0x6A6)));assert(allowed||actor_before[i]==memory[self+i]);}checks++;
 }
 reset();R_WARP_SHIP(u32,self+0x714)=0xFFFFFFFFu;assert(randomizer_ship_warp(self)==0);unsigned canceled[]={1,10,10,11,4,12};assert(n==6&&!memcmp(trace,canceled,sizeof canceled)&&!scenes);checks++;
 reset();R_WARP_SHIP(s16,self+0x662)=0;R_WARP_SHIP(s16,self+0x68E)=0;R_WARP_SHIP(f32,self+0x318)=-1000;mutate=1;assert(randomizer_ship_warp(self)==1&&scenes==1&&R_WARP_SHIP(s16,self+0x6A4)==(s16)(2345+0x8000));checks++;
 // Full tornado matrix layout, dirty flag and first-joint-above scan.
 for(unsigned j=0;j<11;j++)for(unsigned axis=0;axis<3;axis++){
  reset();R_WARP_SHIP(u32,tornado+0x3B4)=model;R_WARP_SHIP(u32,model+0x2C)=block;R_WARP_SHIP(u32,block+0x10)=matrices;R_WARP_SHIP(u16,block+4)=0xA5A0;
  for(unsigned k=0;k<11;k++)for(unsigned row=0;row<3;row++)R_WARP_SHIP(f32,matrices+k*0x30+row*16+12)=(float)(k*100+row*10+100);
  assert(warp_ship_joint_position(tornado,j,axis)==(float)(j*100+axis*10+100)&&R_WARP_SHIP(u16,block+4)==0xA5B0);
  R_WARP_SHIP(f32,self+0x318)=(float)(j*100+109);assert(warp_ship_joint_above(tornado,self+0x318)==(s32)j);checks++;
 }
 // Native BLE rejects only ordered <=, so unordered scale accelerates and
 // unordered speed clamps to50. A modeled unordered joint exits at0, while
 // null-model BLT falls through every joint and returns10.
 reset();R_WARP_SHIP(s16,self+0x662)=2;R_WARP_SHIP(s16,self+0x68E)=0;R_WARP_SHIP(f32,tornado+0x744)=NAN;R_WARP_SHIP(f32,self+0x340)=NAN;R_WARP_SHIP(f32,self+0x318)=-0.0f;chase_answer=1;
 assert(randomizer_ship_warp(self)==1&&R_WARP_SHIP(f32,self+0x340)==50&&R_WARP_SHIP(f32,self+0x318)==50);checks++;
 reset();R_WARP_SHIP(f32,self+0x318)=NAN;assert(warp_ship_joint_above(tornado,self+0x318)==10);
 R_WARP_SHIP(u32,tornado+0x3B4)=model;R_WARP_SHIP(u32,model+0x2C)=block;R_WARP_SHIP(u32,block+0x10)=matrices;R_WARP_SHIP(f32,matrices+0x1C)=1;
 assert(warp_ship_joint_above(tornado,self+0x318)==0);checks++;
 R_WARP_SHIP(f32,self+0x318)=0;R_WARP_SHIP(f32,matrices+0x1C)=-0.0f;R_WARP_SHIP(f32,matrices+0x30+0x1C)=1;assert(warp_ship_joint_above(tornado,self+0x318)==1);checks++;
 printf("ship warp: %u complete flow/layout cases passed\n",checks);return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            c,exe=Path(directory)/'test.c',Path(directory)/'test'
            c.write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
