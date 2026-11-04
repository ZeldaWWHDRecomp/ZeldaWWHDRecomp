"""Check both seed gifts, signed action fallbacks and failed actor creation."""
from pathlib import Path
import os, subprocess, tempfile, unittest

class MaggieReward(unittest.TestCase):
    def test_actions_and_creation_failure(self):
        source=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t u32;typedef uint8_t u8;
#define WWHD_REPLACE(a,r,n,p) r n p
static u8 actor[0xA00];static _Alignas(16) u8 info[0x6000];
static const u8 init_reward_maggie[]={0x27,0x38};
static unsigned marks,presents,info_calls;static u32 created,item;
u32 wwhd_dComIfG_getGameInfo(void){++info_calls;return (u32)info;}
u32 wwhd_fopAcM_createItemForPresentDemo(u32 pos,u32 value,u32 flags,u32 fourth,u32 room,u32 angle,u32 scale){
 assert(pos==(u32)actor+0x314 && !flags && fourth==0xFFFFFFFFu && room==0xFFFFFFFFu && !angle && !scale);
 ++presents;item=value;return created;
}
int wwhd_save_event_set(u32 bit,u32 value){assert(bit==0x6A01 && value==1);++marks;return 0;}
#include "reward_maggie.h"
int main(void){
 for(unsigned action=0;action<256;action++)for(unsigned success=0;success<2;success++){
  memset(actor,0,sizeof(actor));memset(info,0,sizeof(info));actor[0x942]=(u8)action;
  created=success?0x1234:0xFFFFFFFFu;*(u32*)(info+0x52A0)=0x4321;marks=presents=info_calls=0;
  const u8 result=randomizer_maggie_gift((u32)actor);
  assert(result==(action>1 || success));
  assert(marks==(action==0));
  assert(presents==(action<=1) && info_calls==(action<=1 && success));
  assert(*(u32*)(info+0x52A0)==(action<=1 && success?0x1234:0x4321));
  if(action<=1)assert(item==init_reward_maggie[action]);
  assert(actor[0x942]==action);
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
