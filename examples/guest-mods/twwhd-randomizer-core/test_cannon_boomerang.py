from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class CannonBoomerang(unittest.TestCase):
    def test_hit_lifecycle(self):
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int8_t s8;typedef uint8_t u8;typedef uint16_t u16;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(8) u8 actor[0x700],play[0x5C00],hit[0x30],player[0x400];static u8 debug;static f32 two=2;
static void* data(unsigned a){if(a==0x10469465)return &debug;assert(a==0x10026BA0);return &two;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
#include "cannon_boomerang.h"
static unsigned targets,effects,sounds,modes,mode,mutate,timer_busy,has_hit,initial_health;static u32 kind;
void rcb_status(u32 a){assert(a==(u32)actor+0x53C);}
void rcb_mode(u32 a,s32 p,s32 m){assert(a==(u32)actor&&p==0);++modes;mode=m;}
s32 rcb_timer(u32 a){assert(a==(u32)actor+0x55C);return timer_busy;}
u32 rcb_target(u32 a){assert(a==(u32)actor+0x3F4);++targets;if(targets==2){memset(actor+0x4C0,0,12);if(mutate==3)*(u32*)(hit+0x10)=0x40;}return has_hit?(u32)hit:0;}
s32 rcb_reverb(s32 room){assert(room==-3);return 99;}
void rcb_sound(u32 id,u32 pos,u32 param,s32 reverb){assert(pos==(u32)actor+0x37C&&reverb==99);++sounds;if(id==0x2833){assert(param==0x20);if(mutate==1)actor[0x3A1]=3;}else assert(id==0x2828&&!param);}
u32 rcb_game(void){return (u32)play;}
u32 rcb_particle(u32 control,s32 group,u32 id,u32 pos,u32 angle,u32 scale,s32 alpha,u32 cb,s32 setup,u32 color,u32 env,u32 scale2){
 assert(control==123&&!group&&alpha==255&&!cb&&setup==-1&&!color&&!env&&!scale2);
 const unsigned int* p=(const unsigned int*)pos;assert(p[0]==0x11111111&&p[1]==0x22222222&&p[2]==0x33333333);++effects;
 if(id==0x10){assert(!angle&&!scale);unsigned before=kind==0x40&&mutate==1?3:initial_health;assert(actor[0x3A1]==(u8)(before-2));if(mutate==2)actor[0x3A1]=1;}
 else {assert(id==15&&angle==(u32)player+0x328&&scale);const f32* v=(const f32*)scale;assert(v[0]==2&&v[1]==2&&v[2]==2);}return 0;
}
int main(void){const unsigned types[]={0,0x20,0x40,0x60};unsigned cases=0;
for(initial_health=0;initial_health<256;initial_health++)for(unsigned t=0;t<4;t++)for(mutate=0;mutate<4;mutate++)for(timer_busy=0;timer_busy<2;timer_busy++)for(debug=0;debug<2;debug++)for(has_hit=0;has_hit<2;has_hit++){
 memset(actor,0,sizeof actor);memset(hit,0,sizeof hit);targets=effects=sounds=modes=0;actor[0x326]=(u8)-3;actor[0x3A1]=initial_health;*(unsigned int*)(actor+0x4C0)=0x11111111;*(unsigned int*)(actor+0x4C4)=0x22222222;*(unsigned int*)(actor+0x4C8)=0x33333333;*(u32*)(hit+0x10)=types[t];*(u32*)(play+0x5AB0)=123;*(u32*)(play+0x5B2C)=(u32)player;kind=mutate==3?0x40:types[t];
 s32 result=randomizer_cannon_hit((u32)actor);
 if(debug){assert(result==1&&actor[0x3A1]==0&&mode==2&&modes==1&&!targets&&!effects&&!sounds);}
 else if(timer_busy){assert(!result&&!targets&&!effects&&!modes&&!sounds);}
 else if(!has_hit||(kind!=0x20&&kind!=0x40)){assert(!result&&targets==2&&!effects&&!modes&&!sounds);}
 else {unsigned before=kind==0x40&&mutate==1?3:initial_health;u8 remaining=mutate==2?1:(u8)(before-2);unsigned dies=(s8)remaining<=0;assert(result==1&&targets==2&&effects==1+dies&&modes==1&&mode==(dies?2:0)&&sounds==(kind==0x40)+dies&&*(s32*)(actor+0x55C)==5&&actor[0x3A1]==remaining);if(kind==0x40)assert(*(u16*)(actor+0x586)==0x190);}
 ++cases;
}assert(cases==32768);return 0;}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'test.c'
            executable = Path(directory) / 'test'
            source.write_text(code)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(Path(__file__).parent), str(source), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
