"""Control-flow fixture for the actual pickup adapter; PPC builds test its ABI."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

class FieldPickup(unittest.TestCase):
    def test_demo_cleanup_and_native_passthrough(self):
        source = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t u32;
typedef int32_t s32;
typedef uint8_t u8;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 actor[0x900],before[0x900];
static unsigned native_calls,rupee_calls,arrow_calls,tg_calls,co_calls;
void wwhd_dCcD_GObjInf_ClrTgHit(void* p){assert(p==actor+0x614);++tg_calls;}
void wwhd_dCcD_GObjInf_ClrCoHit(void* p){assert(p==actor+0x614);++co_calls;}
s32 wwhd_daItem_c__itemActionForArrow(void* p){assert(p==actor);++arrow_calls;return 23;}
#include "field_item_pickup.h"
void randomizer_native_item_pickup(u32 p){assert(p==(u32)actor);++native_calls;}
s32 randomizer_native_rupee_action(u32 p){assert(p==(u32)actor);++rupee_calls;return 17;}
static void reset(unsigned item,unsigned status){
 memset(actor,0xAA,sizeof(actor));actor[0x74E]=(u8)item;actor[0x785]=(u8)status;
 actor[0x783]=0xFF;memcpy(actor+0x63C,"\x12\x34\x56\x78\x12\x34\x56\x79",8);
 memcpy(before,actor,sizeof(actor));native_calls=rupee_calls=arrow_calls=tg_calls=co_calls=0;
}
int main(void){
 const unsigned expanded[]={0x16,0x1D,0x20,0x31,0x37,0x39,0x3A,0x3C,0x3E,0x44,0x4C,0x81,0x84,0xFF};
 const unsigned native[]={0,7,8,0x13,0x14,0x15,0x1E,0x1F,0x38,0x3B,0x3D,0x45,0x4B,0x82,0x83};
 for(unsigned i=0;i<sizeof(expanded)/sizeof(*expanded);i++){
  reset(expanded[i],2);randomizer_field_pickup((u32)actor);
  assert(!native_calls && tg_calls==1 && co_calls==1);
  assert(actor[0x785]==7 && actor[0x783]==0xFB);
  assert(!memcmp(actor+0x63C,"\0\0\0\0\x12\x34\x56\x78",8));
  /* No item, inventory, or save flag can be granted by this transition. */
  memcpy(before+0x63C,actor+0x63C,8);
  before[0x785]=7;before[0x783]=0xFB;assert(!memcmp(before,actor,sizeof(actor)));
  reset(expanded[i],5);randomizer_field_pickup((u32)actor);
  assert(!native_calls && !tg_calls && !co_calls && !memcmp(before,actor,sizeof(actor)));
 }
 for(unsigned i=0;i<sizeof(native)/sizeof(*native);i++){
  reset(native[i],2);randomizer_field_pickup((u32)actor);
  assert(native_calls==1 && !tg_calls && !co_calls && !memcmp(before,actor,sizeof(actor)));
 }
 reset(1,2);assert(randomizer_field_default_action((u32)actor)==17 && rupee_calls==1 && !arrow_calls);
 reset(0x14,2);assert(randomizer_field_default_action((u32)actor)==17 && rupee_calls==1 && !arrow_calls);
 reset(0x20,2);assert(randomizer_field_default_action((u32)actor)==23 && !rupee_calls && arrow_calls==1);
 reset(0x3C,2);assert(randomizer_field_default_action((u32)actor)==23 && !rupee_calls && arrow_calls==1);
 reset(0xFE,2);assert(randomizer_field_default_action((u32)actor)==23 && !rupee_calls && arrow_calls==1);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);(p/'test.c').write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                            '-I',str(Path(__file__).resolve().parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)

if __name__=='__main__': unittest.main()
