"""Boss-source ABI, native argument preservation, one-time pickup and heart side effects."""
from pathlib import Path
import os,subprocess,tempfile,unittest
class BossRewards(unittest.TestCase):
 def test_source_scopes_and_factory_contracts(self):
  source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef int16_t s16;typedef uint16_t u16;typedef float f32;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 actor[0xC00],info[0x6000],save[4800],expected_info[0x6000],expected_save[4800];
static u32 save_pointer;
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_GAME_DATA(a,t) (*(t*)&save_pointer)
u32 wwhd_dComIfG_getGameInfo(void){return (u32)info;}
static u8 init_reward_boss[6];
#include "boss_rewards.h"
static unsigned cloud_calls,native_factory_calls,item_calls,cloud_execute_calls,respawn_calls,nested;
static u32 last_bit,return_id=0xFEDC,expected_item,last_args[8];
u32 randomizer_native_boss_cloud(u32 a,u32 pos,u32 scale,u32 drop,u32 bit){
 assert(a==(u32)actor && pos==0x111 && scale==15);assert(drop==2 || drop==10);cloud_calls++;last_bit=bit;return return_id;
}
static void native_cloud_source(u32 a,unsigned source){
 const u32 old=randomizer_boss_source;assert(old==source);
 assert(randomizer_create_boss_cloud(a,0x111,15,2,0xFF)==return_id);assert(last_bit==((u32)init_reward_boss[source]<<8|0xFF));
 assert(randomizer_create_boss_cloud(a,0x111,15,10,0xFF)==return_id && last_bit==0xFF);
 assert(randomizer_create_boss_cloud(a,0x111,15,2,7)==return_id && last_bit==7);
 if(nested){nested=0;randomizer_boss_gohma(a);assert(randomizer_boss_source==old);}
}
u32 randomizer_boss_kalle_native(u32 a){native_cloud_source(a,1);return 0xA5;}
void randomizer_boss_gohma_native(u32 a){native_cloud_source(a,0);}
void randomizer_boss_helmaroc_native(u32 a){native_cloud_source(a,3);}
void randomizer_boss_jalhalla_native(u32 a){native_cloud_source(a,4);}
u32 randomizer_native_boss_factory(u32 p,u32 unused,u32 room,u32 angle,u32 scale,u32 mode){native_factory_calls++;last_args[0]=p;last_args[1]=unused;last_args[2]=room;last_args[3]=angle;last_args[4]=scale;last_args[5]=mode;return return_id;}
u32 randomizer_boss_create_item(u32 p,u32 item,u32 bit,u32 room,u32 type,u32 angle,u32 action,u32 scale){
 item_calls++;last_args[0]=p;last_args[1]=item;last_args[2]=bit;last_args[3]=room;last_args[4]=type;last_args[5]=angle;last_args[6]=action;last_args[7]=scale;return return_id;
}
static void check_item(u32 p,u32 room,u32 angle,u32 scale,u32 action){
 assert(last_args[0]==p && last_args[1]==expected_item && last_args[2]==0x15 && last_args[3]==room && last_args[4]==3 && last_args[5]==angle && last_args[6]==action && last_args[7]==scale);
}
void randomizer_boss_gohdan_native(u32 a){assert(a==(u32)actor);expected_item=init_reward_boss[2];assert(randomizer_boss_factory(1,0,0xFFFFFFFF,0,3,0)==return_id);check_item(1,0xFFFFFFFF,0,3,5);}
u32 randomizer_native_boss_cloud_execute(u32 a){
 cloud_execute_calls++;assert(a==(u32)actor);
 s16* timer=(s16*)(a+0x3B8);if(*timer){--*timer;if(!*timer && actor[0x3A1]==2){
  assert(randomizer_boss_factory(a+0x314,0,5,a+0x320,0,0)==return_id);
 }}return 1;
}
u32 randomizer_native_boss_respawn(u32 a){respawn_calls++;assert(a==(u32)actor);assert(randomizer_boss_factory(a+0x314,1,5,a+0x320,0,1)==return_id);return 5;}
int main(void){
 save_pointer=(u32)save;
 for(unsigned item=0;item<256;item++){
  for(unsigned i=0;i<6;i++)init_reward_boss[i]=(u8)item;
  randomizer_boss_source=99;nested=1;randomizer_boss_gohma((u32)actor);assert(randomizer_boss_source==99);
  assert(randomizer_boss_kalle((u32)actor)==0xA5 && randomizer_boss_source==99);
  randomizer_boss_helmaroc((u32)actor);randomizer_boss_jalhalla((u32)actor);assert(randomizer_boss_source==99);
  randomizer_boss_factory_active=0;randomizer_boss_gohdan((u32)actor);/* Molgera full camera scope is covered by test_molgera_camera.py. */assert(!randomizer_boss_factory_active);
  for(unsigned mode=0;mode<3;mode++){
   expected_item=item;randomizer_boss_factory_active=1;randomizer_boss_factory_item=item;
   assert(randomizer_boss_factory(12,88,0xFFFFFFFF,34,56,mode)==return_id);check_item(12,0xFFFFFFFF,34,56,mode==1?0xC:5);
  }
  randomizer_boss_factory_active=0;
  for(unsigned drop=0;drop<14;drop++)for(unsigned timer=0;timer<3;timer++)for(unsigned bit=0;bit<2;bit++){
   memset(actor,0,sizeof(actor));actor[0xB0]=item;actor[0xB1]=bit?0xFF:3;actor[0x3A1]=drop;*(s16*)(actor+0x3B8)=timer;
   unsigned before=item_calls,native_before=native_factory_calls;
   assert(randomizer_boss_cloud_execute((u32)actor)==1 && !randomizer_boss_factory_active);
   const unsigned applies=timer==1 && drop==2 && bit;
   assert(item_calls-before==applies);
   assert(native_factory_calls-native_before==(timer==1 && drop==2 && !bit));
   if(applies){expected_item=item;check_item((u32)actor+0x314,5,(u32)actor+0x320,0,5);}
  }
  actor[0xB2]=item;assert(randomizer_boss_respawn((u32)actor)==5 && !randomizer_boss_factory_active);expected_item=item;check_item((u32)actor+0x314,5,(u32)actor+0x320,0,0xC);
 }
 randomizer_boss_source=0xFFFFFFFF;assert(randomizer_create_boss_cloud((u32)actor,0x111,15,2,0xFF)==return_id && last_bit==0xFF);
 randomizer_boss_factory_active=0;assert(randomizer_boss_factory(1,2,3,4,5,6)==return_id);for(unsigned i=0;i<6;i++)assert(last_args[i]==i+1);
 /* A failed factory return is propagated unchanged, without manufacturing actors. */
 return_id=0xFFFFFFFF;randomizer_boss_factory_active=1;assert(randomizer_boss_factory(1,2,3,4,5,6)==return_id);randomizer_boss_factory_active=0;
 /* Nesting restores a non-empty outer factory context as well as empty state. */
 randomizer_boss_factory_active=1;randomizer_boss_factory_item=123;randomizer_boss_gohdan((u32)actor);assert(randomizer_boss_factory_active==1 && randomizer_boss_factory_item==123);
 for(unsigned life=0;life<=80;life++)for(int pending=-8;pending<=8;pending++){
  memset(info,0xA5,sizeof(info));memset(save,0x5A,sizeof(save));*(s16*)(info+0x5B5E)=pending;*(f32*)(info+0x5B44)=3.5f;*(u16*)(save+0x20)=life;
  memcpy(expected_info,info,sizeof(info));memcpy(expected_save,save,sizeof(save));*(s16*)(expected_info+0x5B5E)=pending+4;*(f32*)(expected_info+0x5B44)=3.5f+life;
  randomizer_heart_container_grant();assert(!memcmp(info,expected_info,sizeof(info)) && !memcmp(save,expected_save,sizeof(save)));
 }
 return 0;
}
'''
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);(p/'test.c').write_text(source)
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-I',str(p),'-I',str(Path(__file__).resolve().parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
 def test_seed_operand_extraction_and_rejection(self):
  from extract_boss_rewards import BOSS_SITES,extract
  for item in range(256):
   words={a:(b"\x38\xE0"+bytes([item,255]) if cloud else b"\x38\x80"+bytes([0,item])) for _,a,cloud in BOSS_SITES}
   self.assertEqual(extract(lambda a,n:words[a]),[item]*6)
  for _,site,cloud in BOSS_SITES:
   words={a:(bytes([0x38,0xE0,17,255]) if c else bytes([0x38,0x80,0,17])) for _,a,c in BOSS_SITES}
   for bad in (b"",bytes([0,0,0,0]),bytes([0x38,0xE0,17,3]) if cloud else bytes([0x38,0x80,1,17])):
    broken=dict(words);broken[site]=bad
    with self.assertRaises(ValueError):extract(lambda a,n:broken[a])
if __name__=='__main__':unittest.main()
