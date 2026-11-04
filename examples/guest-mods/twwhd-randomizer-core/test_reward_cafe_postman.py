"""Café source predicates and post-order index/return-value preservation."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class CafePostman(unittest.TestCase):
    def test_full_conditions_and_callback_mutations(self):
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint16_t u16;typedef int16_t s16;typedef uint8_t u8;typedef int8_t s8;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static _Alignas(16) u8 actor[0xC00],before[0xC00],save[0x800];
#define WWHD_GAME_DATA(a,t) ((a)==1?(u32)save:(a)==0x10016F58?0xFFFFu:0x021FEF00u)
static unsigned source,story,repeat,actions,speaks,orders,sets,queries,mode;static s16 selected;
static u32 result;
static s32 wwhd_save_event_test(u32 bit){queries++;return bit==0x6A02?source:bit==0x1808?story:bit==0x1908?repeat:0;}
static void wwhd_dSv_event_c_onEventBit(u32 p,u32 bit){assert(p==(u32)save+0x644&&bit==0x6A02);assert(orders);sets++;}
#include "reward_cafe_postman.h"
s32 randomizer_cafe_set_action(u32 a,u32 p,u32 arg){assert(a==(u32)actor&&!arg);u32*x=(u32*)p;assert(x[0]==0xFFFF&&x[1]==0x021FEF00);actions++;return 0;}
u32 randomizer_cafe_speak(u32 a){assert(a==(u32)actor);speaks++;return result;}
u32 randomizer_cafe_order(u32 a,s16 event,u8 tool,u16 p,u16 priority,u16 flags){assert(a==(u32)actor&&event==selected&&tool==255&&p==65535&&!priority&&flags==1);orders++;if(mode==1)*(s16*)(actor+0x9DA)=0;else if(mode==2)*(s16*)(actor+0x9DA)=-1;return result;}
int main(void){
 for(source=0;source<2;source++)for(story=0;story<2;story++){
  actions=queries=0;assert(randomizer_cafe_initialize((u32)actor)==(!source&&story));assert(actions==(!source&&story));assert(queries==(source?1:2));
 }
 for(source=0;source<2;source++)for(repeat=0;repeat<2;repeat++)for(unsigned shortcut=0;shortcut<256;shortcut++){
  actor[0x9EC]=shortcut;queries=0;assert(randomizer_cafe_message((u32)actor)==(shortcut?0x1EF7:source?0x1EF9:repeat?0x1EF2:0x1EF0));assert(queries==(shortcut?0:source?1:2));
 }
 const u32 results[]={0,1,0xFFFFFFFFu,0x1234};
 for(unsigned raw=0;raw<256;raw++)for(unsigned type=0;type<16;type++)for(mode=0;mode<3;mode++)for(unsigned r=0;r<4;r++){
  memset(actor,0xA5,sizeof(actor));actor[0xA71]=raw;actor[0xA76]=type;*(u16*)(actor+0xFA)=0x8000;*(s16*)(actor+0x9DA)=-42;result=results[r];orders=speaks=sets=0;
  s8 condition=(s8)raw;s16 index=condition>=3?(s16)(condition-3):-42;
  if(condition>=3){if(index<4)*(s16*)(actor+0x9D2+(u32)index*2)=(s16)(index*13-200);selected=index==4?4:*(s16*)(actor+0x9D2+(u32)index*2);}
  memcpy(before,actor,sizeof(actor));u32 returned=randomizer_cafe_event_order((u32)actor);
  if(condition==1||condition==2){assert(*(u16*)(actor+0xFA)==(type==9?0x8021:0x8001));assert(speaks==(condition==1));assert(returned==(condition==1?result:(u32)actor));assert(!orders&&!sets);}
  else if(condition>=3){assert(orders==1&&!speaks);assert(sets==(mode==1||(!mode&&!index)));assert(*(s16*)(actor+0x9DA)==(mode==1?0:mode==2?-1:index));assert(returned==result);}
  else{assert(!memcmp(actor,before,sizeof(actor)));assert(returned==(u32)actor);assert(!orders&&!speaks&&!sets);}
 }
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'test.c').write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).resolve().parent),str(root/'test.c'),'-o',str(root/'test')],check=True)
            subprocess.run([str(root/'test')],check=True)


if __name__=='__main__':
    unittest.main()
