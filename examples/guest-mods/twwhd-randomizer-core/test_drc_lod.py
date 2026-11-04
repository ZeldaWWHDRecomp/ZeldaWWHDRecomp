import os
from pathlib import Path
import subprocess
import tempfile
import unittest

class DrcLod(unittest.TestCase):
    def test_resources_callbacks_and_independent_boss_gate(self):
        source = r"""
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(uintptr_t) u8 actor[0x500],names[3];
static unsigned room,event,boss,primary,secondary,names_called,resources,event_calls,boss_calls,mutate;
static void *data(u32 address){return &names[address==0x10014034?0:address==0x10014028?1:2];}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static s32 wwhd_save_event_test(u32 flag){assert(flag==0x1820);event_calls++;return event;}
static void access_name(u32 name){assert(name==(u32)actor+0x3AC);names_called++;*(uint32_t*)(actor+0x3AC)=100+names_called;}
#define RANDOMIZER_DRC_LOD_NAME_ACCESS(name) access_name(name)
#include "drc_lod.h"
s32 randomizer_drc_lod_boss(u32 stage){assert(stage==3);boss_calls++;return boss;}
u32 randomizer_drc_lod_resource(u32 name,u32 path){assert(name==100+names_called);assert(names_called==resources+1);resources++;if(resources==1){assert(path==(u32)&names[0]);if(mutate)*(uint32_t*)(actor+0xB0)=13;return primary;}assert(path==(u32)&names[room==11&&!mutate?1:2]);return secondary;}
int main(void){for(room=0;room<64;room++)for(event=0;event<2;event++)for(boss=0;boss<2;boss++)for(primary=0;primary<2;primary++)for(secondary=0;secondary<2;secondary++)for(mutate=0;mutate<2;mutate++){
 memset(actor,0,sizeof(actor));*(uint32_t*)(actor+0xB0)=room;*(uint32_t*)(actor+0x3F0)=77;names_called=resources=event_calls=boss_calls=0;
 unsigned selected=mutate?13:room;unsigned extra=selected==11||(selected==1&&!event)||(selected==13&&!boss);
 assert(randomizer_drc_lod_models((u32)actor)==(s32)(primary?(extra?secondary:1):0));
 assert(resources==1u+(unsigned)(primary&&extra)&&names_called==resources);
 assert(*(uint32_t*)(actor+0x3EC)==primary&&*(uint32_t*)(actor+0x3F0)==(primary&&extra?secondary:77));
 assert(event_calls==(primary&&selected==1)&&boss_calls==(primary&&selected==13));
 }}
"""
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            header=Path(__file__).with_name('drc_lod.h').read_text().replace('volatile u32*','volatile uint32_t*')
            (root/'drc_lod.h').write_text(header)
            (root/'test.c').write_text(source)
            subprocess.run([os.environ.get("CC", "cc"),'-std=c11','-O2','-Wall','-Wextra','-Werror',str(root/'test.c'),'-o',str(root/'test')],check=True)
            subprocess.run([str(root/'test')],check=True)
if __name__=='__main__':unittest.main()
