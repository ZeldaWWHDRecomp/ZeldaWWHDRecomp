"""Check one-time source flags independently of actual inventory ownership."""
from pathlib import Path
import os, subprocess, tempfile, unittest
class QuestFlags(unittest.TestCase):
 def test_consumers_and_native_passthrough(self):
  source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef uint8_t u8;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 a[0xC00],b[0xC00],info[0x6000];
static u32 save_pointer;
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_GAME_DATA(a,t) (*(t*)&save_pointer)
static unsigned delivered,deluxe,gallery,beast,own_value,own_calls,maggie_calls,lenzo_calls,scenario,nested;
u32 wwhd_dComIfG_getGameInfo(void){return (u32)info;}
int wwhd_save_event_test(u32 bit){if(bit==0x6A01)return delivered;if(bit==0x6920)return deluxe;assert(bit==0x1601);return gallery;}
int wwhd_dSv_player_get_bag_item_c_isBeast(u32 p,u32 item){assert(p==save_pointer+0xB0 && item==0);return beast;}
#include "quest_reward_flags.h"
u32 randomizer_native_maggie_message(u32 actor){assert(actor==(u32)a);++maggie_calls;return 0x1234;}
u8 randomizer_native_quest_item_owned(u32 item){assert(item<256);++own_calls;return own_value;}
u32 randomizer_native_lenzo_message(u32 actor){
 ++lenzo_calls;
 if(scenario==1){
  if(!nested){nested=1;const u32 p=randomizer_lenzo_first_query;randomizer_lenzo_message((u32)b);assert(randomizer_lenzo_first_query==p);}
 }
 /* A preceding ownership query must consume the narrow first-query context. */
 if(scenario==2)assert(randomizer_quest_item_owned(0x23)==own_value);
 const u8 first=randomizer_quest_item_owned(0x26);
 assert(randomizer_quest_item_owned(0x26)==own_value);
 assert(actor==(u32)a || actor==(u32)b);return first;
}
int main(void){
 save_pointer=(u32)info;
 for(unsigned shown=0;shown<256;shown++)for(unsigned ended=0;ended<4;ended++)
 for(delivered=0;delivered<2;delivered++)for(beast=0;beast<2;beast++)for(unsigned offered=0;offered<2;offered++){
  memset(a,0,sizeof(a));a[0x92E]=shown;a[0x92F]=ended&1;a[0x930]=ended>>1;a[0x931]=offered;maggie_calls=0;
  const u32 expected=shown!=255 || ended?0x1234:delivered?(beast?0x1E87:0x1E88):offered?0x1E93:0x1E8A;
  assert(randomizer_maggie_message((u32)a)==expected);
  assert(maggie_calls==(shown!=255 || ended));
 }
 const unsigned states[]={0,1,2,3,4,5,255};
 for(unsigned st=0;st<7;st++)for(unsigned button=0;button<7;button++)for(unsigned photo=0;photo<2;photo++)
 for(gallery=0;gallery<2;gallery++)for(deluxe=0;deluxe<2;deluxe++)for(own_value=0;own_value<2;own_value++){
  memset(a,0,sizeof(a));memset(info,0,sizeof(info));a[0xB35]=states[st];info[0x52B0]=button;info[0x52B2]=photo;
  scenario=nested=own_calls=lenzo_calls=0;
  const unsigned applies=!photo && !(button>=1 && button<=4) && states[st]!=1 && states[st]!=3 && states[st]!=4 && gallery;
  assert(randomizer_lenzo_message((u32)a)==(applies?deluxe:own_value));
  assert(!randomizer_lenzo_first_query && own_calls==(applies?1:2) && lenzo_calls==1);
  scenario=2;own_calls=0;assert(randomizer_lenzo_message((u32)a)==own_value && own_calls==3);
 }
 memset(a,0,sizeof(a));memset(b,0,sizeof(b));memset(info,0,sizeof(info));gallery=1;deluxe=1;own_value=0;
 scenario=1;nested=0;assert(randomizer_lenzo_message((u32)a)==1 && !randomizer_lenzo_first_query);
 for(unsigned item=0;item<256;item++)assert(randomizer_quest_item_owned(item)==own_value);
 return 0;
}
'''
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);(p/'test.c').write_text(source)
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-I',str(Path(__file__).resolve().parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
