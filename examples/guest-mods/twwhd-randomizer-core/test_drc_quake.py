from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class DrcQuake(unittest.TestCase):
    def test_schedule_type_reload_and_hio_lifecycle(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int8_t s8;typedef uint8_t u8;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(uintptr_t) unsigned char actor[0x400],hio[0x20],info[0x20];
static unsigned first,second,seconds,constructed,boss,hio_new,step,types;
static unsigned char name;
static void* data(u32 address){if(address==0x1002EC08)return &name;assert(address==0x1046BD08||address==0x1046BD0C);return hio+(address-0x1046BD08);}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
u32 wwhd_dComIfG_getGameInfo(void){assert(step++==(constructed?4u:5u));return 0x10000;}
static u32 stage_info(u32 stage){assert(stage==0x15150&&step++==(constructed?5u:6u));return (u32)info;}
#define RANDOMIZER_QUAKE_STAGE_INFO(stage) stage_info(stage)
#include "drc_quake.h"
void randomizer_quake_ctor(u32 self){assert(self==(u32)actor&&step++==0);*(uint32_t*)(actor+0x2E4)=0x100;}
s32 randomizer_quake_boss(u32 stage){assert(stage==3&&step++==(constructed?0u:1u));return boss;}
u8 randomizer_quake_type(u32 self){assert(self==(u32)actor);if(!types++){assert(step++==(constructed?1u:2u));return first;}assert(step++==(constructed?3u:4u));return second;}
void randomizer_quake_draw_queue(u32 tag){assert(tag==(u32)actor+0xDC&&step++==(constructed?2u:3u));}
s32 randomizer_quake_hio_child(u32 title,u32 self){assert(title==(u32)&name&&self==(u32)hio&&step++==(constructed?6u:7u));*(unsigned int*)(hio+4)=0xFFFFFFFF;return 7;}
int main(void){
 for(constructed=0;constructed<2;constructed++)for(boss=0;boss<2;boss++)
 for(first=0;first<8;first++)for(second=0;second<8;second++)
 for(seconds=0;seconds<256;seconds++)for(hio_new=0;hio_new<2;hio_new++){
  memset(actor,0,sizeof(actor));memset(hio,0,sizeof(hio));memset(info,0,sizeof(info));
  *(uint32_t*)(actor+0x2E4)=constructed?8:0;info[15]=seconds;hio[0]=hio_new?255:3;
  *(f32*)(actor+0x3B4)=123;*(f32*)(actor+0x3B8)=456;*(unsigned int*)(hio+4)=99;step=types=0;
  assert(randomizer_quake_create((u32)actor)==(boss?3:first>=3?5:4));
  assert(*(uint32_t*)(actor+0x2E4)==(constructed?8:0x108));
  if(boss){assert(types==0);continue;}
  if(first>=3){assert(types==1);continue;}
  assert(types==2&&step==(constructed?6u:7u)+hio_new);
  assert(actor[0x3BC]==second&&!actor[0x3BD]&&*(f32*)(actor+0x3AC)==seconds*30.0f);
  assert(*(f32*)(actor+0x3B4)==(second==0||second==2?4:second==1?0:123));
  assert(*(f32*)(actor+0x3B8)==(second<3?6:456));
  assert(hio[0]==(hio_new?7:3)&&*(unsigned int*)(hio+4)==(hio_new?0u:100u));
 }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = Path(__file__).with_name('drc_quake.h').read_text()
            (root/'drc_quake.h').write_text(header.replace('volatile u32*', 'volatile uint32_t*'))
            (root/'test.c').write_text(source)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2',
                            '-Wall', '-Wextra', '-Werror', '-I', str(Path(__file__).parent),
                            str(root/'test.c'), '-o', str(root/'test')], check=True)
            subprocess.run([str(root/'test')], check=True)


if __name__ == '__main__':
    unittest.main()
