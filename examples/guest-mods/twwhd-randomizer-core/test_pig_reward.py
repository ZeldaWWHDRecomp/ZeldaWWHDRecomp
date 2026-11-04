"""Buried reward uses async IDs and preserves request/switch lifecycle."""
from pathlib import Path
import subprocess,tempfile,unittest
class PigReward(unittest.TestCase):
 def test_lifecycle(self):
  code=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int16_t s16;typedef int8_t s8;typedef uint8_t u8;typedef uint16_t u16;typedef float f32;typedef double f64;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static u8 actor[0x500],pig[0x400],save[0x100];static unsigned mask,item,swcalls,sets,deletes,randoms,spawns,valid;
static unsigned int scale[3]={0x3F800000,0x40000000,0x40400000};static s16 angles[3]={11,22,33};static float ranges[4]={32768,4,10,-2};
static void* data(u32 a){if(a>=0x101FFBA8&&a<=0x101FFBB0)return(u8*)scale+a-0x101FFBA8;if(a>=0x101FFB14&&a<=0x101FFB18)return(u8*)angles+a-0x101FFB14;assert(a>=0x1004BCC4&&a<=0x1004BCD0);return(u8*)ranges+a-0x1004BCC4;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static u32 randomizer_daily_save(void){return(u32)save;}
static u32 wwhd_fopAcM_createItem(u32 p,u32 i,u32 bit,u32 room,u32 type,u32 angle,u32 action,u32 s){
 spawns++;assert(p==(u32)pig+0x314&&i==item&&bit==0x1234&&room==(u32)(s32)-7&&type==3&&action==5);s16* a=(s16*)angle;assert(a[0]==11&&a[1]==123&&a[2]==33);assert(!memcmp((void*)s,scale,12));
 if(mask&32)*(unsigned*)(actor+0x3C0)=255;
 return(mask&64)?0xFFFFFFFF:0x123; /* Both are IDs, neither is an actor pointer. */
}
#include "pig_reward.h"
s32 randomizer_pig_switch(u32 s,u32 bit,s32 room){swcalls++;assert(s==(u32)save+0x20&&bit==7&&room==-9);return(mask&1)?2:0;}
void randomizer_pig_set_switch(u32 s,u32 bit,s32 room){assert(s==(u32)save+0x20&&room==-9);assert(bit==((spawns&&(mask&32))?255:7));sets++;}
u32 randomizer_pig_is_actor(u32 p){assert(p==(u32)pig);return valid;}
void randomizer_pig_delete(u32 a){assert(a==(u32)actor);deletes++;}
f64 randomizer_pig_random(f32 range){randoms++;if(randoms==1){assert(range==32768);return 123.75;}assert(range==4&&randoms==2);return 3.125;}
f64 randomizer_pig_random_signed(f32 range){assert(range==4&&randoms==2);randoms++;return -0.1234567890123;}
int main(void){for(mask=0;mask<128;mask++)for(item=0;item<256;item++){
 memset(actor,0,sizeof actor);memset(pig,0x5A,sizeof pig);*(unsigned*)(actor+0x3C0)=7;actor[0x2FE]=(u8)-9;actor[0x326]=(u8)-7;actor[0x3B5]=(mask&2)?0:1;actor[0x3B4]=(mask&4)?1:0;actor[0x3BC]=item;actor[0x3BD]=(mask&8)?42:255;*(u32*)(actor+0x3C4)=(u32)pig;*(unsigned*)(actor+0x3B8)=0x1234;*(s16*)(pig+8)=220;valid=(mask&16)?0:1;
 swcalls=sets=deletes=randoms=spawns=0;randomizer_pig_dig((u32)actor);assert(swcalls==1);
 const unsigned early=(mask&7)||!valid;
 if(early){assert(!sets&&!deletes&&!randoms&&!spawns);continue;}
 assert(spawns==(item!=255)&&randoms==(item!=255?3:0));assert(actor[0x3B5]==0);assert(actor[0x3B4]==((item!=255||(mask&8))?1:0));
 assert(deletes==((spawns&&(mask&32))?1:0)+!!(mask&8));assert(sets==(spawns&&!(mask&32)?1:0)+!!(mask&8));
 if(mask&8)assert(*(u16*)(pig+0x2FC)==42);assert(*(unsigned*)(pig+0x2E0)==0x5A5A5A5A);
 }
 assert(randomizer_pig_integer(1.0/0.0)==0x7FFFFFFF);assert(randomizer_pig_integer(-1.0/0.0)==(s32)0x80000000);assert(randomizer_pig_integer(0.0/0.0)==(s32)0x80000000);assert(randomizer_pig_integer(-123.9)==-123);
 return 0;}
'''
  with tempfile.TemporaryDirectory() as t:
   p=Path(t);(p/'test.c').write_text(code)
   subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
