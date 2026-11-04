"""Forest Water choices preserve the complete native dialogue table."""
from pathlib import Path
import subprocess,tempfile,unittest
class DekuChoices(unittest.TestCase):
 def test_choices(self):
  code=r'''
#include <stdint.h>
#include <assert.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef int32_t s32;
#define WWHD_REPLACE(a,r,n,p) r n p
static u8 manager[0x980],save[0x700];static u32 managerptr;static unsigned int message;static s32 eventresult;static unsigned boss,sets,queries,selection;
static void* data(u32 a){assert(a==0x101F4B5C);return&managerptr;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static u32 randomizer_daily_event(void){return(u32)save+0x644;}
static s32 wwhd_dSv_event_c_isEventBit(u32 e,u32 f){assert(e==randomizer_daily_event()&&(f==0x3940||f==0x102||f==0x1820));queries++;return eventresult;}
static u32 randomizer_daily_boss(u32 stage){assert(stage==6&&eventresult);return boss;}
static void wwhd_dSv_event_c_onEventBit(u32 e,u32 f){assert(e==randomizer_daily_event()&&f==0x3940&&message==0x13CB);sets++;message=0xDEADBEEF;}
#include "deku_chart_choices.h"
int main(void){const unsigned pairs[][2]={{0x139D,0x139E},{0x139F,0x13A0},{0x13A1,0x13A2},{0x13A3,0x13A4},{0x13A4,0x13A5},{0x13A5,0x13A6},{0x13A8,0x13A9},{0x13AC,0x13A9},{0x13AE,0x13C5},{0x13AF,0x13C5},{0x13B0,0x13C5},{0x13C1,0x13C5},{0x13C3,0x13C5},{0x13C4,0x13C5},{0x13C0,0x13C1},{0x13C2,0x13C3},{0x13C7,0x13C8},{0x13C8,0x13C9},{0x13C9,0x13CA},{0x13CC,0x13CD},{0x13CD,0x13CE},{0x13CE,0x13CF},{0x13CF,0x13D0},{0x13D0,0x13D1}};const s32 answers[]={0,1,-1,2};const unsigned choices[]={0,1,2,255,0xFFFFFFFF};
 managerptr=(u32)manager;for(unsigned id=0;id<65536;id++)for(unsigned e=0;e<4;e++)for(unsigned s=0;s<5;s++){
 eventresult=answers[e];selection=choices[s];boss=id&1;*(unsigned*)(manager+0x948)=selection;message=id;sets=queries=0;unsigned expected=id,status=16,q=0,set=0;
 for(unsigned p=0;p<sizeof pairs/sizeof pairs[0];p++)if(id==pairs[p][0]){expected=pairs[p][1];status=15;break;}
 if(id==0x13A9||id==0x13C5){expected=eventresult==1?0x13AB:0x13AA;status=15;q=1;}
 if(id==0x13AA||id==0x13AB||id==0x13AD){status=15;expected=0x13C6;if(!selection){expected=id==0x13AD?(eventresult?0x13B0:0x13AE):0x13AD;q=id==0x13AD;}else if(selection==1){expected=id==0x13AA?(eventresult?0x13D2:0x13C7):id==0x13AB?0x13CC:0x13B6;q=id==0x13AA;}}
 if(id==0x13B6){expected=eventresult?(boss?0x13C2:0x13C4):0x13C0;status=15;q=1;}
 if(id==0x13CA){expected=0xDEADBEEF;status=15;set=1;}
 assert(randomizer_deku_chart_choices(0,(u32)&message)==status);assert(message==expected&&queries==q&&sets==set);
 }return 0;}
'''
  with tempfile.TemporaryDirectory() as t:
   p=Path(t);(p/'test.c').write_text(code)
   subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
