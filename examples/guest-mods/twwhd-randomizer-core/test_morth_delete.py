"""Morth cleanup, create-status mutation and wrapping counter contracts."""
from pathlib import Path
import os,subprocess,tempfile,unittest
class Morth(unittest.TestCase):
 def test_consumer(self):
  code=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static u8 actor[0x1000];static unsigned int counters[4];
#define WWHD_GAME_DATA(a,t) counters[((a)-0x101B883C)/4]
static u32 wwhd_dComIfG_getGameInfo(void);
#include "morth_delete.h"
static unsigned step,infos,mutation;static u8 status;
void rmd_resource_delete(u32 phase,u32 name){assert(step++==0&&phase==(u32)actor+0x3C8&&name==0x10013794);if(mutation==1)actor[0xD]=status;}
void rmd_ripple_remove(u32 ripple){assert(step++==1&&ripple==(u32)actor+0x7A8);if(mutation==2)actor[0xD]=status;}
void rmd_fire_remove(u32 fire){assert(step++==2&&fire==(u32)actor+0xCDC);if(mutation==3)actor[0xD]=status;}
static u32 wwhd_dComIfG_getGameInfo(void){assert(step==3&&counters[0]==0);++infos;/* original ignored callback must run before the three reset stores */assert(counters[1]==11&&counters[2]==22&&counters[3]==33);counters[0]=19;counters[1]=counters[2]=counters[3]=99;return 0;}
int main(void){unsigned cases=0;const unsigned counts[]={0,1,2,19,0x7FFFFFFF,0x80000000,0xFFFFFFFF};
for(unsigned initial=0;initial<256;initial++)for(unsigned target=0;target<256;target++)for(mutation=0;mutation<4;mutation++)for(unsigned c=0;c<7;c++){
 actor[0xD]=initial;status=target;counters[0]=counts[c];counters[1]=11;counters[2]=22;counters[3]=33;step=infos=0;
 assert(randomizer_morth_delete((u32)actor)==1&&step==3);unsigned final_status=mutation?target:initial;unsigned remaining=counts[c]-(final_status==2);unsigned reset=final_status==2&&remaining==0;
 assert(infos==reset&&counters[0]==(reset?19:remaining));assert(counters[1]==(reset?0:11)&&counters[2]==(reset?0:22)&&counters[3]==(reset?0:33));++cases;
}
printf("Morth delete: %u cases passed\n",cases);return 0;}
'''
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'test.c').write_text(code)
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
