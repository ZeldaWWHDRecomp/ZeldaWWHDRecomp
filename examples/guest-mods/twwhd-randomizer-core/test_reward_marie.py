"""Check seed gift substitution without replacing native dialogue side effects."""
from pathlib import Path
import os, subprocess, tempfile, unittest

class MarieReward(unittest.TestCase):
    def test_native_results_and_nonreward_messages(self):
        source=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t u32;typedef uint8_t u8;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static u8 actor[0xA00];static unsigned calls;
static const u8 init_reward_marie[]={0x21,0x22,0x23,0x24};
#include "reward_marie.h"
u32 randomizer_native_marie_message(u32 self,u32 message){
 assert(self==(u32)actor);++calls;actor[0x806]=0x80;actor[0x807]=0x55;
 ++*(u32*)message;return 0x12345678;
}
int main(void){
 const u32 ids[]={0x271E,0x275C,0x2743,0x2751,0x2754,0x2716,0,0xFFFFFFFFu};
 for(unsigned i=0;i<sizeof(ids)/sizeof(*ids);i++){
  memset(actor,0xAA,sizeof(actor));calls=0;u32 message=ids[i];
  const u32 result=randomizer_marie_message((u32)actor,(u32)&message);
  assert(result==0x12345678 && calls==1 && message==ids[i]+1);
  assert(actor[0x806]==(i<4?init_reward_marie[i]:0x80) && actor[0x807]==0x55);
  for(unsigned n=0;n<sizeof(actor);n++)if(n!=0x806 && n!=0x807)assert(actor[n]==0xAA);
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
