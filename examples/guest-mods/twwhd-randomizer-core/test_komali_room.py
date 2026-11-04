from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class KomaliRoom(unittest.TestCase):
    def test_room_query_callbacks_and_exact_door_dispatch(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int16_t s16;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_GAME_DATA(a,t) ((t)0x10000)
static _Alignas(uintptr_t) unsigned char actor[0x400],before[0x400];
static unsigned query_result,queries,doors,mutate,init_mesa_door_unlocked;
static s32 result;
#include "komali_room.h"
s32 randomizer_komali_room_symbol(u32 collect,u32 symbol){
 assert(collect==0x100D4&&symbol==1);++queries;
 if(mutate)actor[0x2E0]^=0x80;
 return query_result;
}
s32 randomizer_native_knob_exception(u32 self){assert(self==(u32)actor);++doors;return result;}
int main(void){
 for(query_result=0;query_result<256;query_result++)for(mutate=0;mutate<2;mutate++){
  memset(actor,0xA5,sizeof(actor));memcpy(before,actor,sizeof(actor));queries=doors=0;
  assert(randomizer_komali_room_init((u32)actor)==0&&queries==1&&!doors);
  if(mutate)before[0x2E0]^=0x80;
  assert(!memcmp(actor,before,sizeof(actor)));
 }
 for(init_mesa_door_unlocked=0;init_mesa_door_unlocked<2;init_mesa_door_unlocked++)for(unsigned angle=0;angle<65536;angle++){
  memset(actor,0xA5,sizeof(actor));*(s16*)(actor+0x2FC)=(s16)angle;
  memcpy(before,actor,sizeof(actor));queries=doors=0;result=(s32)angle-32768;
  assert(randomizer_knob_exception((u32)actor)==((angle==0x6AA||(init_mesa_door_unlocked&&angle==0x6A7))?1:result));
  assert(!queries&&doors==!(angle==0x6AA||(init_mesa_door_unlocked&&angle==0x6A7))&&!memcmp(actor,before,sizeof(actor)));
 }
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
