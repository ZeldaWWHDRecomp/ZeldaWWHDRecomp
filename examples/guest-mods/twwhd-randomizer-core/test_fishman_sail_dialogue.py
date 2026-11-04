import os
from pathlib import Path
import subprocess,tempfile,unittest
class FishmanSailDialogue(unittest.TestCase):
 def test_first_sail_callback_radius_and_other_story_guards(self):
  header=Path(__file__).with_name('fishman_sail_dialogue.h').read_text().replace('volatile u32*','volatile uint32_t*')
  header=header.replace('const u32 ship=*(volatile uint32_t*)','const u32 ship=*(volatile u32*)')
  source=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
typedef uintptr_t u32;typedef uint32_t u32word;typedef uint8_t u8;typedef int32_t s32;typedef float f32;typedef double f64;
static _Alignas(16) u8 actor[0xD00],ship[0x400],play[2][0x6000];
static f32 limit;static f64 distance;static unsigned n,getters,item_calls,boss_calls,mode_calls,sail,boss,mutation,game;
#define WWHD_GAME_DATA(a,t) (limit)
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#include "fishman_sail_dialogue.h"
u32 randomizer_fish_dialogue_info(void){getters++;return(u32)play[game];}
void randomizer_fish_dialogue_subtract(u32 a,u32 out,u32 b){assert(n++==0&&a==(u32)ship+0x314&&b==(u32)actor+0xC24);if(mutation==1)*(f32*)(ship+0x314)=50;for(unsigned i=0;i<3;i++)((f32*)out)[i]=((f32*)a)[i]-((f32*)b)[i];}
f64 randomizer_fish_dialogue_square(u32 vector){assert(n++==1);const f32*v=(f32*)vector;assert(v[0]==(mutation==1?49.0f:9.0f)&&v[1]==0&&v[2]==27.0f);return 810.0;}
f64 randomizer_fish_dialogue_sqrt(f64 square){assert(n++==2&&square==810.0);if(mutation==2)limit=50;return distance;}
s32 randomizer_fish_dialogue_item(u32 item){assert(n++==3&&item==0x78);item_calls++;return sail;}
s32 randomizer_fish_dialogue_boss(u32 stage){assert(n++==4&&stage==3);boss_calls++;if(mutation==3){game=1;*(uint32_t*)(play[1]+0x5CD8)=0x10000;}return boss;}
void randomizer_fish_dialogue_mode(u32 self,s32 command,s32 mode){assert(n++==5&&self==(u32)actor&&command==0&&mode==6);mode_calls++;}
static void reset(void){memset(actor,0,sizeof(actor));memset(ship,0,sizeof(ship));memset(play,0,sizeof(play));*(u32*)(play[0]+0x5B3C)=(u32)ship;*(f32*)(ship+0x314)=10;*(f32*)(ship+0x318)=20;*(f32*)(ship+0x31C)=30;*(f32*)(actor+0xC24)=1;*(f32*)(actor+0xC28)=2;*(f32*)(actor+0xC2C)=3;limit=100;distance=100;n=getters=item_calls=boss_calls=mode_calls=sail=boss=mutation=game=0;}
int main(void){
 for(unsigned item=0;item<256;item++)for(unsigned beaten=0;beaten<2;beaten++)for(unsigned riding=0;riding<2;riding++){
  reset();sail=item;boss=beaten;*(uint32_t*)(play[0]+0x5CD8)=riding?0x10000:0xFFFFFFFFu^0x10000u;
  randomizer_fish_dialogue_wait((u32)actor);assert(item_calls==1&&boss_calls==1&&mode_calls==(beaten&&riding)&&getters==(beaten?2u:1u));assert(!memcmp(actor+0x314,ship+0x314,12));
 }
 reset();*(u32*)(play[0]+0x5B3C)=0;randomizer_fish_dialogue_wait((u32)actor);assert(getters==1&&n==0&&item_calls==0);
 reset();distance=nextafter(100.0,-INFINITY);randomizer_fish_dialogue_wait((u32)actor);assert(n==3&&!item_calls);
 reset();distance=NAN;boss=1;*(uint32_t*)(play[0]+0x5CD8)=0x10000;randomizer_fish_dialogue_wait((u32)actor);assert(mode_calls==1);
 reset();mutation=1;randomizer_fish_dialogue_wait((u32)actor);assert(*(f32*)(actor+0x314)==10&&*(f32*)(ship+0x314)==50);
 reset();mutation=2;distance=90;randomizer_fish_dialogue_wait((u32)actor);assert(item_calls==1&&limit==50);
 reset();mutation=3;boss=1;randomizer_fish_dialogue_wait((u32)actor);assert(mode_calls==1&&game==1);
}
'''
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);(root/'fishman_sail_dialogue.h').write_text(header);(root/'test.c').write_text(source)
   subprocess.run([os.environ.get("CC", "cc"),'-std=c11','-O2','-Wall','-Wextra','-Werror',str(root/'test.c'),'-lm','-o',str(root/'test')],check=True);subprocess.run([str(root/'test')],check=True)
if __name__=='__main__':unittest.main()
