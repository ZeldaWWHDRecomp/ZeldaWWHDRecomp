from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class DrcLava(unittest.TestCase):
    def test_constructor_flags_and_no_creation_side_effects(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(uintptr_t) unsigned char actor[0x400],expected[0x400];
static unsigned calls,mutate;
#include "drc_lava.h"
void randomizer_drc_lava_ctor(u32 self){
 assert(self==(u32)actor);++calls;
 if(mutate){*(uint32_t*)(actor+0x2E4)=0xFFFFFFF0;actor[0x3B8]=0x17;}
}
int main(void){
 for(unsigned flags=0;flags<65536;flags++)for(mutate=0;mutate<2;mutate++){
  memset(actor,0xA5,sizeof(actor));*(uint32_t*)(actor+0x2E4)=flags;
  memcpy(expected,actor,sizeof(actor));calls=0;
  if(!(flags&8)){
   if(mutate){*(uint32_t*)(expected+0x2E4)=0xFFFFFFF0;expected[0x3B8]=0x17;}
   *(uint32_t*)(expected+0xB4)=0x100278F4;
   *(uint32_t*)(expected+0x2E4)|=8;
  }
  assert(randomizer_drc_lava_create((u32)actor)==5&&calls==!(flags&8));
  assert(!memcmp(actor,expected,sizeof(actor)));
 }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = Path(__file__).with_name('drc_lava.h').read_text()
            (root/'drc_lava.h').write_text(header.replace('volatile u32*', 'volatile uint32_t*'))
            (root/'test.c').write_text(source)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2',
                            '-Wall', '-Wextra', '-Werror', '-I', str(Path(__file__).parent),
                            str(root/'test.c'), '-o', str(root/'test')], check=True)
            subprocess.run([str(root/'test')], check=True)


if __name__ == '__main__':
    unittest.main()
