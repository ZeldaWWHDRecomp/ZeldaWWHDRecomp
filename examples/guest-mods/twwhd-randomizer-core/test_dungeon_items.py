"""Dungeon-targeted grants and scoped statue ownership, independent of seed assets."""
from pathlib import Path
import os, subprocess, tempfile, unittest

class DungeonItems(unittest.TestCase):
 def test_grants_and_scoped_consumers(self):
  source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef int s32;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_HOOK(a,n,p) void n p
#define WWHD_HOOK_RETURN(a,n,p) void n p
static _Alignas(16) u8 info[0x6000],save[0x12C0],before[0x12C0],info_before[0x6000],vtable[0x200],stag[16];
static u32 save_pointer;
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_GAME_DATA(a,t) (*(t*)&save_pointer)
static unsigned bits,small_calls,bit_calls,event_calls,native_calls,room_calls;
static u32 expected_memory,expected_kind;
u32 wwhd_dComIfG_getGameInfo(void){return (u32)info;}
int wwhd_save_event_test(u32 event){assert((event&0xFF00)==0x6A00);return !!(bits&(event&255));}
void wwhd_save_event_set(u32 event,int value){assert(value==1);bits|=event&255;++event_calls;}
void wwhd_dSv_memBit_c_onDungeonItem(u32 memory,u32 kind){assert(memory==expected_memory && kind==expected_kind);++bit_calls;}
void wwhd_item_func_small_key(void){++small_calls;}
static u32 room(u32 self){assert(self==(u32)info+0x5150);++room_calls;return (u32)stag;}
#include "dungeon_items.h"
u32 randomizer_native_statue_chest(u32 stage,u32 chest){++native_calls;return stage*100+chest;}
s32 randomizer_native_collect_map_count(u32 self){assert(self==123);++native_calls;return 71;}
static const unsigned ids[22]={0x40,0x41,0x5A,0x5B,0x5C,0x5D,0x5E,0x5F,0x60,0x73,0x74,0x75,0x76,0x81,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B};
static const unsigned stages[22]={3,3,3,3,4,4,4,4,5,5,5,5,2,2,6,6,6,6,7,7,7,7};
static const unsigned kinds[22]={3,2,0,1,3,2,0,1,3,2,0,1,0,1,3,2,0,1,3,2,0,1};
int main(void){
 save_pointer=(u32)save;
 for(unsigned n=0;n<22;n++)for(unsigned active=0;active<32;active++)for(unsigned dungeon=0;dungeon<2;dungeon++)for(unsigned count=0;count<256;count++){
  memset(info,0xA5,sizeof(info));memset(save,0xA5,sizeof(save));*(u32*)(info+0x5150)=(u32)vtable;*(u32*)(vtable+0x15C)=(u32)room;stag[9]=dungeon;
  save[0x7BC]=active;expected_memory=(u32)save+(active==stages[n]?0x798:0x3A0+stages[n]*0x24);expected_kind=kinds[n];
  *(u8*)(expected_memory+0x20)=count;memcpy(before,save,sizeof(save));memcpy(info_before,info,sizeof(info));small_calls=bit_calls=room_calls=0;
  assert(randomizer_grant_dungeon_item(ids[n]));
  const unsigned pending=kinds[n]==3 && active==stages[n] && dungeon;
  assert(small_calls==pending);assert(bit_calls==(kinds[n]!=3));
  assert(room_calls==(kinds[n]==3 && active==stages[n]));
  if(kinds[n]==3 && !pending)before[expected_memory-(u32)save+0x20]=(u8)(count+1);
  assert(!memcmp(save,before,sizeof(save)));
  assert(!memcmp(info,info_before,sizeof(info)));
 }
 for(unsigned id=0;id<512;id++){
  unsigned known=id>=0xA3 && id<=0xA7;
  for(unsigned i=0;i<22;i++)known|=id==ids[i];
  if(!known){memcpy(before,save,sizeof(save));assert(!randomizer_grant_dungeon_item(id));assert(!memcmp(save,before,sizeof(save)));}
 }
 for(unsigned pattern=0;pattern<32;pattern++){
  bits=pattern<<2;event_calls=0;
  for(unsigned stage=0;stage<32;stage++)for(unsigned chest=0;chest<32;chest++){
   const unsigned wanted=stage>=3 && stage<=7 && chest==15;
   const unsigned owned=wanted && (pattern&(1u<<(stage-3)));
   assert(randomizer_statue_owned(stage,chest)==!!owned);
   native_calls=0;assert(randomizer_statue_chest(stage,chest)==stage*100+chest && native_calls==1);
   randomizer_statue_is_create_enter();randomizer_statue_red_message_enter();
   assert(randomizer_statue_query_depth==2);
   native_calls=0;assert(randomizer_statue_chest(stage,chest)==(chest==15?!!owned:stage*100+chest));assert(native_calls==(chest!=15));
   randomizer_statue_red_message_leave();assert(randomizer_statue_query_depth==1);randomizer_statue_is_create_leave();
  }
  native_calls=0;assert(randomizer_collect_statue_count(123)==71 && native_calls==1);
  randomizer_quest_statue_count_enter();randomizer_quest_statue_count_enter();
  unsigned count=0;for(unsigned i=0;i<5;i++)count+=!!(pattern&(1u<<i));
  assert(randomizer_collect_statue_count(123)==(s32)count);
  randomizer_quest_statue_count_leave();randomizer_quest_statue_count_leave();assert(!randomizer_statue_count_depth);
  for(unsigned item=0xA3;item<=0xA7;item++)for(unsigned repeat=0;repeat<3;repeat++)assert(randomizer_grant_dungeon_item(item));
  assert(bits==0x7C && event_calls==15);
 }
 return 0;
}
'''
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);(p/'test.c').write_text(source)
   subprocess.run([os.environ.get('CC','clang'),'-O2','-std=c11','-Wall','-Wextra','-Werror','-I',str(Path(__file__).resolve().parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)

if __name__=='__main__':unittest.main()
