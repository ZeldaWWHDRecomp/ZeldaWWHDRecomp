"""Check narrowly scoped gift conversion and nested event-context restoration."""
from pathlib import Path
import os, subprocess, tempfile, unittest

class IvanReward(unittest.TestCase):
    def test_scope_and_native_forwarding(self):
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 a[0x900],b[0x900];
static const u32 init_reward_ivan=0x27;
static unsigned scenario,calls,nested;static u32 received;
#include "reward_ivan.h"
static u32 randomizer_battlesquid_actor;
static const u8 init_reward_battlesquid[3]={1,2,3};
static const u8 init_reward_zunari[1]={42};
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_GAME_DATA(a,t) ((u32)0x10000)
static void wwhd_dSv_event_c_onEventBit(u32 p,u32 b){(void)p;(void)b;assert(0);}
#include "reward_present.h"
u32 randomizer_native_present_item(u32 pos,u32 item,u32 flags,u32 bit,u32 room,u32 angle,u32 scale){
 (void)pos;assert(flags==0x55 && bit==127 && room==44 && angle==6 && scale==7);
 ++calls;received=item;return 0xBEEF;
}
u8 randomizer_native_ivan_demo(u32 self){
 if(scenario==4 && !nested){
  nested=1;assert(randomizer_ivan_demo((u32)b)==0xA5);assert(randomizer_ivan_actor==self);
 }
 const u32 pos=self+0x314+(scenario==3?4:0),item=scenario==2?5:7;
 assert(randomizer_present_item(pos,item,0x55,127,44,6,7)==0xBEEF);
 return scenario==5?0:0xA5;
}
int main(void){
 memset(a,0,sizeof(a));memset(b,0,sizeof(b));*(s32*)(b+0x830)=13;
 assert(randomizer_present_item((u32)a+0x314,7,0x55,127,44,6,7)==0xBEEF && received==7);
 for(scenario=0;scenario<6;scenario++){
  *(s32*)(a+0x830)=scenario==1?12:13;calls=nested=0;
  assert(randomizer_ivan_demo((u32)a)==(scenario==5?0:0xA5));
  assert(randomizer_ivan_actor==0 && calls==(scenario==4?2:1));
  assert(received==(scenario==1 || scenario==3?7:scenario==2?5:0x27));
 }
 assert(randomizer_present_item((u32)a+0x314,7,0x55,127,44,6,7)==0xBEEF && received==7);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);(p/'test.c').write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                            '-I',str(Path(__file__).resolve().parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)

if __name__=='__main__': unittest.main()
