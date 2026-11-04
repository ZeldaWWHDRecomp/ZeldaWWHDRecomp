from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class ZephosSingleReward(unittest.TestCase):
    def test_message_completion_flags_and_callback_mutations(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint16_t u16;typedef uint8_t u8;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(uintptr_t) unsigned char actor[0xA00],messages[2][0xA00],play[0x6000];
static u32 message_pointer;
#define WWHD_GAME_DATA(a,t) ((t)message_pointer)
static unsigned calls,gets,cancel,mutate,status,flags;
u32 wwhd_dComIfG_getGameInfo(void){++gets;return (u32)play;}
#include "zephos_single_reward.h"
s32 randomizer_zephos_message_status(u32 self){assert(self==(u32)messages[0]&&calls++==0);return (s32)status;}
void randomizer_zephos_set_status(u32 self,s32 value){
 assert(self==(u32)messages[0]&&value==16&&calls++==1);
 if(mutate){message_pointer=(u32)messages[1];*(u16*)(actor+0x784)=0xA702;*(s32*)(actor+0x7B8)=-12345;}
}
void randomizer_zephos_cut_end(u32 manager,s32 staff){
 assert(manager==(u32)play+0x52C4);
 const unsigned talking=flags&2;
 assert(calls++==(talking?2u:0u)&&gets==(talking?2u:1u));
 assert(staff==(talking&&mutate?-12345:12345));
 if(talking){
  assert(messages[0][0x921]==1&&messages[1][0x921]==0);
  assert((*(u16*)(actor+0x784)&0x200)==(cancel?0x200:0));
 }
 if(mutate)*(u16*)(actor+0x784)=0xBEEF;
}
int main(void){
 for(flags=0;flags<65536;flags++)for(status=13;status<16;status++)
 for(cancel=0;cancel<2;cancel++)for(mutate=0;mutate<2;mutate++){
  memset(actor,0,sizeof(actor));memset(messages,0,sizeof(messages));memset(play,0,sizeof(play));
  message_pointer=(u32)messages[0];*(u16*)(actor+0x784)=(u16)flags;*(s32*)(actor+0x7B8)=12345;
  play[0x5BD3]=cancel;calls=gets=0;
  assert(randomizer_zephos_tact((u32)actor)==1);
  if((flags&2)&&status!=14){assert(calls==1&&gets==0&&*(u16*)(actor+0x784)==flags);continue;}
  assert(calls==((flags&2)?3u:1u));
  unsigned expected=mutate?0xBEEF:flags;
  if(flags&2){if(!mutate)expected=(expected&~0x200u)|(cancel?0x200u:0);expected&=~2u;}
  assert(*(u16*)(actor+0x784)==expected);
 }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'test.c').write_text(source)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2',
                            '-Wall', '-Wextra', '-Werror', '-I', str(Path(__file__).parent),
                            str(root/'test.c'), '-o', str(root/'test')], check=True)
            subprocess.run([str(root/'test')], check=True)


if __name__ == '__main__':
    unittest.main()
