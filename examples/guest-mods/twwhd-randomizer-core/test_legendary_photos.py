"""Legendary photo day gates and unchanged ordered event short-circuit."""
import os
from pathlib import Path
import subprocess,tempfile,unittest
class LegendaryPhotos(unittest.TestCase):
 def test_requirements(self):
  code=r'''
#include <stdint.h>
#include <assert.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint16_t u16;
#define WWHD_REPLACE(a,r,n,p) r n p
static unsigned char save[0x800];static s32 day,value;static unsigned mask,calls;static unsigned stages[]={0,2,0,0,0,7,6};
static s32 wwhd_dKy_get_dayofweek(void){return day;}
static u32 randomizer_daily_save(void){return(u32)save;}
static u32 randomizer_daily_event(void){return(u32)save+0x644;}
static s32 wwhd_dSv_player_collect_c_getTriforceNum(u32 p){assert(day==0&&p==(u32)save+0xD4);calls++;return value;}
static u32 randomizer_daily_boss(u32 s){assert(day>=0&&day<7&&s==stages[day]);calls++;return(u32)value;}
static s32 wwhd_dSv_memBit_c_isSwitch(u32 p,u32 bit){assert(day==3&&p==(u32)save+0x3A0&&bit==0x1C);calls++;return value;}
static s32 wwhd_dSv_player_collect_c_isTact(u32 p,u32 song){assert(day==4&&p==(u32)save+0xD4&&song==1);calls++;return value;}
static s32 wwhd_dSv_event_c_isEventBit(u32 e,u32 flag){const unsigned flags[]={0x3002,0x3001,0x3008,0x3004,0x3020,0x3010,0x3180};assert(day==2&&e==(u32)save+0x644&&calls<7&&flag==flags[calls]);return(mask>>(calls++))&1;}
#include "legendary_photos.h"
int main(void){const s32 values[]={-2147483647-1,-1,0,1,2,7,8,9,2147483647};for(day=-2;day<10;day++)for(unsigned v=0;v<9;v++)for(mask=0;mask<128;mask++){
 value=values[v];calls=0;const s32 result=randomizer_legendary_photo_available(0);unsigned n=0,want=0;
 if(day==2){for(unsigned i=0;i<7;i++){n++;if(mask&(1u<<i)){want=1;break;}}}else if(day>=0&&day<=6){n=1;want=day==0?value==8:value!=0;}
 assert(result==(s32)want&&calls==n);
 }
return 0;}
'''
  with tempfile.TemporaryDirectory() as t:
   p=Path(t);(p/'test.c').write_text(code)
   subprocess.run([os.environ.get("CC", "cc"),'-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
