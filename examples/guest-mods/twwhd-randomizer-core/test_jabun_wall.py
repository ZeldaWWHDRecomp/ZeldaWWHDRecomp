from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class JabunWall(unittest.TestCase):
    def test_late_constructor_scope_and_nested_callbacks(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_HOOK_RETURN(a,n,p) void n p
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_GAME_DATA(a,t) ((t)0x10000)
#include "jabun_wall.h"
static _Alignas(uintptr_t) unsigned char actors[2][0x1000];
static unsigned nested, depth, phase, constructed;
static u32 query(u32 events,u32 flag){return randomizer_jabun_wall_query(events,flag);}
s32 randomizer_native_jabun_wall_create(u32 actor){
 const randomizer_jabun_wall_scope entered=randomizer_jabun_wall;
 assert(entered.actor==actor);
 assert(entered.pending==(!!(*(uint32_t*)(actor+0x2E4)&8)));
 if(!entered.pending){
  /* Constructor callbacks and unrelated constructor returns cannot steal it. */
  assert(!query(0x10644,0xA02));
  randomizer_jabun_wall_constructed(actor+0x698,2,0x3C,0x02315D50);
  assert(!query(0x10644,0xA02));
  randomizer_jabun_wall_constructed(actor+0x710,1,0x130,0x02315DB8);
  assert(!query(0x10644,0xA02));
  randomizer_jabun_wall_constructed(actor+0x710,2,0x131,0x02315DB8);
  assert(!query(0x10644,0xA02));
  randomizer_jabun_wall_constructed(actor+0x710,2,0x130,0x02315DB8);
  /* A later return observer still sees an unconstructed actor. */
  assert(!query(0x10644,0xA02)&&randomizer_jabun_wall.pending==1);
  *(uint32_t*)(actor+0x2E4)|=8;
 }
 if(nested&&!depth){
  const randomizer_jabun_wall_scope outer=randomizer_jabun_wall;
  depth=1;assert(randomizer_jabun_wall_create((u32)actors[1])==(s32)phase);depth=0;
  assert(randomizer_jabun_wall.actor==outer.actor&&randomizer_jabun_wall.pending==outer.pending);
 }
 assert(!query(0x10644,0xA01)&&!query(0x20644,0xA02));
 assert(query(0x10644,0xA02)==1);
 /* Query callbacks, later resource/heap callbacks and repeated queries are native. */
 assert(!query(0x10644,0xA02));
 return (s32)phase;
}
int main(void){
 for(constructed=0;constructed<2;constructed++)for(nested=0;nested<2;nested++)for(phase=0;phase<256;phase++){
  memset(actors,0,sizeof(actors));
  for(unsigned i=0;i<2;i++)*(uint32_t*)(actors[i]+0x2E4)=constructed?8:0;
  randomizer_jabun_wall=(randomizer_jabun_wall_scope){0x12345,1};
  assert(randomizer_jabun_wall_create((u32)actors[0])==(s32)phase);
  assert(randomizer_jabun_wall.actor==0x12345&&randomizer_jabun_wall.pending==1);
 }
 randomizer_jabun_wall=(randomizer_jabun_wall_scope){0,0};
 assert(!query(0x10644,0xA02));
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
