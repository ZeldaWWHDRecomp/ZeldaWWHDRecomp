"""Pearl ownership vs story flags, exact scoped consumers and native finish-branch side effects."""
from pathlib import Path
import os, subprocess, tempfile, unittest
class PearlProgression(unittest.TestCase):
 def test_grants_and_consumer_scopes(self):
  source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef uint16_t u16;typedef uint8_t u8;typedef int32_t s32;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static _Alignas(16) u8 save_area[0x2000],expected[0x2000],play[0x6000],play_before[0x6000],actor[0xC00],other[0xC00];
#define WWHD_GAME_DATA(a,t) ((u32)save_area)
static unsigned symbols,sets,queries,counts,orders,finish_calls,tail_calls,nested;
static u32 last_symbol;static s32 query_result=73;
static u32 state_count,state_event_flags;
void wwhd_dSv_player_collect_c_onSymbol(u32 p,u32 bit){assert(p==(u32)save_area+0xD4&&bit<3);save_area[0xDF]|=1u<<bit;++symbols;last_symbol=bit;}
s32 wwhd_dSv_player_collect_c_getTriforceNum(u32 p){assert(p==(u32)save_area+0xD4);u8 bits=save_area[0xDE];s32 n=0;for(unsigned i=0;i<8;i++)n+=(bits>>i)&1;return n;}
u32 wwhd_dComIfG_getGameInfo(void){return (u32)play;}
void wwhd_dSv_event_c_onEventBit(u32 p,u32 bit);
#include "pearl_progression.h"
#include "ship_boarding.h"
s32 randomizer_native_ship_execute(u32 a){
 assert(randomizer_ship_boarding_scope==1);
 assert(randomizer_pearl_event_query((u32)play+0x644,0x1980)==query_result);
 assert(randomizer_pearl_event_query((u32)save_area+0x644,0x1981)==query_result);
 assert(randomizer_ship_boarding_scope==1);
 /* Complete dialogue-consumer suspension is covered in test_ship_sail_dialogue. */
 if(a==1234){assert(randomizer_ship_execute(1235)==-7);assert(randomizer_ship_boarding_scope==1);}
 unsigned before=queries;
 assert(randomizer_pearl_event_query((u32)save_area+0x644,0x1980)==1);
 assert(queries==before+1&&!randomizer_ship_boarding_scope);
 assert(randomizer_pearl_event_query((u32)save_area+0x644,0x1980)==query_result);
 return -7;
}
void wwhd_dSv_event_c_onEventBit(u32 p,u32 bit){assert(p==(u32)save_area+0x644);assert((bit>>8)<0x100);save_area[0x644+(bit>>8)]|=bit&0xFF;++sets;}
s32 randomizer_native_pearl_event_query(u32 p,u32 bit){(void)p;(void)bit;++queries;return query_result;}
s32 randomizer_native_goddess_count(u32 a){(void)a;++counts;u8 b=save_area[0x658];return !!(b&0x10)+!!(b&0x80)+!!(b&0x40);}
s32 randomizer_native_goddess_execute(u32 a){
 s32*state=(s32*)(a+0xB98);
 if(*state==9){
  ++finish_calls;wwhd_dSv_event_c_onEventBit((u32)save_area+0x644,0x1410); // native setFinishMyEvent before count.
  if(nested){nested=0;assert(randomizer_goddess_count((u32)other)==randomizer_native_goddess_count((u32)other));assert(randomizer_goddess.finish);}
  if(randomizer_goddess_count(a)>=3){wwhd_dSv_event_c_onEventBit((u32)save_area+0x644,0x1E40);++orders;*state=11;}
  else {*state=10;*(u16*)(play+0x52B8)|=8;}
 }else if(*state==8){*state=9; /* native previous event ends; finishing is next frame */
  assert(randomizer_goddess_count(a)==randomizer_native_goddess_count(a));
 }else assert(randomizer_goddess_count(a)==randomizer_native_goddess_count(a));
 ++tail_calls;state_count=randomizer_goddess_count(a);state_event_flags=*(u16*)(play+0x52B8);return 91;
}
static void reset(void){memset(save_area,0,sizeof save_area);memset(play,0x3C,sizeof play);memset(actor,0,sizeof actor);memset(other,0,sizeof other);symbols=sets=queries=counts=orders=finish_calls=tail_calls=nested=0;randomizer_goddess=(randomizer_goddess_scope){0};}
int main(void){
 void(*const grants[])(void)={randomizer_nayru_pearl,randomizer_din_pearl,randomizer_farore_pearl};const u8 placed[]={0x10,0x80,0x40};
 for(unsigned pearl=0;pearl<3;pearl++)for(unsigned inventory=0;inventory<256;inventory++)for(unsigned flags=0;flags<256;flags++){
  reset();memset(save_area,0xA5,sizeof save_area);save_area[0xDF]=inventory;save_area[0x658]=flags;memcpy(expected,save_area,sizeof save_area);memcpy(play_before,play,sizeof play);
  expected[0xDF]|=1u<<pearl;expected[0x658]|=placed[pearl];if(expected[0xDF]==7){expected[0x662]|=0x40;expected[0x672]|=0x80;}
  grants[pearl]();assert(symbols==1&&last_symbol==pearl&&sets==(expected[0xDF]==7?3:1));assert(!memcmp(save_area,expected,sizeof save_area)&&!memcmp(play,play_before,sizeof play));
 }
 const unsigned orders_of_pearls[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
 for(unsigned n=0;n<6;n++){reset();for(unsigned i=0;i<3;i++){grants[orders_of_pearls[n][i]]();assert(!!(save_area[0x662]&0x40)==(i==2));assert(!!(save_area[0x672]&0x80)==(i==2));}grants[n%3]();assert(save_area[0xDF]==7&&(save_area[0x658]&0xD0)==0xD0);}
 for(unsigned bits=0;bits<256;bits++)for(unsigned flag=0;flag<2;flag++){
  reset();save_area[0xDE]=bits;save_area[0x671]=flag?8:0;query_result=flag?73:0;memcpy(play_before,play,sizeof play);
  assert(randomizer_pearl_event_query((u32)save_area+0x644,0x2D08)==query_result); // outside scope stays native even with8shards.
  // With complete source consumers, there is no residual Hyrule flag adapter.
  assert(save_area[0x671]==(flag?8:0));assert(!memcmp(play,play_before,sizeof play));
  assert(!sets&&queries==1);
  assert(randomizer_pearl_event_query((u32)save_area+0x644,0x2D08)==query_result);
 }
 for(unsigned state=0;state<14;state++)for(unsigned flags=0;flags<8;flags++){
  reset();*(s32*)(actor+0xB98)=state;*(s32*)(other+0xB98)=9;save_area[0x658]=(flags&1?0x10:0)|(flags&2?0x80:0)|(flags&4?0x40:0);
  *(u16*)(play+0x52B8)=0x8040;nested=1;assert(randomizer_goddess_execute((u32)actor)==91);
  unsigned actual=!!(save_area[0x658]&0x10)+!!(save_area[0x658]&0x80)+!!(save_area[0x658]&0x40);
  assert(!orders&&tail_calls==1&&state_count==actual&&!randomizer_goddess.actor&&!randomizer_goddess.finish);
  if(state==9){assert(finish_calls==1&&*(s32*)(actor+0xB98)==10&&*(u16*)(play+0x52B8)==0x8048);assert(!!(save_area[0x662]&0x40)==(actual==3));}
  else {assert(!finish_calls&&*(s32*)(actor+0xB98)==(state==8?9:state));assert(!(save_area[0x662]&0x40)&&state_event_flags==0x8040);}
  assert(randomizer_goddess_count((u32)actor)==actual); // no scope leakage.
 }
 for(unsigned flag=0;flag<2;flag++){query_result=flag?73:0;assert(randomizer_pearl_event_query((u32)save_area+0x644,0x1980)==query_result);assert(randomizer_ship_execute(1234)==-7);assert(!randomizer_ship_boarding_scope);assert(randomizer_pearl_event_query((u32)save_area+0x644,0x1980)==query_result);}
 return 0;
}
'''
  here=Path(__file__).resolve().parent
  with tempfile.TemporaryDirectory() as temp:
   c=Path(temp)/'pearl_test.c';exe=Path(temp)/'pearl_test';c.write_text(source)
   subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O2','-I',str(here),str(c),'-o',str(exe)],check=True)
   subprocess.run([str(exe)],check=True)
if __name__=='__main__':unittest.main()
