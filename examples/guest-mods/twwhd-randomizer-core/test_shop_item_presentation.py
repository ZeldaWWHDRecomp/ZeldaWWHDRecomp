"""Check that model construction cannot change the shop's raw slot identity."""
from pathlib import Path
import os, subprocess, tempfile, unittest

class ShopPresentation(unittest.TestCase):
    def test_model_tier_preserves_slot_and_phase(self):
        source=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef float f32;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_HOOK(a,n,p) void n p
static _Alignas(16) u8 shopdata[8192];
#define WWHD_GAME_DATA(a,t) (*(t*)shopdata)
static u8 actor[0x800],save[0x1000];
static s32 phase;static unsigned calls,change_parameter;static u8 received;
#include "progressive_ids.h"
static u32 randomizer_display_item(u32 item){
 unsigned field=rando_progressive_field((unsigned)item);
 return field==RANDO_NO_PROGRESSIVE_FIELD?item:rando_progressive_display((unsigned)item,save[field],0);
}
#include "shop_item_presentation.h"
s32 randomizer_native_shop_create(u32 self){
 assert(self==(u32)actor);++calls;received=actor[0xB3];
 assert(actor[0xB0]==0xA5 && actor[0xB1]==0x7F && actor[0xB2]==2);
 actor[0x74E]=received==255?1:received;
 if(change_parameter)actor[0xB3]=6;
 return phase;
}
int main(void){
 const u8 raw[]={0x38,0x3E,0x3B,0x27,5,255},display[]={0x39,0x39,0x3C,0x36,5,255};
 for(unsigned i=0;i<sizeof(raw);i++)for(phase=0;phase<=4;phase++){
  memset(actor,0,sizeof(actor));memset(save,0,sizeof(save));
  save[0xD4]=1;save[0xD5]=1;save[0x7D]=3;
  actor[0xB0]=0xA5;actor[0xB1]=0x7F;actor[0xB2]=2;actor[0xB3]=raw[i];calls=0;
  assert(randomizer_shop_create((u32)actor)==phase);
  assert(calls==1 && received==display[i] && actor[0xB3]==raw[i]);
  assert(actor[0x74E]==(display[i]==255?1:display[i]));
 }
 *(float*)(shopdata+0x10)=7.0f;
 randomizer_shop_model_offsets();
 for(unsigned i=0;i<256;i++) {
  const float expected=i==0?7.0f:(i>=0x10 && i<=0x12)?0.0f:20.0f;
  assert(*(float*)(shopdata+i*32+16)==expected);
 }
 change_parameter=1;randomizer_shop_create((u32)actor);assert(actor[0xB3]==6);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);(p/'test.c').write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                            '-I',str(Path(__file__).resolve().parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)

if __name__=='__main__': unittest.main()
