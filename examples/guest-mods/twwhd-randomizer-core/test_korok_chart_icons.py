"""Korok chart frames retain marker ordering, guards and callback reloads."""
import os
from pathlib import Path
import subprocess,tempfile,unittest
class KorokChart(unittest.TestCase):
 def test_frames(self):
  code=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static u8 actor[0x60],graphics[0x230],owners[2][0xE0],save[0x700];static u32 graphicsptr;static float collected,available,missing;
static unsigned busy,complete,marked,mutation,bits,frames,operations,hides,queries,gets,owner;
static void* data(u32 a){if(a==0x101F8344)return&graphicsptr;if(a==0x100F7E88)return&collected;if(a==0x100F7E70)return&available;assert(a==0x100F7E84);return&missing;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static u32 randomizer_daily_save(void){return(u32)save;}
static u32 randomizer_daily_event(void){return(u32)save+0x644;}
static s32 wwhd_dSv_event_c_isEventBit(u32 e,u32 flag){assert(e==randomizer_daily_event());queries++;if(flag==0x3940)return marked;assert(flag==0x102);return complete||(mutation&&frames);}
static u8 wwhd_dSv_event_c_getEventReg(u32 e,u32 reg){assert(e==randomizer_daily_event()&&reg==0x9EFF);gets++;return bits;}
#include "korok_chart_icons.h"
s32 randomizer_korok_chart_busy(u32 ui){assert(ui==0x123456);return busy;}
void randomizer_korok_chart_hide(u32 a){assert(a==(u32)actor);hides++;}
static void move_owner(void){owner^=1;*(u32*)(actor+0x44)=(u32)owners[owner];}
void randomizer_korok_chart_operate(u32 a,u32 animation,u32 group){assert(a==0x1000+owner&&animation==0x62+operations&&group==0x63+operations);operations++;move_owner();}
void randomizer_korok_chart_frame(u32 a,u32 animation,u32 group,float frame){const unsigned order[]={2,4,0,7,5,1,6,3};assert(a==0x1000+owner&&animation==0x62+order[frames]&&group==0x63+order[frames]);assert(frame==(((mutation&&frames)||((bits>>frames)&1))?20:10));frames++;move_owner();collected=90;available=80;}
int main(void){for(bits=0;bits<256;bits++)for(busy=0;busy<2;busy++)for(complete=0;complete<2;complete++)for(marked=0;marked<2;marked++)for(mutation=0;mutation<2;mutation++){
 graphicsptr=(u32)graphics;*(u32*)(graphics+0x218)=0x123456;owner=0;*(u32*)(actor+0x44)=(u32)owners[0];*(u32*)(owners[0]+0xD4)=0x1000;*(u32*)(owners[1]+0xD4)=0x1001;collected=20;available=10;missing=0;frames=operations=hides=queries=gets=0;
 randomizer_korok_chart_icons((u32)actor);
 if(busy){assert(hides==1&&!queries&&!frames&&!operations);}else if(complete){assert(operations==8&&!frames&&queries==1);}else if(!marked){assert(!frames&&!operations&&queries==2);}else{assert(frames==8&&!operations&&queries==10&&gets==(mutation?1:8));}
 }
return 0;}
'''
  with tempfile.TemporaryDirectory() as t:
   p=Path(t);(p/'test.c').write_text(code)
   subprocess.run([os.environ.get("CC", "cc"),'-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
