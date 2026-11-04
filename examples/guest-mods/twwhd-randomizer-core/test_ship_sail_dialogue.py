"""Complete dialogue priorities and real-ownership callbacks, including SafeString capture."""
from pathlib import Path
import os, subprocess, tempfile, unittest
class ShipSailDialogue(unittest.TestCase):
 def test_consumers(self):
  source=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef uint16_t u16;typedef int16_t s16;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static u8 savedata[0x1000],play[0x7000],actor[0x800],vtable[0x30],stagevt[0x180],stageinfo[0x20];
static u32 saveptr=(u32)savedata;
static const char* literals[]={"GanonA","GanonB","GanonC","GanonD","GanonE","GanonN","GanonM","GanonL","GanonJ","Xboss0","Xboss1","Xboss2","Xboss3","GanonK","GTower","Hyrule","Hyroom"};
static u32 literal_address(u32 a){if(a==0x1003A3E0)return(u32)"kenroom";assert(a>=0x1003A3E8&&a<=0x1003A468);return(u32)literals[(a-0x1003A3E8)/8];}
static void* memory(u32 a){return a==1?(void*)&saveptr:(void*)literal_address(a);}
#define WWHD_GAME_DATA(a,t) (*(t*)memory(a))
#define RANDOMIZER_SHIP_DIALOGUE_STRING_VTABLE ((u32)vtable)
static u32 init_use_different_korl_hyrule_text;
static unsigned late_queries;
static unsigned callback_count,owned,bombs,symbols,songs,triforce,assures,assure_mode,in_init;
static u32 event_flags[256],first_string;

static u32 wwhd_dComIfG_getGameInfo(void);
#include "ship_sail_dialogue.h"
static void callback(void){if(in_init)assert(!randomizer_ship_boarding_scope);++callback_count;assert(owned==(*(u8*)(savedata+0x72)&1));}
static u32 stage_get(u32 object){callback();assert(object==(u32)play+0x5150);return(u32)stageinfo;}
static u32 wwhd_dComIfG_getGameInfo(void){callback();return(u32)play;}
s32 randomizer_ship_dialogue_event_native(u32 object,u32 flag){callback();if(flag==0x3D02||flag==0x2C02||flag==0x2C01||flag==0x2D08||flag==0x4004)late_queries++;assert(object==(u32)savedata+0x644);return(event_flags[flag>>8]&(flag&255))!=0;}
s32 randomizer_ship_dialogue_symbol_native(u32 object,u32 symbol){callback();assert(object==(u32)savedata+0xD4);return(symbols>>symbol)&1;}
s32 randomizer_ship_dialogue_item_native(u8 item){callback();assert(item==0x31);return bombs;}
s32 randomizer_ship_dialogue_tact_native(u32 object,u32 song){callback();assert(object==(u32)savedata+0xD4);return(songs>>song)&1;}
s32 randomizer_ship_dialogue_triforce_native(u32 object){callback();assert(object==(u32)savedata+0xD4);return triforce;}
u32 randomizer_ship_dialogue_wind(void){static f32 vector[3];callback();return(u32)vector;}
s32 randomizer_ship_force_dispatch(u32 a){return randomizer_ship_force_message(a);}
static u8 biga[0x40002],bigb[0x40002];
static void assure(randomizer_ship_dialogue_string* text){
 callback();if(!assures)first_string=(u32)text;++assures;
 if(assures==3){
  if(assure_mode==1)((randomizer_ship_dialogue_string*)first_string)->string=(u32)"changed";
  if(assure_mode==2)((randomizer_ship_dialogue_string*)first_string)->string=text->string;
  if(assure_mode==4)text->string=(u32)bigb;
 }
}
static void flag(u32 f){event_flags[f>>8]|=f&255;}
static void reset(unsigned have_sail){
 memset(savedata,0,sizeof savedata);memset(play,0,sizeof play);memset(actor,0,sizeof actor);memset(event_flags,0,sizeof event_flags);
 late_queries=0;init_use_different_korl_hyrule_text=0;owned=have_sail;savedata[0x72]=have_sail;bombs=symbols=songs=triforce=assures=assure_mode=0;
 flag(0x0908);*(u32*)(actor+0x648)=0xDEAD;*(u32*)(play+0x5150)=(u32)stagevt;*(u32*)(stagevt+0x15C)=(u32)stage_get;*(u32*)(vtable+0x14)=(u32)assure;
}
static void expect_init(u32 message){randomizer_ship_boarding_scope=37;in_init=1;randomizer_ship_init_message((u32)actor);in_init=0;assert(*(u32*)(actor+0x648)==message);assert(randomizer_ship_boarding_scope==37);assert(!late_queries);}
int main(void){
 const u32 gates[]={0x2110,0x2C02,0x2C01,0x4004,0x2E02,0x1610,0x3A02,0x2E04,0x1620,0x3910,0x3040,0x3810,0x1E40,0x2F20,0x1940,0x3E10,0x1F02,0x1F04,0x0A08,0x0A20,0x0A80,0x0902};

 for(unsigned sail=0;sail<2;sail++)for(unsigned n=0;n<sizeof gates/sizeof*gates;n++){reset(sail);flag(gates[n]);expect_init(n==0?0x623:0xD73);}
 for(unsigned sail=0;sail<2;sail++){
  reset(sail);event_flags[9]=0;assert(randomizer_ship_force_message((u32)actor)==1&&*(u32*)(actor+0x648)==0x5E0);
  const u32 force_flags[]={0,0,0x0A02,0,0,0x2D10,0x3E01,0x2D02};const u32 force_messages[]={0x5EC,0x5F6,0x607,0x624,0xD5A,0xD65,0x1688,0x1645};
  for(unsigned n=0;n<8;n++){reset(sail);if(n==0)symbols=2;if(n==1)symbols=4;if(n==3)bombs=1;if(n==4)symbols=1;if(force_flags[n])flag(force_flags[n]);assert(randomizer_ship_force_message((u32)actor)==1);assert(*(u32*)(actor+0x648)==force_messages[n]);}
  reset(sail);assert(!randomizer_ship_force_message((u32)actor)&&*(u32*)(actor+0x648)==0xDEAD);
  reset(sail);*(u32*)(play+0x5CD8)=0x10000;expect_init(0xDEAD);
  reset(sail);*(u32*)(actor+0x644)=0x800000;expect_init(0x636);
  for(unsigned option=0;option<2;option++)for(unsigned n=0;n<18;n++){
   reset(sail);init_use_different_korl_hyrule_text=option;flag(0x3D02);strcpy((char*)play+0x5134,n<17?literals[n]:"kenroom");
   expect_init(option&&n==15?0xD77:0xD73);assert(assures==(option?3u:0u));
  }
  reset(sail);init_use_different_korl_hyrule_text=1;strcpy((char*)play+0x5134,"Hyrule");flag(0x2C02);flag(0x2C01);flag(0x2D08);expect_init(0xD77);assert(assures==3);
  reset(sail);flag(0x4004);triforce=8;expect_init(0xD73);
  reset(sail);flag(0x3A02);songs=16;expect_init(0xD73);
  reset(sail);songs=8;expect_init(0xD73);
  reset(sail);flag(0x1980);flag(0x0A80);expect_init(0xD73);
  reset(sail);flag(0x0A80);expect_init(0xD73);
  reset(sail);flag(0x1E40);*(u32*)(stageinfo+12)=1<<16;expect_init(0xD73);*(u32*)(stageinfo+12)=0;

 }
 reset(0);assure_mode=1;assert(randomizer_ship_dialogue_stage((u32)play+0x5134));assert(assures==3);
 reset(0);strcpy((char*)play+0x5134,"different");assure_mode=2;assert(randomizer_ship_dialogue_stage((u32)"literal"));
 reset(0);memset(biga,'x',sizeof biga);memset(bigb,'x',sizeof bigb);assure_mode=4;assert(!randomizer_ship_dialogue_stage((u32)biga));assures=0;biga[0x40000]=bigb[0x40000]=0;assert(randomizer_ship_dialogue_stage((u32)biga));
 assert(callback_count>1000);return 0;
}
'''
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);(p/'test.c').write_text(source)
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-I',str(Path(__file__).resolve().parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
 def test_no_shared_ownership_override(self):
  text=Path(__file__).with_name('ship_sail_dialogue.h').read_text()
  self.assertNotIn('WWHD_REPLACE(0x025B5D40',text)
  self.assertNotIn('randomizer_ship_sail_scope',text)
  # Pinned CC0 d_a_ship.h TU identity: a same-body SafeString from another TU changes callbacks.
  self.assertIn('#define RANDOMIZER_SHIP_DIALOGUE_STRING_VTABLE 0x1003A1EC',text)
if __name__=='__main__':unittest.main()
