"""Classic exits retain scene gates and native precision around the death check."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class ClassicFallExit(unittest.TestCase):
    def test_complete_consumer(self):
        code=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef int8_t s8;typedef uint16_t u16;typedef int16_t s16;typedef int32_t s32;typedef float f32;typedef double f64;
#define WWHD_RANDOMIZER_CLASSIC_MODE 1
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define CFL_STAGE_EQUALS() 0
static _Alignas(16) u8 actor[0x7000],play[0x6000],save[0x900];static u32 null_ship,save_pointer=(u32)save;
#define CFLPTR(a,o) null_ship
#define WWHD_GAME_DATA(a,t) (*(t*)&save_pointer)
static u32 wwhd_dComIfG_getGameInfo(void){return(u32)play;}
#include "classic_fall_exit.h"
static unsigned exits,rooms,scenes,restarts,changes,sounds,events,sets,compulsories,mutation;
static s32 exit_id,accepted;static f32 observed_speed;static s32 observed_type,observed_room;static f64 distance;
s32 cfl_restart(u32 self,s32 mode,s32 wipe,f32 speed,s32 arg){assert(self==(u32)actor&&mode==5&&wipe==201&&speed==-1&&!arg);restarts++;CFL32(self,0x834)=0xABC00021;return 1;}
s32 cfl_exit(u32 bg,u32 g){assert(bg==(u32)play+0x12A0&&g==(u32)actor+0x8F4);exits++;if(mutation)CFL32((u32)actor,0x834)=0x20;return exit_id;}
s32 cfl_room(u32 bg,u32 g){assert(bg==(u32)play+0x12A0&&g==(u32)actor+0x8F4);rooms++;return 0xFE;}
s32 cfl_compulsory(u32 e,u32 self,u32 zero,u32 flags){assert(e==(u32)play+0x51D0&&self==(u32)actor&&!zero&&flags==65535);compulsories++;return 1;}
s32 cfl_scene(u32 ground,f32 speed,s32 type,s32 room){assert(ground==(u32)actor+0x8F4);scenes++;observed_speed=speed;observed_type=type;observed_room=room;return accepted;}
s32 cfl_event(u32 s,u32 flag){assert(s==(u32)save+0x644&&flag==0x3E10);events++;return 1;}
void cfl_event_set(u32 s,u32 flag){assert(s==(u32)save+0x644&&flag==0x3E01);sets++;}
void cfl_change_player(u32 a,u32 b){assert(a==(u32)actor&&a==b);changes++;}
void cfl_sound(u32 id){assert(id==0x288B);sounds++;}
void cfl_sub(u32 a,u32 out,u32 b){assert(a==(u32)actor+0x314&&b==(u32)actor+0x300);CFLF(out,0)=3;CFLF(out,4)=0;CFLF(out,8)=4;}
f64 cfl_square(u32 v){assert(CFLF(v,0)==3&&CFLF(v,8)==4);return 25;}
f64 cfl_sqrt(f64 v){assert(v==25);return distance;}
s16 cfl_atan(f64 x,f64 z){assert(x==3&&z==4);return 789;}
static void reset(void){memset(actor,0,sizeof actor);memset(play,0,sizeof play);exits=rooms=scenes=restarts=changes=sounds=events=sets=compulsories=mutation=0;null_ship=0;exit_id=63;accepted=0;distance=5;CFLF((u32)actor,0x8A0)=0;CFL16((u32)actor,0x322)=123;}
int main(void){unsigned cases=0;
/* Every life halfword reaches the native polygon queries, including zero. */
for(unsigned life=0;life<65536;life++){reset();CFL16((u32)play,0x5BAC)=life;randomizer_classic_fall_exit((u32)actor);assert(exits==1&&rooms==1&&!scenes&&!restarts);cases++;}
const f32 speeds[]={-INFINITY,-1,-0.0f,1.4999999f,1.5f,17,17.000002f,100,INFINITY,NAN};
for(unsigned i=0;i<10;i++)for(unsigned success=0;success<2;success++)for(unsigned crawl=0;crawl<3;crawl++){
 reset();exit_id=1;accepted=success;CFL32((u32)actor,0x834)=0x20;CFL16((u32)actor,0xF8)=3;CFLF((u32)actor,0x370)=speeds[i];if(crawl){CFL32((u32)actor,0x6A70)=0x1000000;CFLF((u32)actor,0x5898)=crawl==1?-1:1;}
 randomizer_classic_fall_exit((u32)actor);f32 expected=speeds[i]<1.5f?1.5f:(17.0f-speeds[i])>=0?speeds[i]:17;
 assert(scenes==1&&observed_speed==expected&&observed_type==(crawl==0?0:crawl==1?3:2)&&observed_room==-2&&changes==success&&sets==success&&!compulsories);cases++;
}
for(unsigned reason=0;reason<3;reason++){reset();CFL32((u32)actor,0x69D0)=reason==0?4:0;if(reason==1)CFLF((u32)actor,0x8A0)=-1000000000.0f;CFL32((u32)actor,0x834)=0x20;if(reason==2){CFL32((u32)actor,0x3B8)=0x10;CFL32((u32)actor,0x6A70)=0x40000;}randomizer_classic_fall_exit((u32)actor);assert(restarts==1&&!exits);if(reason!=2)assert(CFL32((u32)actor,0x834)==0xABC00001);cases++;}
const f64 lengths[]={0,0.1,(f64)0.1f,0.10000001,NAN,INFINITY};
for(unsigned i=0;i<6;i++){reset();exit_id=0;accepted=1;mutation=1;distance=lengths[i];randomizer_classic_fall_exit((u32)actor);assert(scenes==1&&compulsories==1&&changes==1&&CFL32((u32)actor,0x430)==2);assert(CFL16((u32)actor,0x422)==(lengths[i]>(f64)0.1f?789:123));cases++;}
printf("classic fall exit: %u cases passed\n",cases);return 0;}
'''
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory);(p/'test.c').write_text(code)
            subprocess.run(['clang','-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)
