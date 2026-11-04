from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class DrcWind(unittest.TestCase):
    def test_boss_gate_constructor_and_distinct_environment_getters(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int16_t s16;typedef uint8_t u8;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(uintptr_t) unsigned char actor[0x400],env[4][0xB00];
static unsigned step,gets,constructed,boss,parameter,mutate;
#include "drc_wind.h"
u32 randomizer_drc_wind_environment(void){
 assert(gets<4);
 if(!gets)assert(step++==0);
 else assert(step++==(constructed?3u:4u)+gets);
 return (u32)env[gets++];
}
void randomizer_drc_wind_ctor(u32 self){assert(self==(u32)actor&&step++==1);if(mutate){*(uint32_t*)(actor+0x2E4)=0x100;actor[0xB3]^=255;}}
s32 randomizer_drc_wind_boss(u32 stage){assert(stage==3&&step++==(constructed?1u:2u));return boss;}
void randomizer_drc_wind_go(void){
 assert(step++==(constructed?2u:3u));
 assert(!actor[0x3AC]&&!*(s32*)(actor+0x3B0));
 assert(*(s32*)(actor+0x3B4)==(s32)(!constructed&&mutate?parameter^255:parameter));
}
void randomizer_drc_wind_set(s16 first,s16 second){assert(!first&&!second&&step++==(constructed?3u:4u));}
int main(void){
 for(constructed=0;constructed<2;constructed++)for(boss=0;boss<2;boss++)
 for(parameter=0;parameter<256;parameter++)for(mutate=0;mutate<2;mutate++){
  memset(actor,0xA5,sizeof(actor));memset(env,0,sizeof(env));
  *(uint32_t*)(actor+0x2E4)=constructed?0x28:0x20;actor[0xB3]=parameter;step=gets=0;
  assert(randomizer_drc_wind_create((u32)actor)==(boss?3:4));
  assert(*(uint32_t*)(actor+0x2E4)==(constructed?0x28:mutate?0x108:0x28));
  if(!constructed)assert(*(uint32_t*)(actor+0xB4)==0x10013B94);
  if(boss){assert(gets==1&&actor[0x3AC]==0xA5);continue;}
  assert(gets==4&&step==(constructed?7u:8u));
  assert(*(s32*)(env[1]+0xA4C)==200&&env[2][0xA7D]==0&&*(s32*)(env[3]+0xA80)==30);
  assert(!*(s32*)(env[0]+0xA4C)&&!*(s32*)(env[0]+0xA80));
 }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = Path(__file__).with_name('drc_wind.h').read_text()
            (root/'drc_wind.h').write_text(header.replace('volatile u32*', 'volatile uint32_t*'))
            (root/'test.c').write_text(source)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2',
                            '-Wall', '-Wextra', '-Werror', '-I', str(Path(__file__).parent),
                            str(root/'test.c'), '-o', str(root/'test')], check=True)
            subprocess.run([str(root/'test')], check=True)


if __name__ == '__main__':
    unittest.main()
