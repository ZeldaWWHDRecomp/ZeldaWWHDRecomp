"""Keep Hurricane Spin ownership separate from story progress and preserve native init gates."""
from pathlib import Path
import os, subprocess, tempfile, unittest

class HurricaneSpin(unittest.TestCase):
 def test_consumer_gates_and_native_callback_effects(self):
  source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef uint16_t u16;typedef int16_t s16;typedef float f32;
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 actor[0x7000],save[4800],saved[4800],play[0x6000],model[0x100],joint[0x100],matrix[0x400],model2[0x100],joint2[0x100],matrix2[0x400];
static f32 fixture_scale[3],upper;
static u32 save_pointer;
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_GAME_DATA(a,t) (*(t*)((a)==1?(void*)&save_pointer:(void*)fixture_scale))
static unsigned owned,temporary,legacy,normal,common_calls,anime_calls,effects,persistent_calls,temp_calls,change_initial,change_model,change_scale;
int wwhd_daPy_lk_c__commonProcInit(void* p,int proc){assert(p==actor && proc==0x59 && !common_calls && !anime_calls);common_calls++;if(change_initial)*(u16*)(actor+0x69B0)=0x103;return 0;}
int wwhd_daPy_lk_c__setSingleMoveAnime(void* p,int anm,f32 rate,f32 start,int end,f32 morph){assert(p==actor && anm==0x19 && !rate && !start && end==-1 && morph==3 && common_calls==1 && !anime_calls);anime_calls++;return 0;}
u32 wwhd_dComIfG_getGameInfo(void){return (u32)play;}
int wwhd_save_event_test(u32 flag){assert(flag==0x6901 && anime_calls==1);persistent_calls++;return owned;}
int wwhd_dSv_event_c_isEventBit(u32 object,u32 flag){assert(object==(u32)save+0x1178 && flag==0x0402);temp_calls++;return temporary;}
void* wwhd_daPy_makeEmitter(void* callback,u32 effect,void* transform,void* pos,void* effect_scale){
 assert(callback==actor+(effects?0x6704:0x66F8) && effect==(effects?0x2A:0x29) && pos==actor+0x314);
 assert(transform==((effects && change_model)?matrix2:matrix)+0x180);
 assert(effect_scale==(normal?0:(void*)fixture_scale));
 assert((*(u16*)(((effects && change_model)?joint2:joint)+4)&0x10)!=0);
 if(!effects && change_model)*(u32*)(actor+0x448)=(u32)model2;
 if(!effects && change_scale){save[0x2E]=0x38;play[0x5CEA]=2;}
 effects++;return 0;
}
#include "hurricane_spin.h"
static void reset(void){
 memset(actor,0,sizeof(actor));memset(save,0,sizeof(save));memset(play,0,sizeof(play));memset(model,0,sizeof(model));memset(model2,0,sizeof(model2));memset(joint,0,sizeof(joint));memset(joint2,0,sizeof(joint2));
 *(u32*)(actor+0x448)=(u32)model;*(u32*)(model+0x2C)=(u32)joint;*(u32*)(model2+0x2C)=(u32)joint2;*(u32*)(joint+0x10)=(u32)matrix;*(u32*)(joint2+0x10)=(u32)matrix2;
 *(u16*)(joint+4)=0xA0;*(u16*)(joint2+4)=0xC0;*(u32*)(play+0x5CD8)=0x12;
 common_calls=anime_calls=effects=persistent_calls=temp_calls=change_initial=change_model=change_scale=0;
}
int main(void){
 save_pointer=(u32)save;const u16 equipment[]={0x101,0x103,0xFFFF};const u8 magic[]={0,1,2,255};
 for(owned=0;owned<2;owned++)for(temporary=0;temporary<2;temporary++)for(legacy=0;legacy<2;legacy++)for(normal=0;normal<2;normal++)for(unsigned mp=0;mp<4;mp++)for(unsigned mini=0;mini<256;mini++)for(unsigned item=0;item<3;item++){
  reset();*(u16*)(actor+0x69B0)=equipment[item];save[0x34]=magic[mp];save[0x2E]=normal?0x38:0x39;save[0x644+0x0B]=legacy?0x20:0;play[0x5CEA]=mini;memcpy(saved,save,sizeof(save));
  const unsigned eligible=(owned||temporary)&&equipment[item]==0x103&&magic[mp]>=2&&mini!=2&&mini!=6;
  assert(randomizer_hurricane_spin_init((u32)actor)==1);
  assert(*(s16*)(actor+0x6916)==(eligible?47:-1) && effects==(eligible?2:0));
  assert(*(u32*)(play+0x5CD8)==0x40000012 && !actor[0x68D4] && *(f32*)(actor+0x69F0)==1);
  assert(common_calls==1 && anime_calls==1 && persistent_calls==1 && temp_calls==!owned);
  assert(!memcmp(save,saved,sizeof(save)));
 }
 /* Preserve the non-sword animation cleanup despite failed spin eligibility. */
 for(unsigned attached=0;attached<2;attached++){
  reset();*(u16*)(actor+0x69B0)=0x101;*(s16*)(actor+0x58C2)=-7;*(f32*)(actor+0x58BC)=4;upper=5;
  *(u32*)(actor+0x581C)=attached?(u32)&upper:0;
  randomizer_hurricane_spin_init((u32)actor);
  assert(*(f32*)(actor+0x58BC)==(attached?-7.001f:4.0f));assert(upper==(attached?-7.001f:5.0f));
 }
 /* Native calls may change the state subsequently consumed by this initializer. */
 reset();owned=1;temporary=0;normal=0;change_initial=change_model=change_scale=1;
 *(u16*)(actor+0x69B0)=0xFFFF;save[0x34]=2;save[0x2E]=0x39;
 assert(randomizer_hurricane_spin_init((u32)actor)==1 && effects==2);
 assert(*(u16*)(joint+4)==0xB0 && *(u16*)(joint2+4)==0xD0 && *(s16*)(actor+0x6916)==47);
 return 0;
}
'''
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);(p/'test.c').write_text(source)
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-I',str(Path(__file__).resolve().parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
