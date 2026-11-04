from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class RainbowRupee(unittest.TestCase):
    def test_full_animation_entry(self):
        code = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <math.h>
typedef uintptr_t u32;typedef int8_t s8;typedef uint8_t u8;typedef uint16_t u16;typedef float f32;
static _Alignas(8) u8 arena[0x4000],resources[36*256];static const u32 actor=0x30000000,model=0x30001000,animations=0x30002000;
static void* mem(u32 a){if(a>=actor&&a<actor+sizeof arena)return arena+a-actor;if(a>0xFFFFFFFFu)return(void*)a;assert(0);return 0;}
#define W(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)mem(a))
#define RRAIN(t,a) W(t,a)
#define WWHD_GAME_DATA(a,t) (*(assert((a)==0x101E4674),(t*)resources))
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#include "rainbow_rupee.h"
static unsigned calls,mask,color,mutate;static float initial;static unsigned seen[5];
static void record(unsigned kind,u32 animation,u32 data,float frame){
 unsigned slot=(animation-animations)/16;assert(slot<5&&animation==animations+slot*16);
 assert(kind==(slot==0||slot==2?0:slot==4?2:1));
 assert(data==(mutate&&calls?0xBB:0xAA));
 float expected;
 if(slot==0){if(color==255)expected=10;else if(color==7){expected=(float)(initial+0.15f);if(!(expected<6))expected=-6;expected=fabsf(expected);}else expected=(s8)color;}
 else expected=10+slot;
 assert((isnan(frame)&&isnan(expected))||memcmp(&frame,&expected,4)==0);
 seen[calls++]=slot;
 if(mutate&&slot==0){W(u32,actor+0x3C0)=0;W(u32,actor+0x3B4)=model+0x100;}
}
void rrain_brk(u32 animation,u32 data,float frame){record(0,animation,data,frame);}
void rrain_btk(u32 animation,u32 data,float frame){record(1,animation,data,frame);}
void rrain_bck(u32 animation,u32 data,float frame){record(2,animation,data,frame);}
static void reset(void){
 memset(arena,0,sizeof arena);W(u8,actor+0x74E)=0xB8;resources[0xB8*36+0x14]=color;W(u32,actor+0x3B4)=model;W(u32,model+0xAC)=0xAA;W(u32,model+0x1AC)=0xBB;W(float,actor+0x3A0)=initial;calls=0;
 const unsigned offsets[]={0x3C8,0x3C0,0x3CC,0x3C4,0x3D0};
 for(unsigned n=0;n<5;n++){W(u32,actor+offsets[n])=(mask&(1u<<n))?animations+n*16:0;W(float,animations+n*16+4)=10+n;}
}
int main(void){const float values[]={0,-0.0f,-6,5.85f,5.9f,NAN,INFINITY,-INFINITY};unsigned cases=0;
for(color=0;color<256;color++)for(mask=0;mask<32;mask++)for(unsigned f=0;f<8;f++){
 initial=values[f];mutate=0;reset();randomizer_item_animation_entry(actor);
 unsigned expected=0;for(unsigned slot=0;slot<5;slot++)if(mask&(1u<<slot)){assert(seen[expected]==slot);++expected;}assert(calls==expected);
 float after=initial;if((mask&1)&&color==7){after=(float)(initial+0.15f);if(!(after<6))after=-6;}
 float actual=W(float,actor+0x3A0);assert((isnan(after)&&isnan(actual))||memcmp(&after,&actual,4)==0);++cases;
}
color=7;mask=31;initial=0;mutate=1;reset();randomizer_item_animation_entry(actor);assert(calls==4&&seen[0]==0&&seen[1]==2&&seen[2]==3&&seen[3]==4);
assert(cases==65536);return 0;}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'test.c'
            executable = Path(directory) / 'test'
            source.write_text(code)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(Path(__file__).parent), str(source), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
