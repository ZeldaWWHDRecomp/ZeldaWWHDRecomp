from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class OrcaCounter(unittest.TestCase):
    def test_full_battle_consumer(self):
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int16_t s16;typedef uint16_t u16;typedef int8_t s8;typedef uint8_t u8;typedef float f32;typedef double f64;
static _Alignas(8) u8 actor[0x1000],play[0x5D00],player[0x400],save[0x40],table[0x30];static u32 saveptr=(u32)save;static f32 constants[]={0,221,439,32,419,150};static u8 life;
#define WWHD_ADDR_save_info_pointer 1
static void* data(u32 a){switch(a){case 1:return &saveptr;case 0x1001AE10:return constants;case 0x1001B3B8:return constants+1;case 0x1001B3BC:return constants+2;case 0x1001B3C0:return constants+3;case 0x1001B3C4:return constants+4;case 0x1001B194:return constants+5;case 0x101D5F44:return &life;}assert(0);return 0;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#include "orca_counter.h"
#define A(t,o) (*(t*)(actor+(o)))
static unsigned timers,animations,waits,actions,parts,attackparts,maths,getters,attacking,mutate;static u32 expected_this;static double random_value;
u32 roc_game(void){++getters;return (u32)play;}
void roc_counter(s32 mode,u32 limit,u32 type,u32 icon,f32 x,f32 y,f32 width,f32 height){assert(mode==6&&limit==1000&&type==2&&icon==(save[0x2E]==0x38?1:2)&&x==221&&y==439&&width==32&&height==419);++timers;if(mutate)actor[0xEA0]=5;}
void roc_animation(u32 a,s32 n,f32 zero,s32 mode){assert(a==(u32)actor&&n==5&&zero==0&&mode==0);++animations;}
f64 roc_random(f64 range){assert(range==150);return random_value;}
void roc_wait(u32 a){assert(a==(u32)actor);++waits;if(mutate)actor[0xEA0]=7;}
void roc_subtract(u32 a,u32 out,u32 b){assert(a==(u32)player+0x314&&b==(u32)actor+0x314);f32* v=(f32*)out;v[0]=3;v[1]=9;v[2]=4;++maths;}
s32 roc_angle(f64 x,f64 z){assert(x==3&&z==4);++maths;return 12;}
f64 roc_square(u32 p){const f32* v=(const f32*)p;assert(v[0]==3&&v[1]==0&&v[2]==4);++maths;return 25.0000000001;}
f64 roc_sqrt(f64 d){assert(d==25.0000000001);++maths;return 5;}
s32 roc_guard(u32 a){assert(a==(u32)actor);return 0;}
static void action(u32 a){assert(a==expected_this);++actions;}
void roc_particle(u32 a){assert(a==(u32)actor);++parts;}
s32 roc_attacking(u32 a){assert(a==(u32)actor);return attacking;}
void roc_attack_particle(u32 a){assert(a==(u32)actor);++attackparts;}
static void reset(void){memset(actor,0,sizeof actor);timers=animations=waits=actions=parts=attackparts=maths=getters=mutate=attacking=0;*(u32*)(play+0x5B2C)=(u32)player;A(s16,0x3E6)=-1;A(u32,0x3E8)=(u32)action;expected_this=(u32)actor;random_value=29.999999999;life=0xCC;}
int main(void){unsigned cases=0;
for(unsigned sword=0;sword<256;sword++)for(unsigned mode=0;mode<256;mode++){
 reset();save[0x2E]=sword;actor[0xEA0]=mode;A(unsigned int,0x39C)=0xFF;A(unsigned int,0xA9C)=0xFF;assert(randomizer_orca_battle((u32)actor,0x123)==1);
 if(mode==0){assert(timers==(sword!=255)&&animations==1&&waits==1&&!actions&&!maths&&actor[0xEA0]==1&&A(s32,0xE54)==59&&actor[0x38A]==3&&getters==2);}
 else if(mode==255){assert(!timers&&!animations&&!actions&&!maths&&actor[0x38A]==0xB5&&A(unsigned int,0x39C)==0xFB);}
 else {assert(!timers&&!animations&&!waits&&actions==1&&maths==4&&parts==1&&attackparts==1&&life==3&&A(unsigned int,0xA9C)==0xFE);}
 ++cases;
}
reset();save[0x2E]=255;mutate=1;randomizer_orca_battle((u32)actor,0);assert(!timers&&actor[0xEA0]==8);
reset();actor[0xEA0]=1;A(s16,0x3E4)=8;A(s16,0x3E6)=1;A(s16,0x3EA)=0x100;*(u32*)(actor+0x108)=(u32)table;*(u32*)(table+12)=(u32)action;expected_this=(u32)actor+8;randomizer_orca_battle((u32)actor,0);assert(actions==1);
for(unsigned hp=0;hp<256;hp++)for(unsigned suppressed=0;suppressed<2;suppressed++){
 reset();actor[0xEA0]=1;attacking=1;A(unsigned int,0xE60)=hp;A(s32,0xF94)=suppressed;randomizer_orca_battle((u32)actor,0);assert(parts==!suppressed&&!attackparts&&life==(hp<3?3-hp:0));
}
const double randoms[]={NAN,INFINITY,-INFINITY,65535.5,2147483648.,-2147483649.};
const int expected[]={30,29,30,29,29,30};
for(unsigned n=0;n<6;n++){reset();save[0x2E]=0x38;random_value=randoms[n];randomizer_orca_battle((u32)actor,0);assert(A(s32,0xE54)==expected[n]);}
assert(cases==65536);return 0;}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'test.c'
            executable = Path(directory) / 'test'
            source.write_text(code)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(Path(__file__).parent), str(source), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
