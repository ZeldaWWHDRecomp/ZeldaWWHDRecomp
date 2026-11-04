"""Pond switch gating, fivefold speeds, callback timing and frame transition."""
import os
from pathlib import Path
import subprocess,tempfile,unittest
class DrcPondSpeed(unittest.TestCase):
 def test_complete_consumers(self):
  source=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int16_t s16;typedef int8_t s8;typedef float f32;
static _Alignas(uintptr_t) unsigned char actor[0x560],before[0x560];
static f32 frame=299,maximum=510,zero=0,amplitude=-10,speed=5;static u32 save;
static void* data(u32 a){if(a==1)return &save;if(a==0x10029A24)return &frame;if(a==0x10029A20)return &maximum;if(a==0x100299D8)return &zero;if(a==0x10029A28)return &amplitude;if(a==0x10029A38)return &speed;assert(0);return 0;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define R_POND(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)(a))
#include "drc_pond_speed.h"
#define A(t,o) R_POND(t,(u32)actor+(o))
static s32 result,switch_index,room,frame_result;static unsigned calls,particles,mutation;
s32 randomizer_pond_switch(u32 s,s32 i,s32 r){assert(s==save+0x20&&i==switch_index&&r==room);calls++;if(mutation){amplitude=-20;speed=7;A(s32,0x51C)=255;}return result;}
void randomizer_pond_particles(u32 a){assert(a==(u32)actor);particles++;assert(A(f32,0x540)==-0.5263158f&&A(f32,0x534)==0.5263158f&&A(f32,0x490)==speed&&A(f32,0x41C)==speed&&A(f32,0x530)==speed);assert(A(f32,0x53C)==amplitude);if(mutation){A(s32,0x528)=17;A(s32,0x548)=19;A(s16,0x522)=7;}}
s32 randomizer_pond_frame(u32 c,f32 f){assert(c==(u32)actor+0x41C&&f==299);calls++;if(mutation){frame=123;maximum=777;zero=-1;A(f32,0x494)=456;}return frame_result;}
int main(void){unsigned cases=0;save=0x34560000;
for(switch_index=-1;switch_index<=256;switch_index++)for(room=-128;room<128;room++)for(unsigned v=0;v<4;v++)for(mutation=0;mutation<2;mutation++){
 result=v==0?0:v==1?1:v==2?-1:2;amplitude=-10;speed=5;memset(actor,0xA5,sizeof actor);A(s32,0x51C)=switch_index;A(s8,0x2FE)=room;memcpy(before,actor,sizeof actor);calls=particles=0;randomizer_pond_wait((u32)actor);assert(calls==(switch_index!=255));unsigned active=switch_index!=255&&result==1;assert(particles==active);if(active){assert(A(s32,0x528)==0&&A(s32,0x548)==1&&A(s32,0x54C)==62&&A(s16,0x520)==0&&A(s16,0x522)==-1&&A(u32,0x524)==0x0234D0F0);}else if(!mutation||switch_index==255)assert(!memcmp(actor,before,sizeof actor));cases++;}
for(unsigned value=0;value<4;value++)for(mutation=0;mutation<2;mutation++){
 frame_result=value==0?0:value==1?1:value==2?-1:2;frame=299;maximum=510;zero=0;memset(actor,0xA5,sizeof actor);memcpy(before,actor,sizeof actor);calls=0;randomizer_pond_level_wait((u32)actor);assert(calls==1);if(frame_result){assert(A(f32,0x494)==299&&A(f32,0x530)==maximum&&A(f32,0x490)==zero&&A(f32,0x534)==8.388704f&&A(s16,0x520)==0&&A(s16,0x522)==-1&&A(u32,0x524)==0x0234D1EC);}else if(!mutation)assert(!memcmp(actor,before,sizeof actor));cases++;}
printf("DRC pond speeds: %u cases passed\n",cases);}
'''
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory);(root/'test.c').write_text(source)
   subprocess.run([os.environ.get("CC", "cc"),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(root/'test.c'),'-o',str(root/'test')],check=True)
   subprocess.run([str(root/'test')],check=True)
if __name__=='__main__':unittest.main()
