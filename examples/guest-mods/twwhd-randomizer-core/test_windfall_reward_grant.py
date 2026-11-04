"""Packed one-time reward flags and post-factory singleton/return contracts."""
from pathlib import Path
import os,subprocess,tempfile,unittest
from seed_reward_tables import WINDFALL_REWARD_FLAGS,validate_windfall_reward_flags
class WindfallGrant(unittest.TestCase):
 def test_metadata(self):
  fields={a:v.to_bytes(2,'big') for a,v in WINDFALL_REWARD_FLAGS}
  validate_windfall_reward_flags(lambda a,n:fields[a][:n])
  for address in fields:
   for bad in (b'',b'\0',b'\0\0',b'\x69\x80'):
    with self.subTest(address=address,bad=bad),self.assertRaises(ValueError):
     validate_windfall_reward_flags(lambda a,n:bad if a==address else fields[a][:n])
 def test_producer(self):
  code=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef int8_t s8;typedef uint16_t u16;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_HOOK(a,n,p) void n p
#define WWHD_ADDR_save_info_pointer 1
static u8 actor[0x1000],play[0x6000],play2[0x6000],save1[0x800],save2[0x800];static unsigned int table[6],item_index;static u32 saveptr=(u32)save1;static u16 flagtable[3];
static void* data(u32 a){switch(a){case 1:return&saveptr;case 0x101C3998:return table;case 0x101C399C:return flagtable;case 0x101C39A0:return flagtable+1;case 0x101C39AC:return flagtable+2;}assert(0);return 0;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static u32 wwhd_dComIfG_getGameInfo(void);
#include "windfall_reward_grant.h"
static unsigned infos,events,factories;static unsigned int expected,result;static int useindex,mutate;static u32 playptr;
static u32 wwhd_dComIfG_getGameInfo(void){++infos;return playptr;}
u32 rwt_integer(u32 manager,s32 staff,u32 name,s32 count){assert(manager==(u32)play+0x52C4&&staff==-123&&name==0x10020668&&count==3);return useindex?(u32)&item_index:0;}
u32 rwt_present(u32 position,u32 item,u32 flag,s32 subtype,s32 room,u32 rotation,u32 scale){assert(position==(u32)actor+0x314&&item==(expected&255)&&!flag&&subtype==-1&&room==-3&&!rotation&&!scale);++factories;if(mutate){saveptr=(u32)save2;playptr=(u32)play2;*(unsigned int*)(actor+0x8D0)=0;table[item_index]=0;actor[0x326]=7;}return result;}
void rwt_event_on(u32 save,u32 flag){assert(factories==1&&flag==(expected>>16)&&save==saveptr+0x644);++events;}
int main(void){unsigned cases=0;const unsigned flags[]={0,0x6904,0x6908,0x6910,0xFFFF};const unsigned outcomes[]={0,123,0xFFFFFFFF};
for(unsigned f=0;f<5;f++)for(unsigned item=0;item<256;item++)for(unsigned r=0;r<3;r++)for(useindex=0;useindex<2;useindex++)for(mutate=0;mutate<2;mutate++){
 memset(play,0xAA,sizeof play);memset(play2,0xAA,sizeof play2);infos=events=factories=0;playptr=(u32)play;saveptr=(u32)save1;item_index=item%6;expected=flags[f]<<16|0xAB00|item;result=outcomes[r];table[item_index]=expected;*(unsigned int*)(actor+0x8D0)=expected;actor[0x326]=(u8)-3;
 randomizer_windfall_grant((u32)actor,-123);assert(factories==1&&events==(flags[f]!=0));assert(infos==(result==0xFFFFFFFF?1:2));if(result!=0xFFFFFFFF)assert(*(unsigned int*)(playptr+0x52A0)==result);else assert(*(unsigned int*)(playptr+0x52A0)==0xAAAAAAAA);++cases;
}
randomizer_initialize_windfall_reward_flags();assert(flagtable[0]==0x6904&&flagtable[1]==0x6908&&flagtable[2]==0x6910);printf("Windfall grant: %u cases passed\n",cases);return 0;}
'''
  with tempfile.TemporaryDirectory() as d:
   src=Path(d)/'test.c';src.write_text(code);exe=Path(d)/'test'
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(src),'-o',str(exe)],check=True)
   subprocess.run([str(exe)],check=True)
if __name__=='__main__':unittest.main()
