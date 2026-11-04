import os
from pathlib import Path
import subprocess
import tempfile
import unittest
class DrcCloud(unittest.TestCase):
    def test_construction_named_gates_and_load_lifecycle(self):
        source=r"""
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int8_t s8;typedef uint8_t u8;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(uintptr_t) u8 actor[0x500],archive;
static u32 save=0x10000;
static unsigned initialized,reload,name,done,load,heap,model,calls[12],step;
static void *data(u32 addr){return addr==1?(void*)&save:(void*)&archive;}
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
#include "drc_cloud.h"
void randomizer_drc_cloud_ctor(u32 a){assert(a==(u32)actor);calls[step++]=1;*(uint32_t*)(actor+0x2E4)=0x100;}
u32 randomizer_drc_cloud_btk(u32 a){assert(a==(u32)actor+0x3B8);assert(*(uint32_t*)(actor+0xB4)==0x1002EC74);calls[step++]=2;return 0;}
u32 randomizer_drc_cloud_param(u32 a,u32 width,u32 shift){assert(a==(u32)actor&&width==1&&shift==0);calls[step++]=3;return name;}
s32 randomizer_drc_cloud_symbol(u32 collect,u32 symbol){assert(collect==save+0xD4&&symbol==0);calls[step++]=4;return done;}
s32 randomizer_drc_cloud_boss(u32 stage){assert(stage==3);calls[step++]=5;return done;}
s32 randomizer_drc_cloud_load(u32 phase,u32 arc){assert(phase==(u32)actor+0x3AC&&arc==(u32)&archive);calls[step++]=6;return load;}
s32 randomizer_drc_cloud_heap(u32 a,u32 callback,u32 size){assert(a==(u32)actor&&callback==0x02387358&&size==0x5A0);assert(actor[0x43C]==1);calls[step++]=7;*(uint32_t*)(actor+0x3B4)=model;return heap;}
void randomizer_drc_cloud_matrix(u32 a){assert(a==(u32)actor&&*(uint32_t*)(actor+0x348)==(model?model+0xC8:0));calls[step++]=8;*(uint32_t*)(actor+0x438)=99;}
void randomizer_drc_cloud_action(u32 a,s32 action){assert(a==(u32)actor&&action==0&&*(uint32_t*)(actor+0x438)==99);calls[step++]=9;}
int main(void){for(initialized=0;initialized<2;initialized++)for(reload=0;reload<2;reload++)for(name=0;name<4;name++)for(done=0;done<2;done++)for(load=0;load<6;load++)for(heap=0;heap<2;heap++)for(model=0;model<2;model++){
 memset(actor,0,sizeof(actor));memset(calls,0,sizeof(calls));step=0;*(uint32_t*)(actor+0x2E4)=initialized?8:0;actor[12]=reload?0:1;*(uint32_t*)(actor+0x438)=name;
 unsigned expected=name>1||done?5:load==4&&!heap?5:load;assert(randomizer_drc_cloud_create((u32)actor)==(s32)expected);
 unsigned at=0;if(!initialized){assert(calls[at++]==1&&calls[at++]==2);}if(reload)assert(calls[at++]==3);
 if(name<=1){assert(calls[at++]==(name?4:5));if(!done){assert(calls[at++]==6&&actor[0x43C]==1);if(load==4){assert(calls[at++]==7);if(heap)assert(calls[at++]==8&&calls[at++]==9);}}}assert(at==step);
 assert(*(uint32_t*)(actor+0x2E4)==(initialized?8:0x108));
 }}
"""
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/'drc_cloud.h').write_text(Path(__file__).with_name('drc_cloud.h').read_text().replace('volatile u32*','volatile uint32_t*'))
            (root/'test.c').write_text(source)
            subprocess.run([os.environ.get("CC", "cc"),'-std=c11','-O2','-Wall','-Wextra','-Werror',str(root/'test.c'),'-o',str(root/'test')],check=True)
            subprocess.run([str(root/'test')],check=True)
if __name__=='__main__':unittest.main()
