import os
from pathlib import Path
import subprocess,tempfile,unittest
class TowerServantEvents(unittest.TestCase):
 def test_order_finish_and_tablet_callback_order(self):
  source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int8_t s8;typedef uint8_t u8;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static _Alignas(uintptr_t) u8 actor[0xA00],save[3][0x1200];static u32 saveptr;
static f32 maximum=-100,gravity=-9,duration=120,zero;
static void* data(u32 a){switch(a){case 1:return &saveptr;case 0x1001F644:return &maximum;case 0x10467ECC:return &gravity;case 0x1001F594:return &duration;case 0x1001F548:return &zero;}assert(0);return 0;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
#include "tower_servant_events.h"
#include "tower_tablet_delete.h"
static unsigned arg,room,pending,sw,event,calls,mode,delegated,timer_value;
s32 randomizer_servant_switch(u32 self,s32 no,s32 r){assert(self==(u32)save[0]+0x20&&no==205&&r==7&&calls++==0);saveptr=(u32)save[1];actor[0x915]=77;actor[0x2DD]=0;return sw;}
s32 randomizer_servant_event(u32 self,u32 flag){if(mode==0){assert(self==(u32)save[1]+0x644&&flag==(arg==1?0x2608u:0x2604u)&&calls++==1);actor[0x915]=88;}else{assert(self==(u32)save[0]+0x644&&flag==0x1B01&&calls++==0);saveptr=(u32)save[1];}return event;}
u32 randomizer_servant_finish_animation(u32 a,s32 animation){assert(a==(u32)actor&&animation==7&&calls++==1);gravity=-3;maximum=-200;*(uint32_t*)(actor+0x8F4)=0x800;actor[0x919]=7;return 0;}
s32 randomizer_native_servant_finish(u32 a,u32 p){assert(a==(u32)actor&&p==0x1234);delegated++;return -5;}
u8 randomizer_totg_tablet_timer(u32 timer){assert(timer==(u32)actor+0x62D&&calls++==0);saveptr=(u32)save[1];return timer_value;}
void randomizer_totg_tablet_draw_stop(u32 a){assert(a==(u32)actor&&calls++==1);saveptr=(u32)save[2];actor[0x62D]=255;}
void randomizer_totg_tablet_event_on(u32 self,u32 flag){assert(self==(u32)save[2]+0x644&&flag==0x2510&&calls++==2);}
int main(void){mode=0;for(arg=0;arg<256;arg++)for(room=0;room<3;room++)for(pending=0;pending<3;pending++)for(sw=0;sw<2;sw++)for(event=0;event<2;event++){
 memset(actor,0,sizeof(actor));saveptr=(u32)save[0];actor[0x2DD]=arg;actor[0x326]=room==0?7:room==1?8:255;actor[0x915]=pending==0?255:pending==1?0:7;*(uint32_t*)(actor+0x904)=205;calls=0;
 randomizer_servant_event_order((u32)actor);unsigned active=!pending&&!room&&(arg==1||arg==2);
 assert(calls==(active?(sw?2u:1u):0u));assert(actor[0x915]==(active?(sw?(event?88:(arg==1?8:9)):77):(pending==0?255:pending==1?0:7)));
 }
 mode=1;for(event=0;event<2;event++)for(unsigned parameter=0;parameter<2;parameter++){
 memset(actor,0,sizeof(actor));saveptr=(u32)save[0];maximum=-100;gravity=-9;calls=0;assert(randomizer_servant_finish((u32)actor,parameter)==1);
 assert(calls==(parameter?0u:2u));assert(*(f32*)(actor+0x378)==(parameter?-100:-200)&&*(f32*)(actor+0x374)==(parameter?-9:-3));assert(*(f32*)(actor+0x8F8)==120&&*(f32*)(actor+0x370)==0&&actor[0x919]==(parameter?1:8));assert(*(uint32_t*)(actor+0x8F4)==(parameter?1:0x801));
 }
 for(unsigned state=1;state<256;state++){actor[0x919]=state;delegated=0;assert(randomizer_servant_finish((u32)actor,0x1234)==-5&&delegated==1);}
 for(timer_value=0;timer_value<256;timer_value++){calls=0;saveptr=(u32)save[0];assert(randomizer_totg_tablet_delete((u32)actor,123)==(timer_value==0));assert(calls==(timer_value?1u:3u));}
}
'''
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory)
   for name in ['tower_servant_events.h','tower_tablet_delete.h']:
    (root/name).write_text(Path(__file__).with_name(name).read_text().replace('volatile u32*','volatile uint32_t*'))
   (root/'test.c').write_text(source)
   subprocess.run([os.environ.get("CC", "cc"),'-std=c11','-O2','-Wall','-Wextra','-Werror',str(root/'test.c'),'-o',str(root/'test')],check=True)
   subprocess.run([str(root/'test')],check=True)
if __name__=='__main__':unittest.main()
