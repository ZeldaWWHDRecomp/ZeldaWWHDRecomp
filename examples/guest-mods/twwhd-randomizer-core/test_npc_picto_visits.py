"""Visit events preserve first-story behavior and skip picto ownership paths."""
from pathlib import Path
import subprocess,tempfile,unittest
class PictoVisits(unittest.TestCase):
 def test_events(self):
  source=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef uint16_t u16;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static u8 actor[0x900],game[0x5400],saves[2][0x700],strings[4][16];static unsigned active,queries,searches,partners,demos,sets,names,method,noid;static s32 a,b,c;
static void swap(void){active^=1;}
static u32 randomizer_daily_event(void){return(u32)saves[active]+0x644;}
static u32 wwhd_dComIfG_getGameInfo(void){return(u32)game;}
static s32 wwhd_dSv_event_c_isEventBit(u32 e,u32 flag){assert(e==randomizer_daily_event());queries++;s32 r;if(flag==0x1F80){assert(queries==1);r=a;}else if(flag==0x1E02){assert(queries==2&&a);r=b;}else{assert(flag==0x1208&&queries==(a?3:2));r=c;}swap();return r;}
static void wwhd_dSv_event_c_onEventBit(u32 e,u32 flag){assert(e==randomizer_daily_event()&&flag==0x1E04);sets++;swap();}
static void* data(u32 p){if(p==0x1001E4C8)return strings[0];if(p==0x1001E4C0)return strings[1];if(p==0x100233A4)return strings[2];assert(p==0x1002339C);return strings[3];}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
#include "npc_picto_visits.h"
void randomizer_mk_visit_demo(u32 p){assert(p==(u32)actor&&method==0);assert(actor[0x835]==12&&*(u16*)(actor+0x85E)==0xBEEF);demos++;swap();}
void randomizer_uk_visit_demo(u32 p){assert(p==(u32)actor&&method==1);assert(actor[0x82D]==4&&*(u16*)(actor+0x85A)==0xBEEF);assert(*(u32*)(game+0x529C)==0x654321);demos++;swap();}
u32 randomizer_visit_search(u32 cb,u32 id){assert(cb==0x025E1234&&*(unsigned*)id==77&&!noid);assert(*(u16*)(actor+0x85A)==0xBEEF);searches++;swap();return 0x123456;}
u32 randomizer_visit_partner(u32 e,u32 p){assert(e==(u32)game+0x51D0&&p==(noid?0:0x123456));partners++;swap();return 0x654321;}
void randomizer_visit_event_name(u32 e,u32 p){assert(e==(u32)actor+0xF8);unsigned offset=method==2?0:2;assert(p==(u32)strings[offset+!(a&&!b)]);names++;swap();}
int main(void){s32 values[]={0,1,-1,2};for(method=0;method<4;method++)for(unsigned i=0;i<4;i++)for(unsigned j=0;j<4;j++)for(unsigned k=0;k<4;k++)for(noid=0;noid<2;noid++){
 a=values[i];b=values[j];c=values[k];memset(actor,0xA5,sizeof actor);*(u16*)(actor+0xFC)=0xBEEF;*(unsigned*)(actor+0x804)=noid?0xFFFFFFFF:77;
 active=queries=searches=partners=demos=sets=names=0;const unsigned special=a&&!b;
 if(method==0)assert(randomizer_mk_visit_talk((u32)actor)==(special?12:1));else if(method==1)assert(randomizer_uk_visit_talk((u32)actor)==(special?4:1));else if(method==2)randomizer_mk_visit_event((u32)actor);else randomizer_uk_visit_event((u32)actor);
 assert(queries==(special?2:(a?3:2)));assert(sets==(special&&method<2)&&demos==(special&&method<2));assert(names==(method>=2));assert(partners==(special&&method==1)&&searches==(special&&method==1&&!noid));
 if(!special)assert(actor[0x835]==0xA5&&actor[0x82D]==0xA5);
 }return 0;}
'''
  with tempfile.TemporaryDirectory() as t:
   p=Path(t);(p/'test.c').write_text(source)
   subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
