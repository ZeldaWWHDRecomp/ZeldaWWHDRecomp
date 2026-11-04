"""Exercise the first-prize replacement and all native flag combinations."""
from pathlib import Path
import os, subprocess, tempfile, unittest

class FlightReward(unittest.TestCase):
    def test_closed_dialogue_first_repeat_and_pending(self):
        source=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t u32;typedef uint16_t u16;typedef uint8_t u8;
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 actor[0xA00],expected[0xA00];
static const u32 init_reward_flight_first=6;
static u16 status;static unsigned calls,set_flag_during_talk;
u16 wwhd_daNpcBmcon_c__talk2(void* self,int param){
 assert(self==actor && param==0);++calls;
 if(set_flag_during_talk)actor[0x92C]=1;
 return status;
}
#include "reward_flight.h"
int main(void){
 const u16 states[]={0,0xE,0x10,0x12,0xFFFF};
 for(unsigned s=0;s<sizeof(states)/sizeof(*states);s++)for(unsigned flags=0;flags<256;flags++){
  memset(actor,0xAA,sizeof(actor));actor[0x92C]=(u8)flags;
  *(u32*)(actor+0x900)=0x6A;actor[0x922]=3;memcpy(expected,actor,sizeof(actor));
  status=states[s];calls=0;
  if(status==0x12 && (flags&3)){
   expected[0x92C]=(u8)(flags&~((flags&1)?1u:2u));
   *(u32*)(expected+0x900)=(flags&1)?6:5;expected[0x922]=6;
  }
  assert(randomizer_flight_prize((u32)actor)==(status==0x12));
  assert(calls==1 && !memcmp(actor,expected,sizeof(actor)));
 }
 memset(actor,0,sizeof(actor));calls=0;status=0x12;set_flag_during_talk=1;
 assert(randomizer_flight_prize((u32)actor)==1);
 assert(calls==1 && actor[0x92C]==0 && *(u32*)(actor+0x900)==6 && actor[0x922]==6);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);(p/'test.c').write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                            '-I',str(Path(__file__).resolve().parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)

if __name__=='__main__': unittest.main()
