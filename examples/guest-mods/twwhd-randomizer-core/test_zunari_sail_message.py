from pathlib import Path
import subprocess,tempfile,unittest
class ZunariSailMessage(unittest.TestCase):
 def test_source_flag_inventory_independence_and_full_message_flow(self):
  complete=Path(__file__).with_name('reward_zunari.h').read_text();body=complete[complete.index('/* Complete public CC0'):].replace('volatile u32*','volatile uint32_t*')
  state=Path(__file__).with_name('reward_zunari_state.h').read_text().replace('#pragma once','')
  source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef int32_t s32;
static _Alignas(16) u8 actor[0xB00],other[0xB00],save[0x1300];
static unsigned received,armor,sail,fast,delivery,shops,ownership_calls,received_calls,count_calls,event_calls,mutation,nested;
static u8 events[65536];
#define WWHD_GAME_DATA(a,t) ((u32)save)
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_REPLACE(a,r,n,p) r n p
s32 randomizer_zunari_received_query(u8 item,s32 flag);
s32 randomizer_zunari_owned(u32 item);
s32 randomizer_zunari_event_query(u32 object,u32 flag);
s32 randomizer_zunari_shop_count(void);
'''+state+body+r'''
s32 randomizer_zunari_received_query(u8 item,s32 flag){assert(item==0x2A&&flag==1);received_calls++;if(randomizer_zunari_item_query(item,flag))return received;return armor;}
s32 randomizer_zunari_owned(u32 item){ownership_calls++;assert(item==0x78||item==0x30);if(item==0x78){if(mutation){assert(!randomizer_zunari_query);*(uint32_t*)(other+0x8F4)=0xABCD;nested++;assert(randomizer_zunari_message((u32)other)==0xABCD&&!randomizer_zunari_query);*(s32*)(actor+0x8FC)=-1;delivery=1;}return sail;}return delivery;}
s32 randomizer_zunari_event_query(u32 object,u32 flag){assert(object==(u32)save+0x644);event_calls++;return events[flag];}
s32 randomizer_zunari_shop_count(void){count_calls++;if(mutation&&count_calls==2)return 8;return shops;}
static void reset(void){memset(actor,0,sizeof(actor));memset(other,0,sizeof(other));memset(save,0,sizeof(save));memset(events,0,sizeof(events));ownership_calls=received_calls=count_calls=event_calls=mutation=nested=0;randomizer_zunari_actor=randomizer_zunari_query=0;}
int main(void){
 assert(!randomizer_zunari_present_source((u32)actor,0x2A,0x2A));
 for(received=0;received<2;received++)for(armor=0;armor<2;armor++)for(shops=0;shops<13;shops++)for(delivery=0;delivery<2;delivery++)for(unsigned index=0;index<3;index++)for(unsigned flags=0;flags<16;flags++)for(sail=0;sail<2;sail++)for(fast=0;fast<2;fast++){
  reset();*(s32*)(actor+0x8FC)=index==0?-1:index==1?0:5;events[0x2D01]=flags&1;events[0xE08]=(flags>>1)&1;events[0x1108]=(flags>>2)&1;events[0x1110]=(flags>>3)&1;events[0x2420]=91;u8 snapshot[sizeof(events)];memcpy(snapshot,events,sizeof(events));
  u32 expected;
  if(!received&&shops>=3)expected=0x285D;
  else if(!delivery)expected=0x2890;
  else if(index){expected=events[0x1108]?0x2862:0x2865;}
  else if((flags&1)&&!(flags&2)&&(flags&4))expected=0x2859;
  else if(shops>=8)expected=0x283E;
  else if(shops>1)expected=0x2840;
  else if(flags&4)expected=0x2842;
  else expected=flags&8?0x284B:0x2845;
  randomizer_zunari_query=7;assert(randomizer_zunari_message((u32)actor)==expected&&randomizer_zunari_query==7);
  assert(received_calls==1&&ownership_calls==(!received&&shops>=3?0u:2u));assert(!memcmp(snapshot,events,sizeof(events)));
 }
 reset();received=0;armor=1;shops=2;sail=0;delivery=0;mutation=1;assert(randomizer_zunari_message((u32)actor)==0x283E&&nested==1&&count_calls==2&&!randomizer_zunari_query);
 reset();*(uint32_t*)(actor+0x8F4)=0x12345678;randomizer_zunari_query=9;assert(randomizer_zunari_message((u32)actor)==0x12345678&&*(uint32_t*)(actor+0x8F4)==0&&received_calls==0&&ownership_calls==0&&randomizer_zunari_query==9);
}
'''
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);(root/'test.c').write_text(source)
   subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',str(root/'test.c'),'-o',str(root/'test')],check=True);subprocess.run([str(root/'test')],check=True)
if __name__=='__main__':unittest.main()
