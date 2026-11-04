"""Ho Ho spawn retains constructor and event gating without switch queries."""
from pathlib import Path
import tempfile,subprocess,unittest
class HoHoSpawn(unittest.TestCase):
 def test_spawn(self):
  source=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static u8 actor[0x900],save[0x700];static u32 saveptr,arg;static unsigned ctor,arguments,sw,event;static s32 result;
static void* data(u32 a){assert(a==1);return&saveptr;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static s32 wwhd_dSv_event_c_isEventBit(u32 p,u32 bit){assert(p==(u32)save+0x644&&bit==0x520);event++;return result;}
#include "hoho_spawn.h"
u32 randomizer_hoho_ctor(u32 a){assert(a==(u32)actor);ctor++;*(unsigned*)(actor+0x2E4)=0x400;return a;}
u32 randomizer_hoho_argument(u32 a){assert(a==(u32)actor);arguments++;return arg;}
u32 randomizer_hoho_switch_bit(u32 a){assert(a==(u32)actor);sw++;return 255;}
int main(void){
 const s32 results[]={0,1,-1,2};
 for(arg=0;arg<256;arg++)for(unsigned initialized=0;initialized<2;initialized++)for(unsigned e=0;e<4;e++){
  memset(actor,0xA5,sizeof actor);*(unsigned*)(actor+0x2E4)=initialized?0x808:0;actor[0x8BB]=0x93;
  saveptr=(u32)save;ctor=arguments=sw=event=0;result=results[e];
  s32 got=randomizer_hoho_spawn((u32)actor);assert(got==(arg==2&&!result?3:2));
  assert(ctor==!initialized&&arguments==1&&sw==(arg==9)&&event==(arg==2));
  assert(*(unsigned*)(actor+0x2E4)==(initialized?0x808:0x408));assert(actor[0x8BB]==(got==3?0x93:1));
  for(unsigned i=0;i<sizeof actor;i++)if(!(i>=0x2E4&&i<0x2E8)&&i!=0x8BB)assert(actor[i]==0xA5);
 }
 return 0;
}
'''
  with tempfile.TemporaryDirectory() as t:
   p=Path(t);(p/'test.c').write_text(source)
   subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
