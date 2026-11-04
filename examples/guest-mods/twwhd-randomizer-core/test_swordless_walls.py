from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class SwordlessWalls(unittest.TestCase):
    def test_switches_always_flag_and_native_order(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int8_t s8;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static u32 save_pointer=0x10000;
#define WWHD_GAME_DATA(a,t) (*(t*)&save_pointer)
static unsigned char actor[0x400],before[0x400];
static u32 always,switch_index,calls,switch_calls,mutate;static s32 state,room;
#include "swordless_walls.h"
u32 randomizer_wall_parameter(u32 self,u32 width,u32 shift){
 assert(self==(u32)actor);
 if(calls++==0){assert(width==1&&shift==12);if(mutate)switch_index=17;return always;}
 assert(calls==2&&width==8&&shift==0);return switch_index;
}
s32 randomizer_wall_switch(u32 save,u32 number,s32 r){
 assert(calls==2&&save==save_pointer+0x20&&number==switch_index&&r==room);++switch_calls;return state;
}
int main(void){
 for(always=0;always<2;always++)for(switch_index=0;switch_index<256;switch_index++)
 for(room=-128;room<128;room++)for(state=0;state<2;state++){
  memset(actor,0xA5,sizeof(actor));actor[0x2FE]=(unsigned char)room;memcpy(before,actor,sizeof(actor));
  calls=switch_calls=0;const u32 expected=always?1:switch_index==255?0:!state;
  assert(randomizer_wall_appears((u32)actor)==expected);
  assert(calls==(always?1:2)&&switch_calls==(!always&&switch_index!=255));
  assert(!memcmp(before,actor,sizeof(actor)));
 }
 always=0;switch_index=255;room=-1;actor[0x2FE]=255;state=0;calls=switch_calls=0;mutate=1;
 assert(randomizer_wall_appears((u32)actor)==1&&switch_index==17&&switch_calls==1);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); (root/'test.c').write_text(source)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-I', str(Path(__file__).resolve().parent), str(root/'test.c'), '-o', str(root/'test')], check=True)
            subprocess.run([str(root/'test')], check=True)


if __name__ == '__main__':
    unittest.main()
