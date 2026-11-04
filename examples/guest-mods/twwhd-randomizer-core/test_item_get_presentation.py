"""Test temporary presentation inputs and restoration around the native routine."""
from pathlib import Path
import os, subprocess, tempfile, unittest

class ItemGetPresentation(unittest.TestCase):
    def test_native_arguments_and_restore(self):
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_HOOK_RETURN(a,n,p) void n p
static _Alignas(16) u8 actor[0x500],info[0x6000],save[0x1000];
static unsigned change_parameter,change_event,calls,late_parameter;
static u32 seen_parameter;static u8 seen_event;
static const unsigned init_progressive_magic_always_double=0;
#include "progressive_ids.h"
static u32 randomizer_display_item(u32 item){
 unsigned field=rando_progressive_field((unsigned)item);
 return field==RANDO_NO_PROGRESSIVE_FIELD?item:rando_progressive_display((unsigned)item,save[field],init_progressive_magic_always_double);
}
u32 wwhd_dComIfG_getGameInfo(void){return (u32)info;}
#include "item_get_presentation.h"
s32 randomizer_native_get_item_init(u32 self){
 assert(self==(u32)actor);++calls;
 if(late_parameter){*(u32*)(actor+0x428)=0x100;info[0x52A4]=0x38;randomizer_after_item_get_animation(self);randomizer_after_item_get_unequip(self);}
 seen_parameter=*(u32*)(actor+0x428);seen_event=info[0x52A4];
 if(change_parameter)*(u32*)(actor+0x428)=99;
 if(change_event)info[0x52A4]=42;
 return 77;
}
static void reset(u32 param,u8 event){
 memset(actor,0,sizeof(actor));memset(info,0,sizeof(info));memset(save,0,sizeof(save));
 *(u32*)(actor+0x428)=param;info[0x52A4]=event;
 save[0xD4]=1;save[0xD5]=1;save[0x7D]=3;
 change_parameter=change_event=calls=late_parameter=0;
}
int main(void){
 const unsigned inputs[]={0,0x38,0x3E,0x3B,0x27,5,0xFF};
 const unsigned outputs[]={0,0x39,0x39,0x3C,0x36,5,0xFF};
 for(unsigned i=0;i<sizeof(inputs)/sizeof(*inputs);i++){
  reset(inputs[i],8);assert(randomizer_get_item_init((u32)actor)==77);
  assert(calls==1 && seen_parameter==outputs[i] && seen_event==8);
  assert(*(u32*)(actor+0x428)==inputs[i] && info[0x52A4]==8);
  reset(0x100,(u8)inputs[i]);assert(randomizer_get_item_init((u32)actor)==77);
  assert(calls==1 && seen_parameter==0x100 && seen_event==outputs[i]);
  assert(*(u32*)(actor+0x428)==0x100 && info[0x52A4]==inputs[i]);
 }
 reset(0x38,8);change_parameter=1;randomizer_get_item_init((u32)actor);assert(*(u32*)(actor+0x428)==99);
 reset(0x100,0x38);change_event=1;randomizer_get_item_init((u32)actor);assert(info[0x52A4]==42);
 reset(0,8);late_parameter=1;randomizer_get_item_init((u32)actor);
 assert(seen_parameter==0x100 && seen_event==0x39 && info[0x52A4]==0x38);
 assert(*(u32*)(actor+0x428)==0x100);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);(p/'test.c').write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                            '-I',str(Path(__file__).resolve().parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)

if __name__=='__main__': unittest.main()
