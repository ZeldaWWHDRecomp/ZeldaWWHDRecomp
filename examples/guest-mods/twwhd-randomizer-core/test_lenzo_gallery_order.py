from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class LenzoGalleryOrder(unittest.TestCase):
    def test_completion_cut_callbacks_and_prerequisite_order(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int16_t s16;typedef uint16_t u16;typedef uint8_t u8;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static u32 save;
#define WWHD_GAME_DATA(a,t) ((t)save)
static _Alignas(uintptr_t) unsigned char actor[0x500],play[0x6000];
static unsigned ends[2],owned,cut_result,cut_flag,first_flag,calls,gets,sets,private_calls,queries;
u32 wwhd_dComIfG_getGameInfo(void){
 ++gets;
 if(gets==1)*(s16*)(actor+0x430)=456;
 if(gets==2)*(s16*)(actor+0x432)=-456;
 return (u32)play;
}
#include "lenzo_gallery_order.h"
s32 randomizer_gallery_end(u32 manager,s32 event){
 assert(manager==(u32)play+0x52C4&&calls<2);
 assert(event==(calls?-123:123));
 /* The event index must already be captured before the getter. */
 if(!calls)*(s16*)(actor+0x432)=-123;
 return ends[calls++];
}
s32 randomizer_gallery_prerequisite(u32 events,u32 flag){
 assert(events==save+0x644&&flag==0x1701);
 assert(*(u16*)(play+0x52B8)==0x108);++queries;
 save+=0x10000;return owned;
}
void randomizer_gallery_set(u32 events,u32 flag){assert(queries==1&&events==save+0x644&&flag==0x1601);++sets;}
s32 randomizer_gallery_cut(u32 cut){assert(cut==(u32)actor+0x3C0&&!queries);actor[0x420]=cut_flag;return cut_result;}
void randomizer_gallery_private_cut(u32 self){assert(self==(u32)actor&&!queries);++private_calls;}
int main(void){
 for(ends[0]=0;ends[0]<2;ends[0]++)for(ends[1]=0;ends[1]<2;ends[1]++)
 for(owned=0;owned<2;owned++)for(cut_result=0;cut_result<2;cut_result++)
 for(cut_flag=0;cut_flag<256;cut_flag++)for(first_flag=0;first_flag<2;first_flag++){
  memset(actor,0,sizeof(actor));memset(play,0,sizeof(play));save=0x10000;
  *(s16*)(actor+0x430)=123;*(s16*)(actor+0x432)=-123;actor[0x420]=first_flag;
  *(u16*)(play+0x52B8)=0x100;calls=gets=sets=private_calls=queries=0;
  randomizer_gallery_event_move((u32)actor);
  const unsigned done=ends[0]||ends[1];
  assert(calls==(ends[0]?1u:2u)&&gets==calls+done);
  assert(queries==done&&sets==(done&&owned)&&private_calls==(!done&&!cut_result));
  assert(*(u16*)(play+0x52B8)==(done?0x108:0x100));
  assert(actor[0x420]==(done?first_flag:cut_result&&!cut_flag?first_flag:cut_flag));
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
