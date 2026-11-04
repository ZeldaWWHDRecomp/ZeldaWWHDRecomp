"""Exercise shared reward scoping, repeat prizes and nested native calls."""
from pathlib import Path
import os, subprocess, tempfile, unittest
class BattlesquidReward(unittest.TestCase):
 def test_native_scope(self):
  source=r'''
#include <stdint.h>
#include <assert.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 a[0xA00],b[0xA00];
static const u8 init_reward_battlesquid[3]={0x27,0x38,0x79};
static const u8 init_reward_ivan=0xAD;
static u32 received,item,offset,nested;
#include "reward_ivan.h"
#include "reward_battlesquid.h"
static const u8 init_reward_zunari[1]={42};
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_GAME_DATA(a,t) ((u32)0x10000)
static void wwhd_dSv_event_c_onEventBit(u32 p,u32 b){(void)p;(void)b;assert(0);}
#include "reward_present.h"
u32 randomizer_native_present_item(u32 pos,u32 value,u32 flags,u32 bit,u32 room,u32 angle,u32 scale){
 assert(pos && flags==3 && bit==0xFFFF && room==44 && angle==0 && scale==0);
 received=value;return 0x1234;
}
u8 randomizer_native_ivan_demo(u32 self){(void)self;return 1;}
void randomizer_native_battlesquid_wait(u32 self){
 if(nested==1){nested=2;randomizer_battlesquid_wait((u32)b);assert(randomizer_battlesquid_actor==self);}
 assert(randomizer_present_item(self+0x314+offset,item,3,0xFFFF,44,0,0)==0x1234);
}
int main(void){
 const u32 items[]={7,0xCC,5,0xF1,6,0x38};
 for(s32 mode=0;mode<12;mode++)for(unsigned i=0;i<6;i++)for(offset=0;offset<=4;offset+=4){
  *(s32*)(a+0x9AC)=mode;item=items[i];nested=0;
  randomizer_battlesquid_wait((u32)a);
  u32 expected=item;
  if(!offset && mode==6 && item==7)expected=0x27;
  if(!offset && mode==6 && item==0xCC)expected=0x38;
  if(!offset && mode==9 && item==0xF1)expected=0x79;
  assert(received==expected && !randomizer_battlesquid_actor && !randomizer_ivan_actor);
 }
 offset=0;item=7;*(s32*)(a+0x9AC)=6;*(s32*)(b+0x9AC)=9;nested=1;
 randomizer_battlesquid_wait((u32)a);assert(received==0x27 && !randomizer_battlesquid_actor);
 randomizer_present_item((u32)a+0x314,7,3,0xFFFF,44,0,0);assert(received==7);
 return 0;
}
'''
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);(p/'test.c').write_text(source)
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-I',str(Path(__file__).resolve().parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
