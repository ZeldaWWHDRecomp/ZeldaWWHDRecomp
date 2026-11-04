"""Staged complete classic demo-end consumer; family remains guarded."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class ClassicDemoEnd(unittest.TestCase):
    def test_complete_consumer(self):
        code=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef uint16_t u16;typedef int32_t s32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_RANDOMIZER_CLASSIC_MODE 1
static _Alignas(16) unsigned char actor[0x8300],play[0x6000],ship[0x700];
static u32 player_pointer,ship_pointer;
static u32* pointer(u32 address){if(address==(u32)play+0x5B2C)return&player_pointer;assert(address==(u32)play+0x5B3C);return&ship_pointer;}
#define RCDPTR(a,o) (*pointer((a)+(o)))
static unsigned int one=0x3F800000;
#define WWHD_GAME_DATA(a,t) (*(t*)&one)
static u32 wwhd_dComIfG_getGameInfo(void){return(u32)play;}
#include "classic_demo_end.h"
static unsigned action,deletes,mutate,texture_calls,mute_calls;
void rcd_texture(u32 a){assert(a==(u32)actor);texture_calls++;if(mutate){RCD16(a,0x69B0)=0x104;RCD32(a,0x65F0)=201;}}
void rcd_delete_item(u32 a,s32 mode){assert(a==(u32)actor&&!mode);deletes++;RCD32(a,0x3BC)=0xA51234;}
void rcd_mute_off(void){mute_calls++;}
#define ACTION(name,id) s32 name(u32 a){assert(a==(u32)actor&&!action);action=id;return 99;}
ACTION(rcd_change_wait,1) ACTION(rcd_control,2) ACTION(rcd_cannon,3)
ACTION(rcd_crane,4) ACTION(rcd_steer,5) ACTION(rcd_paddle,6) ACTION(rcd_wait,7)
s32 rcd_scope(u32 a,s32 item){assert(item==0x20);assert(a==(u32)actor&&!action);action=8;return 99;}
s32 rcd_next(u32 a,s32 mode){assert(!mode);assert(a==(u32)actor&&!action);action=9;return 99;}
static void reset(void){memset(actor,0,sizeof actor);memset(play,0,sizeof play);memset(ship,0,sizeof ship);action=deletes=mutate=texture_calls=mute_calls=0;player_pointer=(u32)actor;ship_pointer=(u32)ship;RCD32((u32)actor,0x3B8)=0xFFFFFFFF;RCD32((u32)actor,0x3BC)=0xFFFFFFFF;RCD16((u32)actor,0x322)=123;RCD16((u32)actor,0x32A)=456;RCD16((u32)actor,0x69B0)=0x100;RCD32((u32)actor,0x8260)=77;}
int main(void){unsigned cases=0;
for(unsigned proc=0;proc<256;proc++)for(unsigned type=0;type<256;type++){
 reset();RCD32((u32)actor,0x65F0)=proc;RCD16((u32)actor,0x420)=type;RCD32((u32)actor,0x430)=type;
 randomizer_classic_demo_end((u32)actor);assert(action==(proc==201?7:0));assert(texture_calls==1&&mute_calls==1&&!deletes);
 assert(RCD16((u32)actor,0x322)==(proc==104?123:456));assert(RCD32((u32)actor,0x8260)==(type==6||type==0x25?30:77));assert(RCD32((u32)actor,0x3B8)==0xFFEFFFFF&&!RCD16((u32)actor,0x420)&&RCD32((u32)actor,0x434)==one);cases++;
}
for(unsigned proc=0;proc<256;proc++)for(unsigned scope=0;scope<2;scope++){
 reset();RCD32((u32)actor,0x65F0)=proc;RCD32((u32)actor,0x834)=0x20;RCD32((u32)play,0x5CD8)=scope?0x200010:0x10;randomizer_classic_demo_end((u32)actor);unsigned excluded=proc==0x69||proc==0xB2||proc==0xB0;assert(action==(excluded?(proc==201?7:0):scope?8:1));assert(!(RCD32((u32)play,0x5CD8)&0x10));cases++;
}
for(unsigned part=0;part<256;part++)for(unsigned mode=0;mode<3;mode++){
 reset();RCD32((u32)play,0x5CD8)=0x10000;RCD8((u32)ship,0x637)=part;if(mode==0)ship_pointer=0;if(mode==1)player_pointer=0;randomizer_classic_demo_end((u32)actor);assert(action==(mode==0?9:mode==1?2:part==2?3:part==3?4:part==1?5:6));cases++;
}
reset();mutate=1;randomizer_classic_demo_end((u32)actor);assert(deletes==1&&action==7&&RCD32((u32)actor,0x3BC)==(0xA51234u&~0x1000u));cases++;
printf("classic demo end: %u cases passed\n",cases);return 0;}
'''
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory);(p/'test.c').write_text(code)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)
