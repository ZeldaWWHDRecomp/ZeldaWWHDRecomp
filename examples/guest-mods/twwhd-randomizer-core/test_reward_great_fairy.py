"""Validate per-fairy seed rewards and the native message-state transitions."""
from pathlib import Path
import os, subprocess, tempfile, unittest

class GreatFairyReward(unittest.TestCase):
    def test_types_messages_and_existing_capacities(self):
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef uint8_t u8;
#define WWHD_REPLACE(a,r,n,p) r n p
static u8 actor[0x600],info[0x6000];static unsigned info_calls;
static const u8 init_reward_great_fairy[]={0x21,0x22,0x23,0x24,0x25,0x26};
u32 wwhd_dComIfG_getGameInfo(void){++info_calls;return (u32)info;}
#include "reward_great_fairy.h"
int main(void){
 const unsigned messages[]={12008,12011,12012,12015,12016,12017,0,0xFFFFFFFFu};
 for(unsigned type=0;type<256;type++)for(unsigned c=0;c<3;c++)
 for(unsigned m=0;m<sizeof(messages)/sizeof(*messages);m++){
  memset(actor,0,sizeof(actor));memset(info,0,sizeof(info));info_calls=0;
  actor[0xB0]=0xDE;actor[0xB1]=0xAD;actor[0xB2]=0xBE;actor[0xB3]=(u8)type;
  actor[0x582]=0xAA;info[0x52A4]=0xBB;
  info[0x32]=(u8)c;info[0x8F]=info[0x90]=c==0?30:c==1?60:99;
  u32 message=messages[m];const u32 result=randomizer_great_fairy_message((u32)actor,(u32)&message);
  if(messages[m]==12008){
   const u8 expected=type<6?(u8)(0x21+type):4;
   assert(result==15 && message==12009 && actor[0x582]==expected && info[0x52A4]==expected && info_calls==1);
  } else {
   const u32 next=messages[m]==12017?12014:(m>=1 && m<=4)?messages[m]+1:messages[m];
   assert(message==next && result==(m>=1 && m<=5?15:16));
   assert(actor[0x582]==0xAA && info[0x52A4]==0xBB && !info_calls);
  }
  assert(actor[0xB0]==0xDE && actor[0xB1]==0xAD && actor[0xB2]==0xBE && actor[0xB3]==type);
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
