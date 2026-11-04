"""Poe/Jalhalla death registration and light-counter cancellation matrix."""
from pathlib import Path
import os,subprocess,tempfile,unittest
class Poe(unittest.TestCase):
 def test_consumer(self):
  code=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef int8_t s8;typedef int16_t s16;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#include "poe_light_arrow.h"
static u8 actor[0xB00],boss[0x700];static unsigned calls,lookup,mutate;
s32 rpa_search_id(u32 id,u32 result){assert(id==0x12345678);++calls;*(u32*)result=lookup==2?0:(u32)boss;if(mutate){RPA8((u32)actor,0x3A1)=44;RPA8((u32)actor,0x9A6)=0;}return lookup!=0;}
static void reset(void){memset(actor,0,sizeof actor);memset(boss,0,sizeof boss);RPA32((u32)actor,0x488)=0x12345678;RPA16((u32)boss,8)=0xD3;calls=mutate=0;lookup=1;}
int main(void){unsigned cases=0;
for(unsigned health=0;health<256;health++)for(unsigned light=0;light<256;light++)for(unsigned processed=0;processed<2;processed++)for(unsigned path=0;path<5;path++){
 reset();RPA8((u32)actor,0x3A1)=health;RPA8((u32)actor,0x9A6)=light;RPA8((u32)actor,0x461)=processed;
 if(path==0)RPA32((u32)actor,0x488)=0xFFFFFFFF;if(path==1)lookup=0;if(path==2)lookup=2;if(path==3)RPA16((u32)boss,8)=0xD2;
 unsigned eligible=!processed&&path!=0&&((s8)health<=0||light);unsigned resurrect=eligible&&path==4;
 assert(randomizer_poe_down_check((u32)actor)==(s32)resurrect&&calls==eligible);assert(RPA8((u32)actor,0x3A1)==(resurrect?4:health)&&RPA8((u32)actor,0x9A6)==(resurrect?0:light));++cases;
}
const s16 modes[]={-1,0,0x6F,0x70,32767};const s16 frames[]={-32768,-1,0,1,2,3,4,32767};
for(unsigned health=0;health<256;health++)for(unsigned mode=0;mode<5;mode++)for(unsigned frame=0;frame<8;frame++){
 reset();RPA8((u32)actor,0x3A1)=4;RPA8((u32)actor,0x9A6)=1;RPA16((u32)boss,0x562)=modes[mode];RPA16((u32)boss,0x56A)=frames[frame];RPA8((u32)boss,0x3A1)=health;
 unsigned die=modes[mode]==0x6F&&frames[frame]>3;assert(randomizer_poe_down_check((u32)actor)==(s32)!die);
 assert(RPA8((u32)boss,0x3A1)==(die?(u8)(health-1):health));assert(RPA8((u32)actor,0x461)==die);assert(RPA8((u32)actor,0x460)==(die&&(s8)(health-1)<=0));assert(RPA8((u32)actor,0x9A6)==die);++cases;
}
reset();RPA8((u32)actor,0x9A6)=1;RPA8((u32)actor,0x3A1)=4;mutate=1;assert(randomizer_poe_down_check((u32)actor)==1&&RPA8((u32)actor,0x3A1)==4&&!RPA8((u32)actor,0x9A6));++cases;
printf("Poe light-arrow: %u cases passed\n",cases);return 0;}
'''
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'test.c').write_text(code)
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
