"""Full audio frame contracts with independent room/queue/timer/restore oracles."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

class OutsetMusicFrame(unittest.TestCase):
    def test_full_audio_frame(self):
        source = r'''
#define ID_processLevObjSE 1
#define ID_cbPracticeProcess 2
#define ID_processDemoFanfareMute 3
#define ID_checkBgmPlaying 4
#define ID_bgmStop 5
#define ID_bgmStart 6
#define ID_checkSubBgmPlaying 7
#define ID_subBgmStart 8
#define ID_subBgmStopInner 9
#define ID_isDemo 10
#define ID_checkSeqIDDemoPlaying 11
#define ID_checkPlayingStreamBgmFlag 12
#define ID_checkSeaBgmID 13
#define ID_sceneBgmStart 14
#define ID_processHeartGaugeSound 15
#define ID_processTime 16
#define ID_changeSeaBgm 17
#define ID_bgmBattleGFrame 18
#define ID_mbossBgmMuteProcess 19
#define ID_enemyNearByGFrame 20
#define ID_stSkyCloistersProcess 21
#define ID_checkStreamPlaying 22
#define ID_startSoundVec 23
#define ID_seStart 24
#define ID_volume 25
#define ID_sqrt 26
#define ID_daytime 27

#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint16_t u16;typedef uint8_t u8;typedef float f32;typedef double f64;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 memory[0xC0000];
#define R_GRANDMA_u32(a) (*(uint32_t*)(memory+(a)))
#define R_GRANDMA_s32(a) (*(int32_t*)(memory+(a)))
#define R_GRANDMA_u16(a) (*(uint16_t*)(memory+(a)))
#define R_GRANDMA_u8(a) (*(uint8_t*)(memory+(a)))
#define R_GRANDMA_f32(a) (*(float*)(memory+(a)))
#define R_GRANDMA(t,a) R_GRANDMA_##t(a)
#define WWHD_GAME_DATA(a,t) (*(t*)(uintptr_t)(0x80000u+((a)&0x1FFFFu)))
#define GRANDMA_ISLAND_TABLE 0x80000u
static const u32 self=0x10000,info=0x20000,camera=0x30000,position=0x31000,sound=0x40000;
static s32 chest,daytime,demo,playing,seqplaying,streamplaying;static unsigned mutate,chest_calls,expand_calls,bgm_count,sub_count,volume_count;
static u32 last_bgm,last_keep,last_sub,last_sound,last_fade;static f32 last_volume;
static unsigned trace[128],n;
static void record(unsigned id,u32 a,u32 b){(void)a;(void)b;assert(n<128);trace[n++]=id;}
static u32 wwhd_dComIfG_getGameInfo(void){record(100,0,0);return info;}
static s32 grandma_outset_bgm(void){chest_calls++;if(mutate)R_GRANDMA(u8,self+0x2A8)=3;return chest;}
static u32 grandma_expand_bgm(u32 actor,u32 item){assert(actor==self);expand_calls++;return 0xA0000000u|item;}
static s32 grandma_check_event(u32 actor,u32 flag){assert(actor==self&&flag==1);return 0;}
#include "outset_music_frame.h"
void frame_processLevObjSE(u32 p0){record(1,0,0);(void)p0;if(mutate){R_GRANDMA(u8,self+0x26A)=9;R_GRANDMA(u32,self+0x1FA4)=0xBEEF;}}
void frame_cbPracticeProcess(u32 p0){record(2,0,0);(void)p0;if(mutate){assert(R_GRANDMA(u32,self+0x1FA4)==0xBEEF);R_GRANDMA(u8,self+0x26A)=7;}}
void frame_processDemoFanfareMute(u32 p0){record(3,0,0);(void)p0;assert(R_GRANDMA(u32,self+0x1FA4)==0&&R_GRANDMA(u8,self+0x269)==0);}
s32 frame_checkBgmPlaying(u32 p0){record(4,0,0);(void)p0;return playing;}
void frame_bgmStop(u32 p0,u32 p1,u32 p2){record(5,p1,p2);(void)p0;(void)p1;(void)p2;}
void frame_bgmStart(u32 p0,u32 p1,u32 p2,u32 p3){record(6,p1,p2);(void)p0;(void)p1;(void)p2;(void)p3;bgm_count++;last_bgm=p1;last_keep=p3;}
s32 frame_checkSubBgmPlaying(u32 p0){record(7,0,0);(void)p0;return playing;}
void frame_subBgmStart(u32 p0,u32 p1){record(8,p1,0);(void)p0;(void)p1;sub_count++;last_sub=p1;}
void frame_subBgmStopInner(u32 p0){record(9,0,0);(void)p0;}
s32 frame_isDemo(u32 p0){record(10,0,0);(void)p0;return demo;}
s32 frame_checkSeqIDDemoPlaying(u32 p0,u32 p1){record(11,p1,0);(void)p0;(void)p1;return seqplaying;}
s32 frame_checkPlayingStreamBgmFlag(u32 p0,u32 p1){record(12,p1,0);(void)p0;(void)p1;return streamplaying;}
u32 frame_checkSeaBgmID(u32 p0){record(13,0,0);(void)p0;return 0x8000002E;}
void frame_sceneBgmStart(u32 p0){record(14,0,0);(void)p0;}
void frame_processHeartGaugeSound(u32 p0){record(15,0,0);(void)p0;}
void frame_processTime(u32 p0){record(16,0,0);(void)p0;}
void frame_changeSeaBgm(u32 p0){record(17,0,0);(void)p0;}
void frame_bgmBattleGFrame(u32 p0){record(18,0,0);(void)p0;}
void frame_mbossBgmMuteProcess(u32 p0){record(19,0,0);(void)p0;}
void frame_enemyNearByGFrame(u32 p0){record(20,0,0);(void)p0;}
void frame_stSkyCloistersProcess(u32 p0){record(21,0,0);(void)p0;}
s32 frame_checkStreamPlaying(u32 p0,u32 p1){record(22,p1,0);(void)p0;(void)p1;return streamplaying;}
void frame_startSoundVec(u32 p0,u32 p1,u32 p2,u32 p3,u32 p4,u32 p5,u32 p6){record(23,p1,p2);(void)p0;(void)p1;(void)p2;(void)p3;(void)p4;(void)p5;(void)p6;R_GRANDMA(u32,p2)=sound;}
u32 frame_seStart(u32 p0,u32 p1,u32 p2,u32 p3,s32 p4,f32 p5,f32 p6,f32 p7,f32 p8,u32 p9){record(24,p1,p2);(void)p0;(void)p1;(void)p2;(void)p3;(void)p4;(void)p5;(void)p6;(void)p7;(void)p8;(void)p9;return 0;}
void frame_volume(u32 p0,f32 p1,u32 p2,u32 p3){record(25,0,p2);(void)p0;(void)p1;(void)p2;(void)p3;volume_count++;last_volume=p1;last_sound=p0;last_fade=p2;}
f64 frame_sqrt(f64 p0){record(26,0,0);(void)p0;return sqrt(p0);}
s32 frame_daytime(u32 p0){record(27,0,0);(void)p0;return daytime;}
static void reset(void){
 memset(memory,0,sizeof memory);n=mutate=chest_calls=expand_calls=bgm_count=sub_count=volume_count=0;chest=0;daytime=1;demo=playing=seqplaying=streamplaying=0;
 R_GRANDMA(u32,self)=camera;R_GRANDMA(u32,camera)=position;R_GRANDMA(u32,self+0xC0)=0xFFFFFFFF;
 for(u32 off=0x90;off<=0xAC;off+=4)R_GRANDMA(f32,self+off)=1.0f;
 R_GRANDMA(f32,self+0xBC)=1;R_GRANDMA(u32,self+0x20D0)=1;R_GRANDMA(u16,self+0x20D4)=0x1234;R_GRANDMA(u16,self+0x20D6)=0xABCD;
 for(u32 i=0;i<256;i++)R_GRANDMA(u16,GRANDMA_ISLAND_TABLE+i*4)=0x100+i;
}
static void checked_frame(void){
 u8 actor_before[0x2100],save_before[0x7000],allowed[0x2100]={0};
 const unsigned words[]={0x38,0x1FA4,0x1F08,0xC0,0xA0,0xBC,0x1FA8};
 const unsigned bytes[]={0x269,0x26A,0x26B,0x26C,0x26D,0x26E,0x26F,0x270,0x272,0x273,0x277,0x20AD,0x20AC,0x71,0xDE,0x276,0xCB,0x75,0xCC,0x28E,0x2A8,0x20D4,0x20D5};
 for(unsigned i=0;i<sizeof words/sizeof *words;i++)memset(allowed+words[i],1,4);
 for(unsigned i=0;i<sizeof bytes/sizeof *bytes;i++)allowed[bytes[i]]=1;
 memcpy(actor_before,memory+self,sizeof actor_before);memcpy(save_before,memory+info,sizeof save_before);
 randomizer_music_frame(self);
 assert(!memcmp(save_before,memory+info,sizeof save_before));
 for(unsigned i=0;i<sizeof actor_before;i++)assert(allowed[i]||actor_before[i]==memory[self+i]);
}
int main(void){unsigned cases=0;
 // Full room register domain, strict chest answer, daytime early-return, retained room across query.
 for(unsigned room=0;room<256;room++)for(s32 flag=-1;flag<=2;flag++)for(s32 day=-1;day<=1;day++)for(unsigned change=0;change<2;change++){
  reset();chest=flag;daytime=day;mutate=change;R_GRANDMA(u8,self+0x2A8)=room;
  randomizer_start_island_bgm(self);
  assert(bgm_count==(day!=0));assert(chest_calls==(day!=0&&room==44));
  if(day){assert(last_bgm==(room==44&&flag==1?0x80000055u:(0xA0000100u|room)));assert(last_keep==1);}
  cases++;
 }
 // Queued scene transition retains room and clears exactly the original queue fields.
 for(unsigned room=0;room<256;room++)for(unsigned queued=0;queued<6;queued++)for(s32 flag=-1;flag<=2;flag++){
  reset();chest=flag;mutate=1;R_GRANDMA(u8,self+0x2A8)=room;R_GRANDMA(u8,self+0xCC)=queued;R_GRANDMA(u8,self+0x28E)=0x77;
  R_GRANDMA(u32,self+0x38)=0xFFFFFFFF;R_GRANDMA(u8,self+0x29D)=1;
  checked_frame();
  assert(R_GRANDMA(u32,self+0x38)==0xFFFFFFFF&&R_GRANDMA(u8,self+0x26A)==6);
  assert(R_GRANDMA(u8,self+0xCC)==0);
  assert(R_GRANDMA(u8,self+0x28E)==(queued==1||queued==2?0:0x77));
  assert(chest_calls==(queued==3&&room==44));
  assert(bgm_count==(queued==1||queued==3));
  if(queued==3){assert(last_bgm==(room==44&&flag==1?0x80000055u:0xA0000100u|room));assert(last_keep==0);}
  unsigned tail[]={ID_processTime,ID_changeSeaBgm,ID_bgmBattleGFrame,ID_mbossBgmMuteProcess,ID_enemyNearByGFrame,ID_stSkyCloistersProcess,ID_processHeartGaugeSound};
  assert(n>=7&&!memcmp(trace+n-7,tail,sizeof tail));
  cases++;
 }
 // Saturation/reset, all timer bytes and exact final halfword write.
 for(unsigned initial=0;initial<256;initial++)for(unsigned running=0;running<2;running++){
  reset();R_GRANDMA(u8,info+0x5292)=running;R_GRANDMA(u32,self+0x38)=initial;R_GRANDMA(u8,self+0x29D)=initial&1;
  unsigned timers[]={0x26A,0x272,0x273,0x277,0x26D,0x26E,0x26F,0x270,0x71,0xDE};
  for(unsigned i=0;i<10;i++)R_GRANDMA(u8,self+timers[i])=initial;
  R_GRANDMA(u32,self+0x20D0)=0;checked_frame();
  for(unsigned i=0;i<10;i++)assert(R_GRANDMA(u8,self+timers[i])==(i==8&&running?5:initial?initial-1:0));
  assert(R_GRANDMA(u32,self+0x38)==((initial&1)?initial+1:0));
  assert(R_GRANDMA(u16,self+0x20D4)==0xFFFF&&R_GRANDMA(u16,self+0x20D6)==0xABCD);cases++;
 }
 // Native sub-BGM restore set, all u8 subtype values and exact strict playing result.
 for(unsigned number=0;number<0x70;number++)for(unsigned subtype=0;subtype<256;subtype++){
  reset();const u32 id=0x80000000u+number;R_GRANDMA(u32,self+0x84)=id;R_GRANDMA(f32,self+0xB4)=1;R_GRANDMA(u8,self+0xCB)=1;R_GRANDMA(u8,self+0xC6)=subtype;
  checked_frame();
  const int restore=number==2||(number>=0x24&&number<=0x25)||number==0x27||(number>=0x30&&number<=0x32)||number==0x4F||number==0x5D||number==0x61;
  assert(sub_count==(restore&&subtype>=1&&subtype<=3));assert(R_GRANDMA(u8,self+0xCB)==!restore);
  if(sub_count)assert(last_sub==(subtype==1?0x8000001Au:subtype==2?0x80000046u:0x80000047u));cases++;
 }
 // Camera ambience thresholds and rounded main-volume chain.
 const float heights[]={-1,1799,1800,2399,2400,2799,3000,3199,3200,5000,9000,10000,11000};
 for(unsigned i=0;i<sizeof heights/sizeof *heights;i++){
  reset();R_GRANDMA(s32,self+0x294)=0x19;R_GRANDMA(f32,position+4)=heights[i];R_GRANDMA(u32,self+0x78)=sound;
  checked_frame();float expected=heights[i]<2500?1:heights[i]<5000?1-(heights[i]-2500)/2500:0;
  assert(volume_count==1&&last_sound==sound&&last_fade==0&&last_volume==expected);cases++;
 }
 // Every scene and special island, with finite/nonfinite camera positions,
 // sound handles present/absent and independent demo/run state.
 const unsigned rooms[]={0,11,13,41,44};
 const float positions[]={-INFINITY,-1,0,1800,2400,3000,3200,5000,6500,8500,9000,11000,198000,INFINITY,NAN};
 for(s32 scene=-1;scene<0x7A;scene++)for(unsigned room=0;room<5;room++)for(unsigned i=0;i<sizeof positions/sizeof *positions;i++)for(unsigned handle=0;handle<2;handle++){
  reset();R_GRANDMA(s32,self+0x294)=scene;R_GRANDMA(u8,self+0x2A8)=rooms[room];R_GRANDMA(u8,self+0x29D)=1;
  R_GRANDMA(f32,position)=positions[i];R_GRANDMA(f32,position+4)=positions[i];R_GRANDMA(f32,position+8)=positions[i];
  R_GRANDMA(u32,self+0x78)=handle?sound:0;demo=handle;R_GRANDMA(u8,info+0x5292)=handle;
  checked_frame();assert(R_GRANDMA(u16,self+0x20D6)==0xABCD);cases++;
 }
 printf("Outset full frame: %u cases passed\n",cases);return 0;
}

'''
        with tempfile.TemporaryDirectory() as directory:
            c, exe = Path(directory)/'test.c', Path(directory)/'test'
            c.write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
