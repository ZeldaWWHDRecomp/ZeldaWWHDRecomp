"""Check Orca reward priority, first-prize flag and presentation failure behavior."""
from pathlib import Path
import os, subprocess, tempfile, unittest

class OrcaReward(unittest.TestCase):
    def test_reward_priority_and_first_prize(self):
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int16_t s16;typedef int8_t s8;typedef uint8_t u8;
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 actor[0x1100],info[0x6000];
static s16 threshold=1000;
#define WWHD_GAME_DATA(a,t) threshold
static const u32 init_reward_orca_500=0x1F;
static s32 rank,first_flag;static unsigned flag_sets,present_calls;static u32 item,created;
s32 wwhd_save_event_read(u32 selector){assert(selector==0xD003);return rank;}
s32 wwhd_save_event_test(u32 selector){assert(selector==0x0F10);return first_flag;}
s32 wwhd_save_event_set(u32 selector,s32 enabled){assert(selector==0x0F10 && enabled==1);first_flag=1;++flag_sets;return 0;}
u32 wwhd_dComIfG_getGameInfo(void){return (u32)info;}
u32 wwhd_fopAcM_createItemForPresentDemo(u32 pos,u32 value,u32 flags,u32 fourth,u32 room,u32 angle,u32 scale){
 assert(pos==(u32)actor+0x314 && !flags && fourth==0xFFFFFFFFu && room==(u32)(s8)-3 && !angle && !scale);
 item=value;++present_calls;return created;
}
#include "reward_orca.h"
int main(void){
 for(unsigned training=0;training<2;training++)for(unsigned spin=0;spin<3;spin++)
 for(unsigned r=0;r<4;r++)for(unsigned high=0;high<2;high++)
 for(unsigned old_flag=0;old_flag<2;old_flag++)for(unsigned success=0;success<2;success++){
  memset(actor,0,sizeof(actor));memset(info,0,sizeof(info));
  actor[0xFA4]=training?2:0;actor[0xFA3]=(u8)spin;actor[0x326]=(u8)-3;
  *(s32*)(actor+0xF98)=high?1000:999;rank=(s32)r;first_flag=(s32)old_flag;
  *(u32*)(info+0x52A0)=0x4321;created=success?0x1234:0xFFFFFFFFu;flag_sets=present_calls=0;
  unsigned expected;unsigned sets=0;
  if(training)expected=0x38;
  else if(spin==1)expected=0xAA;
  else if(r==1)expected=5;
  else if(r==2)expected=6;
  else if(high && old_flag)expected=0x0F;
  else {expected=0x1F;sets=1;}
  randomizer_orca_prize((u32)actor);
  assert(present_calls==1 && item==expected && actor[0xFA5]==expected);
  assert(flag_sets==sets && first_flag==(old_flag || sets));
  assert(*(u32*)(info+0x52A0)==(success?0x1234:0x4321));
 }
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);(p/'test.c').write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                            '-I',str(Path(__file__).resolve().parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)

if __name__=='__main__': unittest.main()
