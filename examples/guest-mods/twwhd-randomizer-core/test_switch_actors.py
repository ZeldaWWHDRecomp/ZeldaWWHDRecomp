"""Native-word actor ABI, all switch operations, state/timer/event flow and dungeon masks."""
from pathlib import Path
import os, subprocess, tempfile, unittest, sys
class SwitchActors(unittest.TestCase):
 def test_control_flow(self):
  source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef intptr_t s32;typedef uint8_t u8;typedef uint16_t u16;typedef int16_t s16;
#define WWHD_HOOK(a,n,p) void n p
#define RANDOMIZER_SWITCH_ACTORS_HOST_TEST 1
#define WWHD_ADDR_save_info_pointer 1
static _Alignas(16) u8 save_bytes[0x10000],info_bytes[0x10000],actor_bytes[0x3B8];
static u32 save_base,info,actor;
#define RANDOMIZER_SW_WORD(a,o) (*(volatile uint32_t*)((a)+(o)))
#define WWHD_GAME_DATA(a,t) save_base
static u8 switches[256],dungeon[16];
static u32 reads,read_ids[256],last_room,on_calls,off_calls,ctor_calls,event_calls,end_calls,order_calls,delete_calls,registration_calls;
static u32 order_args[6],last_event,ended;static s32 event_index=-1;
u32 wwhd_dComIfG_getGameInfo(void){return info;}
u32 wwhd_fopAc_ac_c_ct(u32 a){assert(a==actor);++ctor_calls;return a;}
s32 wwhd_dSv_info_c_isSwitch(u32 s,s32 no,s32 room){assert(s==save_base+0x20&&no>=0&&no<256);last_room=room;read_ids[reads++]=no;return switches[no]!=0;}
void wwhd_dSv_info_c_onSwitch(u32 s,s32 no,s32 room){assert(s==save_base+0x20&&no>=0&&no<256);last_room=room;switches[no]=1;++on_calls;}
void wwhd_dSv_info_c_offSwitch(u32 s,s32 no,s32 room){assert(s==save_base+0x20&&no>=0&&no<256);last_room=room;switches[no]=0;++off_calls;}
s32 wwhd_event_index(u32 self,u32 name,s32 map){assert(self==info+0x52C4&&!name);last_event=map;++event_calls;return event_index;}
u32 wwhd_end_check(u32 self,s32 id){assert(self==info+0x52C4);assert(id==-2||id==7);++end_calls;return ended;}
s32 wwhd_fopAcM_orderOtherEventId(u32 a,u32 id,u32 map,u32 hind,u32 priority,u32 flag){order_args[0]=a;order_args[1]=id;order_args[2]=map;order_args[3]=hind;order_args[4]=priority;order_args[5]=flag;++order_calls;return 1;}
s32 wwhd_dSv_memBit_c_isDungeonItem(u32 p,u32 condition){assert(p>=save_base+0x3A0&&(p-save_base-0x3A0)%0x24==0);u32 stage=(p-save_base-0x3A0)/0x24;assert(stage<16&&condition<8);++reads;return !!(dungeon[stage]&(1u<<condition));}
s32 wwhd_fopAcM_delete_actor(u32 a){assert(a==actor);++delete_calls;return 1;}
typedef struct {u32 size,version;const void*profile;char name[8];s32 argument;u32 gba_name;} wwhd_native_actor_profile_v1;
s32 wwhd_native_actor_profile_register(const wwhd_native_actor_profile_v1* r);
void wwhd_log_int(const char* text,s32 value){(void)text;assert(value==0x1E4||value==0x1E5);}
#include "switch_actors.h"
s32 wwhd_native_actor_profile_register(const wwhd_native_actor_profile_v1* r){
 const randomizer_switch_profile*p=r->profile;assert(r->size==sizeof(*r)&&r->version==1&&r->argument==-1&&!r->gba_name);
 assert(p->layer==-3&&p->list==7&&p->list_priority==-3&&!p->base&&!p->actor_base&&!p->extra&&!p->parameters&&!p->group);
 if(!registration_calls++){assert(!strcmp(r->name,"SwOp")&&p->id==0x1E4&&p->size==0x3B8&&p->priority==0x9F&&!p->status&&!p->cull);assert(p->methods==randomizer_swop_methods);}
 else {assert(!strcmp(r->name,"DngSw")&&p->id==0x1E5&&p->size==0x3AC&&p->priority==0x134&&p->status==0x44000&&p->cull==6);assert(p->methods==randomizer_dngsw_methods);}
 return p->id;
}
#define B(o) RANDOMIZER_SW_BYTE(actor,o)
#define H(o) RANDOMIZER_SW_HALF(actor,o)
#define W(o) RANDOMIZER_SW_WORD(actor,o)
static void reset(void){memset((void*)(uintptr_t)actor,0,0x3B8);memset(switches,0,sizeof switches);reads=on_calls=off_calls=event_calls=end_calls=order_calls=delete_calls=ctor_calls=0;event_index=-1;ended=0;B(0x326)=255;}
int main(void){
 save_base=(u32)save_bytes;info=(u32)info_bytes;actor=(u32)actor_bytes;
 randomizer_register_switch_actors();assert(registration_calls==2);
 assert(randomizer_swop_methods[0]==randomizer_swop_create&&randomizer_swop_methods[1]==randomizer_swop_delete&&randomizer_swop_methods[2]==randomizer_swop_execute&&randomizer_swop_methods[3]==randomizer_swop_is_delete&&randomizer_swop_methods[4]==randomizer_swop_draw);
 assert(randomizer_dngsw_methods[0]==randomizer_dngsw_create&&randomizer_dngsw_methods[1]==randomizer_dngsw_delete&&randomizer_dngsw_methods[2]==randomizer_dngsw_execute&&randomizer_dngsw_methods[3]==randomizer_dngsw_is_delete&&randomizer_dngsw_methods[4]==randomizer_dngsw_draw);
 /* All six operations, unsupported operations, empty sets and exactly-one XOR. */
 for(unsigned op=0;op<16;++op)for(unsigned count=0;count<=8;++count)for(unsigned bits=0;bits<256;++bits){
  reset();B(0x3AD)=op;B(0x3B0)=254;B(0x3B1)=count;unsigned set=0;
  for(unsigned i=0;i<count;++i){switches[(254+i)&255]=!!(bits&(1u<<i));set+=!!(bits&(1u<<i));}
  unsigned expected=op==0?set==count:op==1?set<count:op==2?set>0:op==3?set==0:op==4?set==1:op==5?set!=1:0;
  assert(randomizer_swop_is_condition(actor)==expected&&reads==count);
  for(unsigned i=0;i<count;++i)assert(read_ids[i]==((254+i)&255));if(count)assert(last_room==255);
 }
 for(unsigned continuous=0;continuous<2;++continuous)for(unsigned active=0;active<2;++active)for(unsigned output=0;output<256;++output){
  reset();switches[output]=active;W(0xB0)=0x12FE0000u|(output<<8)|(continuous<<4)|5;H(0x2F8)=0xABCD;H(0x2FC)=0xFE09;event_index=-2;
  assert(randomizer_swop_create(actor)==4&&ctor_calls==1&&event_calls==1&&last_event==0xCD);
  assert(B(0x3AD)==5&&B(0x3AE)==continuous&&B(0x3AF)==output&&B(0x3B0)==254&&B(0x3B1)==0x12&&B(0x3B2)==0xCD&&H(0x3B4)==0xFFFE&&B(0x3B6)==9&&B(0x3B7)==9);
  assert(B(0x3AC)==(output==255?5:active?(continuous?4:5):0));assert(W(0x2E4)==8);randomizer_swop_create(actor);assert(ctor_calls==1);
 }
 /* Direct activation's literal state4 is retained for both continuous settings. */
 for(unsigned continuous=0;continuous<2;++continuous){
  reset();B(0x3AF)=42;B(0x3AE)=continuous;H(0x3B4)=0xFFFF;
  assert(randomizer_swop_execute(actor)==1&&B(0x3AC)==4&&switches[42]&&on_calls==1);
  randomizer_swop_execute(actor);assert(B(0x3AC)==4&&!off_calls);
  B(0x3AD)=2;randomizer_swop_execute(actor);assert(B(0x3AC)==0&&!switches[42]&&off_calls==1);
 }
 /* Every timer duration, with and without an event and continuous flag. */
 for(unsigned continuous=0;continuous<2;++continuous)for(unsigned duration=1;duration<256;++duration)for(unsigned event=0;event<2;++event){
  reset();B(0x3AF)=42;B(0x3AE)=continuous;B(0x3B6)=B(0x3B7)=duration;H(0x3B4)=event?7:0xFFFF;
  randomizer_swop_execute(actor);assert(B(0x3AC)==1&&!on_calls);
  for(unsigned tick=duration;tick;--tick){randomizer_swop_execute(actor);assert(B(0x3AC)==1&&B(0x3B7)==tick-1&&!on_calls);}
  randomizer_swop_execute(actor);assert(B(0x3AC)==(event?2:continuous?5:1));assert(on_calls==!event);
  if(!event){randomizer_swop_execute(actor);assert(on_calls==(continuous?1:2));}
 }
 reset();B(0x3AC)=1;B(0x3AE)=1;B(0x3AD)=2;B(0x3B6)=9;B(0x3B7)=3;randomizer_swop_execute(actor);assert(B(0x3AC)==0&&B(0x3B7)==9&&!on_calls);
 for(unsigned continuous=0;continuous<2;++continuous){
  reset();B(0x3AE)=continuous;B(0x3AF)=42;B(0x3B2)=0xAB;H(0x3B4)=0xFFFE;
  randomizer_swop_execute(actor);assert(B(0x3AC)==2&&!on_calls);
  randomizer_swop_execute(actor);assert(order_calls==1&&order_args[0]==actor&&order_args[1]==0xFFFE&&order_args[2]==0xAB&&order_args[3]==0xFFFF&&!order_args[4]&&order_args[5]==1);
  if(continuous){B(0x3AD)=2;randomizer_swop_execute(actor);assert(B(0x3AC)==0&&order_calls==1);B(0x3AC)=2;}
  H(0xF8)=2;randomizer_swop_execute(actor);assert(B(0x3AC)==3&&on_calls==1);
  *(u16*)(uintptr_t)(info+0x52B8)=0x8040;randomizer_swop_execute(actor);assert(B(0x3AC)==3&&end_calls==1);
  ended=1;randomizer_swop_execute(actor);assert(B(0x3AC)==(continuous?4:5)&&*(u16*)(uintptr_t)(info+0x52B8)==0x8048);
 }
 for(unsigned state=5;state<256;++state){reset();B(0x3AC)=state;randomizer_swop_execute(actor);assert(B(0x3AC)==state&&!reads&&!on_calls&&!off_calls&&!event_calls&&!end_calls&&!order_calls);}
 assert(randomizer_swop_draw(actor)==1&&randomizer_swop_delete(actor)==1&&randomizer_swop_is_delete(actor)==1);
 /* Dungeon creation stop phase, all 16 stage bits, all three-bit selectors,
    empty masks, short circuit and deletion only when every selected bit is set. */
 for(unsigned active=0;active<2;++active){reset();W(0xB0)=42;switches[42]=active;assert(randomizer_dngsw_create(actor)==(active?3:4)&&ctor_calls==1&&last_room==255);randomizer_dngsw_create(actor);assert(ctor_calls==1);}
 for(unsigned condition=0;condition<8;++condition)for(unsigned mask=0;mask<65536;++mask){
  reset();memset(dungeon,0xFF,sizeof dungeon);W(0xB0)=(condition<<24)|(mask<<8)|42;
  assert(randomizer_dngsw_conditions(actor)==1);unsigned count=0;for(unsigned i=0;i<16;++i)count+=!!(mask&(1u<<i));assert(reads==count);
  if(mask){unsigned first=0;while(!(mask&(1u<<first)))++first;dungeon[first]&=~(1u<<condition);reads=0;assert(!randomizer_dngsw_conditions(actor)&&reads==1);assert(randomizer_dngsw_execute(actor)==1&&!on_calls&&!delete_calls);dungeon[first]|=1u<<condition;}
  assert(randomizer_dngsw_execute(actor)==1&&on_calls==1&&delete_calls==1&&switches[42]&&last_room==255);
 }
 assert(randomizer_dngsw_draw(actor)==1&&randomizer_dngsw_delete(actor)==1&&randomizer_dngsw_is_delete(actor)==1);
 return 0;
}
'''
  here=Path(__file__).resolve().parent
  with tempfile.TemporaryDirectory() as temp:
   c=Path(temp)/'switch_test.c';exe=Path(temp)/'switch_test';c.write_text(source)
   subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O2','-Wno-int-to-pointer-cast','-Wno-pointer-to-int-cast','-I',str(here),str(c),'-o',str(exe)],check=True)
   subprocess.run([str(exe)],check=True)
if __name__=='__main__':unittest.main()
