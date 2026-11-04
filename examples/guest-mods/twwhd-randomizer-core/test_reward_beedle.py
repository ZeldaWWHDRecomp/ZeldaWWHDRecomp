"""Check purchase side effects, native dialogue fallback and raw slot identity."""
from pathlib import Path
import os, subprocess, tempfile, unittest
class BeedleReward(unittest.TestCase):
 def test_purchase_paths(self):
  source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef int8_t s8;typedef int16_t s16;typedef uint16_t u16;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_HOOK(a,n,p) void n p
static _Alignas(16) u8 npc[0xA00],info[0x6000],mgr[0xA00],events[256];
static u8 init_reward_beedle[3]={0x38,0x39,5};
static s32 bought_slot;
static s32 wwhd_save_event_test(u32 selector){assert(selector==0x6902);return bought_slot;}
static s32 error_status;static u32 selected,owned,points,native_calls,bought,hidden,sold,grants,grant_id,checked_id,event_bit;
static u32 manager_pointer=(u32)mgr;static u8 requirements[4096];
#define WWHD_GAME_DATA(a,t) (*(t*)((a)==0x101F4B5C?(void*)&manager_pointer:(void*)requirements))
static u32 wwhd_dComIfG_getGameInfo(void){return (u32)info;}
static u32 wwhd_dComIfGp_getReverb(u32 room){assert(room==44);return 9;}
static void* wwhd_save_flags_detail_event_object(void){return events;}
static u8 wwhd_dSv_event_c_getEventReg(u32 object,u32 reg){assert(object==(u32)events && reg==0x86FF);return points;}
static s32 wwhd_save_event_set(u32 bit,u32 on){assert(on);event_bit=bit;return 0;}
static u32 randomizer_display_item(u32 item){return item==0x38?0x39:item;}
static void wwhd_execItemGet(u32 item){++grants;grant_id=item;}
#include "reward_beedle.h"
u16 randomizer_native_beedle_message(u32 actor,u32 message,u32 second){assert(actor==(u32)npc && message && second==0xAA);++native_calls;return 0x1234;}
s32 randomizer_beedle_error(u32 items,s32 arg,s32 price){assert(items==(u32)npc+0x940 && !arg && price==123);return error_status;}
void randomizer_beedle_sold(u32 items,s16 index){assert(items==(u32)npc+0x940 && index==*(s16*)(npc+0x940));++sold;}
u32 randomizer_beedle_show_message(u32 items){assert(items==(u32)npc+0x940);return 0xBEEF;}
u8 randomizer_beedle_selected(u32 items){assert(items==(u32)npc+0x940);return selected;}
void randomizer_beedle_hide(u32 items){assert(items==(u32)npc+0x940);++hidden;}
void randomizer_beedle_sound(u32 sound,u32 pos,u32 arg,s32 reverb){assert(sound==0x87F && pos==(u32)npc+0x37C && !arg && reverb==9);++bought;}
s32 randomizer_beedle_owned(u8 item,s32 flag){assert(!flag);checked_id=item;return owned;}
static void reset(void){memset(npc,0,sizeof(npc));memset(info,0,sizeof(info));memset(mgr,0,sizeof(mgr));
 npc[0x326]=44;*(s16*)(npc+0x940)=2;*(s16*)(info+0x5BA4)=123;*(s32*)(info+0x5B48)=500;
 bought=hidden=sold=grants=event_bit=native_calls=0;bought_slot=0;checked_id=0xFF;error_status=0;owned=1;points=0;selected=0x38;
}
int main(void){
 const u32 messages[]={0xF47,0xF68,0xF6A,0xF6C,0xF6E,0x2F4D,0x2F4E,0x2F4F,0x2F73,0x2F75,0x2F77};
 const u32 errors[]={1,2,0x20,4,0x10,0x3F};
 for(unsigned m=0;m<11;m++)for(unsigned cancel=0;cancel<3;cancel++){
  reset();u32 msg=messages[m];info[0x5BD3]=cancel==1;*(u32*)(mgr+0x948)=cancel==2;
  const u16 result=randomizer_beedle_message((u32)npc,(u32)&msg,0xAA);
  assert(result==0xF && !native_calls);
  if(cancel){assert(msg==0xBEEF && !bought && !hidden && !sold && !grants && *(s32*)(info+0x5B48)==500);}
  else {assert(bought==1 && hidden==1 && grants==1 && grant_id==0x38 && checked_id==0x39 && *(s32*)(info+0x5B48)==377);assert(msg==(m<5?0xF4E:0x2F53));}
 }
 for(unsigned m=0;m<11;m++)for(unsigned e=0;e<6;e++){
  reset();u32 msg=messages[m];error_status=errors[e];randomizer_beedle_message((u32)npc,(u32)&msg,0xAA);
  if(m<5 || e>=2){assert(!bought && !hidden && !grants && *(s32*)(info+0x5B48)==500);}
  else assert(bought==1 && grants==1);
 }
 for(unsigned i=0;i<3;i++){
  reset();selected=init_reward_beedle[i];u32 msg=0x2F4D;
  assert(randomizer_beedle_message((u32)npc,(u32)&msg,0xAA)==0xF && sold==1 && npc[0x8E6]==1);
  const u16 bits[]={0x2020,0x2010,0x2008};assert(event_bit==bits[i] && grant_id==selected);
 }
 reset();init_reward_beedle[0]=init_reward_beedle[1]=init_reward_beedle[2]=5;selected=5;u32 msg=0x2F4D;
 randomizer_beedle_message((u32)npc,(u32)&msg,0xAA);assert(sold==1 && event_bit==0x2010);
 reset();selected=0x2C;owned=0;*(s16*)(npc+0x940)=0;msg=0xF47;
 assert(randomizer_beedle_message((u32)npc,(u32)&msg,0xAA)==0x10 && npc[0x996]==3 && sold==1 && event_bit==0x6902 && !grants && msg==0xF47);
 for(s16 slot=0;slot<3;slot++)for(unsigned shop=0;shop<6;shop++)for(unsigned prior=0;prior<2;prior++){
  reset();*(s16*)(npc+0x940)=slot;npc[0x99F]=shop;bought_slot=prior;msg=0xF47;
  randomizer_beedle_message((u32)npc,(u32)&msg,0xAA);
  const unsigned one_time=slot==0 && shop!=4 && shop!=5 && !prior;
  assert(sold==one_time && event_bit==(one_time?0x6902:0));
 }
 reset();points=1;msg=0xF47;randomizer_beedle_message((u32)npc,(u32)&msg,0xAA);assert(msg==0xF4C);
 reset();npc[0x99E]=1;msg=0xF47;randomizer_beedle_message((u32)npc,(u32)&msg,0xAA);assert(msg==0x2F53);
 reset();msg=0x123;assert(randomizer_beedle_message((u32)npc,(u32)&msg,0xAA)==0x1234 && native_calls==1 && !bought);
 return 0;
}
'''
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);(p/'test.c').write_text(source)
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-I',str(Path(__file__).resolve().parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
