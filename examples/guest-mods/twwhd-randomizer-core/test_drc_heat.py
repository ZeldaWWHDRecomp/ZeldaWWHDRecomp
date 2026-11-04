import os
from pathlib import Path
import subprocess
import tempfile
import unittest
class DrcHeat(unittest.TestCase):
    def test_parameters_stage_and_emitter_callbacks(self):
        source=r"""
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int8_t s8;typedef uint8_t u8;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(uintptr_t) u8 actor[0x500],play[0x6000],player[0x400],emit[0x300],other[0x300],cb;
static u32 emitter,path;static u8 alpha,flag;
static unsigned init,type,path_index,stage,boss,existing,success,mutate,calls[20],step;
static void *data(u32 a){switch(a){case 0x10475628:return &emitter;case 0x10475630:return &path;case 0x10475652:return &flag;case 0x10475653:return &alpha;case 0x1046EC88:return &cb;}assert(0);return 0;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static int stage_test(void){calls[step++]=3;return stage;}
#define RANDOMIZER_DRC_HEAT_STAGE() stage_test()
u32 wwhd_dComIfG_getGameInfo(void){return (u32)play;}
#include "drc_heat.h"
void randomizer_drc_heat_ctor(u32 a){assert(a==(u32)actor);calls[step++]=1;*(uint32_t*)(actor+0xB0)=((path_index^1)<<8)|((type^1)<<20);}
u32 randomizer_drc_heat_path(s32 i,s32 room){assert(i==(s32)(init?path_index:(path_index^1))&&room==-7);calls[step++]=2;return 0x1234;}
s32 randomizer_drc_heat_boss(u32 s){assert(s==3);calls[step++]=4;return boss;}
u32 randomizer_drc_heat_particle(u32 control,u32 group,u32 id,u32 position,u32 angle,u32 scale,u32 opacity,u32 callback,s32 setup,u32 primary,u32 environment,u32 scale2){
 assert(control==0x123&&group==4&&id==0xC06D&&position==(u32)actor+0x314&&!angle&&!scale&&opacity==255&&!callback&&setup==-1&&!primary&&!environment&&!scale2);
 assert(*(f32*)(actor+0x314)==1&&*(f32*)(actor+0x318)==2&&*(f32*)(actor+0x31C)==3);calls[step++]=5;return success?(u32)emit:0;}
void randomizer_drc_heat_param(u32 callback,f32 value){assert(callback==(u32)&cb&&value==-3&&*(uint32_t*)(emit+0x1E8)==(uint32_t)(uintptr_t)&cb);calls[step++]=6;}
void randomizer_drc_heat_layer(u32 a){assert(a==(u32)actor);calls[step++]=7;if(mutate)emitter=(u32)other;}
int main(void){for(init=0;init<2;init++)for(type=0;type<16;type++)for(path_index=0;path_index<256;path_index++)for(stage=0;stage<2;stage++)for(boss=0;boss<2;boss++)for(existing=0;existing<2;existing++)for(success=0;success<2;success++)for(mutate=0;mutate<2;mutate++){
 memset(actor,0,sizeof(actor));memset(play,0,sizeof(play));memset(emit,0,sizeof(emit));memset(other,0,sizeof(other));step=0;alpha=19;flag=23;path=0x999;
 *(uint32_t*)(actor+0xB0)=(type<<20)|(path_index<<8);*(uint32_t*)(actor+0x2E4)=init?8:0;actor[0x326]=(u8)-7;
 *(u32*)(play+0x5B2C)=(u32)player;*(u32*)(play+0x5AB0)=0x123;
 *(f32*)(player+0x314)=1;*(f32*)(player+0x318)=2;*(f32*)(player+0x31C)=3;emitter=existing?(u32)emit:0;
 unsigned stop=stage&&boss;assert(randomizer_drc_heat_create((u32)actor)==(stop?3:existing?3:success?4:5));
 assert(path==((init?path_index:(path_index^1))==255?0:0x1234));
 unsigned at=0;if(!init)assert(calls[at++]==1);if((init?path_index:(path_index^1))!=255)assert(calls[at++]==2);assert(calls[at++]==3);if(stage)assert(calls[at++]==4);
 if(!stop){if(existing){assert(flag==(type==1)&&alpha==19);}else{assert(calls[at++]==5);if(success){assert(calls[at++]==6&&calls[at++]==7);assert(alpha==(type==1?255:0)&&flag==(type==1));assert((mutate?other:emit)[0x247]==alpha);assert(*(f32*)(actor+0x3E8)==0.5f&&*(f32*)(actor+0x3EC)==0);}}}assert(at==step);
 }}
"""
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/'drc_heat.h').write_text(Path(__file__).with_name('drc_heat.h').read_text().replace('(volatile u32*)(actor','(volatile uint32_t*)(actor').replace('(volatile u32*)(emitter','(volatile uint32_t*)(emitter'))
            (root/'test.c').write_text(source)
            subprocess.run([os.environ.get("CC", "cc"),'-std=c11','-O2','-Wall','-Wextra','-Werror',str(root/'test.c'),'-o',str(root/'test')],check=True)
            subprocess.run([str(root/'test')],check=True)
    def test_stage_callback_pointer_capture_and_loop_reload(self):
        header=Path(__file__).with_name('drc_heat.h').read_text()
        helper=header[header.index('static int randomizer_drc_heat_stage'):header.index('#define RANDOMIZER_DRC_HEAT_STAGE')]
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef uint8_t u8;
static _Alignas(uintptr_t) u8 table[64],play[0x5200];
static char original[]="old",replacement[]="new",stage[]="new";
static u32 *literal;static unsigned count,mode;
static void resolve(u32 address){u32 *str=(u32*)address;count++;if(count<3){literal=str;return;}literal[0]=(u32)replacement;str[0]=mode==0?(u32)original:(u32)stage;}
static void* data(u32 a){return a==0x10043034?(void*)original:(void*)table;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static u32 wwhd_dComIfG_getGameInfo(void){return (u32)play;}
'''+helper+r'''
int main(void){*(u32*)(table+0x14)=(u32)resolve;
 for(mode=0;mode<2;mode++){count=0;assert(randomizer_drc_heat_stage()==1&&count==3);}
 strcpy(stage,"bad");mode=1;count=0;assert(randomizer_drc_heat_stage()==0&&count==3);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'test.c').write_text(source)
            subprocess.run([os.environ.get("CC", "cc"),'-std=c11','-O2','-Wall','-Wextra','-Werror',str(root/'test.c'),'-o',str(root/'test')],check=True)
            subprocess.run([str(root/'test')],check=True)
if __name__=='__main__':unittest.main()
