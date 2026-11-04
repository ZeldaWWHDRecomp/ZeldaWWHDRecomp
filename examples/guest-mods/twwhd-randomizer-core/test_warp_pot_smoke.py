"""Full warp-pot initialization and smoke color selection, with live callbacks."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class WarpPotSmoke(unittest.TestCase):
    def test_full_consumer(self):
        code = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 actor[0xA00],play[0x6000],model[0x100],bg[6][0xC0];
static unsigned int primary[5]={0x01020304,0x11223344,0x55667788,0xAABBCCDD,0xFF00FF80};
static unsigned int environment[5]={0x10203040,0x12345678,0x87654321,0xFEDCBA98,0xFF8000FF};
static void* data(u32 address){if(address==0x100337D4)return primary;assert(address==0x100337E8);return environment;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static struct{u32 key,value;}cells[32];static unsigned nc;
static u32* cell(u32 key){for(unsigned i=0;i<nc;i++)if(cells[i].key==key)return&cells[i].value;assert(nc<32);cells[nc].key=key;cells[nc].value=0;return&cells[nc++].value;}
#define RWPPTR(a,o) (*cell((a)+(o)))
static u32 wwhd_dComIfG_getGameInfo(void);
#include "warp_pot_smoke.h"
static unsigned calls,registers,lid_result,inside_result,mutate;static unsigned char trace[32];static unsigned nt;
static void log_call(unsigned value){assert(nt<32);trace[nt++]=value;}
static u32 wwhd_dComIfG_getGameInfo(void){++calls;log_call(1);if(mutate){if(calls==1)RWPPTR((u32)actor,0x45C)=(u32)bg[4];else RWPPTR((u32)actor,0x3F4)=(u32)bg[5];}return(u32)play;}
u8 rwp_has_lid(u32 a){assert(a==(u32)actor);log_call(2);return lid_result;}
void rwp_mode(u32 a,s32 operation,s32 mode){assert(a==(u32)actor&&!operation&&mode==(lid_result?1:0));log_call(3);if(mutate)RWPPTR(a,0x428)=(u32)bg[3];}
void rwp_initialize_collision(u32 a){assert(a==(u32)actor);log_call(4);assert(RWP32(RWPPTR(a,0x428),0xB0)==0x023B9E88);}
void rwp_set_collision(u32 a){assert(a==(u32)actor);log_call(5);}
void rwp_set_matrix(u32 a){assert(a==(u32)actor&&RWP8(a,0x9BC)==(inside_result?30:5));log_call(6);}
void rwp_register(u32 background,u32 collision,u32 a){assert(background==(u32)play+0x12A0&&a==(u32)actor);assert(collision==RWPPTR(a,registers?0x3F4:(RWPPTR(a,0x45C)?0x45C:0x3F4)));++registers;log_call(7);}
void rwp_wall(u32 circle,f32 height,f32 radius){assert(circle==(u32)actor+0x96C&&height==30&&radius==50);log_call(8);}
void rwp_acch(u32 acch,u32 current,u32 old,u32 a,s32 count,u32 circle,u32 speed,u32 angle,u32 shape){assert(acch==(u32)actor+0x7A8&&current==(u32)actor+0x314&&old==(u32)actor+0x300&&a==(u32)actor&&count==1&&circle==(u32)actor+0x96C&&speed==(u32)actor+0x33C&&!angle&&!shape);log_call(9);if(mutate)RWP32(a,0x7D0)=0xABC00001u;}
s32 rwp_player_in_circle(u32 position,f32 radius,f32 height){assert(radius==50&&height==100&&RWP32(position,0)==0x7FC00123&&RWP32(position,4)==0x80000000&&RWP32(position,8)==0x40400000);log_call(10);RWP8((u32)actor,0x9BC)=99;return inside_result;}
void rwp_cull_box(u32 a,f32 minx,f32 miny,f32 minz,f32 maxx,f32 maxy,f32 maxz){assert(a==(u32)actor&&minx==-80&&miny==0&&signbit(miny)&&minz==-80&&maxx==80&&maxy==160&&maxz==80);log_call(11);if(mutate){RWPPTR(a,0x45C)=(u32)bg[1];RWPPTR(a,0x3F4)=(u32)bg[2];}}
void rwp_move(u32 a,u32 collision){assert(a==(u32)actor&&!collision&&RWPF(a,0x374)==-6.5f);log_call(12);if(mutate){RWP32(a,0x314)=0x7FC00123;RWP32(a,0x318)=0x80000000;RWP32(a,0x31C)=0x40400000;}}
static void reset(void){memset(actor,0,sizeof actor);memset(bg,0,sizeof bg);nc=nt=calls=registers=mutate=0;RWPPTR((u32)actor,0x3F4)=(u32)bg[0];RWPPTR((u32)actor,0x428)=(u32)bg[0];RWP32((u32)actor,0x314)=0x7FC00123;RWP32((u32)actor,0x318)=0x80000000;RWP32((u32)actor,0x31C)=0x40400000;}
int main(void){unsigned cases=0;
for(int event=-128;event<128;event++)for(unsigned index=0;index<5;index++)for(unsigned lid=0;lid<2;lid++)for(unsigned has_model=0;has_model<2;has_model++)for(unsigned inside=0;inside<3;inside++)for(unsigned mode=0;mode<4;mode++){
 reset();lid_result=mode?mode==3?255:mode:0;inside_result=inside;RWPPTR((u32)actor,0x45C)=lid?(u32)bg[1]:0;RWPPTR((u32)actor,0x3EC)=has_model?(u32)model:0;RWP32((u32)actor,0x3D0)=index;RWP32((u32)actor,0x3D4)=event;RWP32((u32)actor,0x7D0)=0x80001000u;randomizer_warp_pot_create_init((u32)actor);unsigned special=event==2||event==5;assert(RWP32((u32)actor,0x9E8)==(special?0xE5101B80u:primary[index]));assert(RWP32((u32)actor,0x9EC)==(special?0x3C379D80u:environment[index]));assert(RWPPTR((u32)actor,0x348)==(has_model?(u32)model+0xC8:0));assert(registers==lid+1&&calls==lid+1&&RWP32((u32)actor,0x7D0)==0x8000140Cu&&RWP32((u32)bg[0],0xA8)==0x024EE658);if(lid)assert(RWP32((u32)bg[1],0xA8)==0x024EE658);assert(trace[0]==11&&trace[nt-1]==6&&trace[nt-2]==10);++cases;
}
/* Native callbacks may replace the registered object after correction is set. */
reset();mutate=1;lid_result=2;inside_result=1;RWP32((u32)actor,0x3D0)=4;RWP32((u32)actor,0x3D4)=5;randomizer_warp_pot_create_init((u32)actor);assert(RWP32((u32)bg[1],0xA8)==0x024EE658&&RWP32((u32)bg[2],0xA8)==0x024EE658&&!RWP32((u32)bg[4],0xA8)&&!RWP32((u32)bg[5],0xA8));assert(RWP32((u32)bg[3],0xB0)==0x023B9E88&&RWP32((u32)actor,0x7D0)==0xABC0040Du);++cases;
printf("Warp pots: %u complete initialization cases passed\n",cases);return 0;}
'''
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'test.c';executable=Path(directory)/'test'
            source.write_text(code)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(source),'-o',str(executable)],check=True)
            subprocess.run([str(executable)],check=True)


if __name__=='__main__':
    unittest.main()
