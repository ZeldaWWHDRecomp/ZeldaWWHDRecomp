"""Salvage Corp source flag, constructor boundaries and live callback reloads."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class SalvageCorp(unittest.TestCase):
    def test_constructor_and_presentation(self):
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint16_t u16;typedef int16_t s16;typedef uint8_t u8;typedef int8_t s8;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static _Alignas(16) u8 actor[0x860],expected[0x860],save[2][0x800],play[0x6000];
static u32 save_ptr;static s32 gift;
#define WWHD_GAME_DATA(a,t) ((a)==1?save_ptr:(u32)(s32)gift)
static unsigned flag,alloc_fail,base_calls,alloc_calls,step,parameter,mutate;
static s32 new_item,new_room,new_gift,received;static u32 return_pid;
static s32 wwhd_save_event_test(u32 bit){assert(bit==0x6980);assert(base_calls);return flag;}
static u32 wwhd_dComIfG_getGameInfo(void){return(u32)play;}
static void wwhd_dSv_event_c_onEventBit(u32 p,u32 bit){assert(step==2&&bit==0x6980&&p==save_ptr+0x644);assert(*(u32*)(play+0x52A0)==0xABCD);step=3;*(u8*)(p+0x69)|=0x80;}
#include "reward_salvage_corp.h"
u32 randomizer_salvage_allocate(u32 size){assert(size==0x860);alloc_calls++;return alloc_fail?0:(u32)actor;}
void randomizer_salvage_base_ctor(u32 a){assert(a==(u32)actor);base_calls++;if(mutate){*(u8*)(actor+0x2DD)=3;*(s16*)(actor+0x2FA)=-123;}}
u8 randomizer_salvage_number(u32 a){assert(a==(u32)actor&&*(u32*)(actor+0xB4)==0x100226A4);s32 n=*(s8*)(actor+0x2DD);return(u32)n<4?(u8)n:0;}
u32 randomizer_salvage_substance(u32 m,s32 staff,const char*name,s32 type){assert(m==(u32)play+0x52C4&&staff==-7&&!strcmp(name,"ItemNo")&&type==3);assert(step==0);step=1;gift=new_gift;*(s32*)(actor+0x828)=new_item;*(s8*)(actor+0x326)=(s8)new_room;return parameter?0xDEADBEEF:0;}
u32 randomizer_salvage_present(u32 p,s32 item,u32 f,s32 bit,s32 room,u32 a,u32 scale){assert(step==1&&p==(u32)actor+0x314&&!f&&bit==-1&&room==(s8)new_room&&!a&&!scale);step=2;received=item;save_ptr=(u32)save[1];gift=999;return return_pid;}
int main(void){
 save_ptr=(u32)save[0];
 for(unsigned argument=0;argument<256;argument++)for(flag=0;flag<2;flag++)for(mutate=0;mutate<2;mutate++){
  memset(actor,0xA5,sizeof(actor));*(u8*)(actor+0x2DD)=argument;*(s16*)(actor+0x2FA)=123;memcpy(expected,actor,sizeof(actor));base_calls=0;
  if(mutate){*(u8*)(expected+0x2DD)=3;*(s16*)(expected+0x2FA)=-123;}
  *(u32*)(expected+0xB4)=0x100226A4;expected[0x851]=0;expected[0x84F]=mutate?3:argument<4?argument:0;*(s16*)(expected+0x83C)=mutate?-123:123;
  expected[0x856]=expected[0x830]=1;expected[0x84D]=expected[0x84E]=expected[0x855]=0;*(s32*)(expected+0x82C)=0;*(u16*)(expected+0x848)=flag?0x20:0;*(f32*)(expected+0x820)=-1;
  assert(randomizer_salvage_constructor((u32)actor)==(u32)actor&&base_calls==1);assert(!memcmp(actor,expected,sizeof(actor)));
 }
 alloc_fail=1;base_calls=alloc_calls=0;assert(!randomizer_salvage_constructor(0)&&alloc_calls==1&&!base_calls);alloc_fail=0;assert(randomizer_salvage_constructor(0)==(u32)actor&&base_calls==1);
 const s32 values[]={-1,0,202,255,0x12345678};
 for(parameter=0;parameter<2;parameter++)for(unsigned i=0;i<5;i++)for(unsigned failed=0;failed<2;failed++){
  step=0;new_item=values[i];new_gift=values[4-i];new_room=i==0?-128:i==1?-3:i==2?0:i==3?44:127;return_pid=failed?0xFFFFFFFFu:0x3456;save_ptr=(u32)save[0];memset(save,0,sizeof(save));*(u32*)(play+0x52A0)=0xABCD;
  randomizer_salvage_get_item((u32)actor,-7);assert(received==(parameter?new_gift:new_item));assert(step==3);assert(save[1][0x6AD]==0x80&&save[0][0x6AD]==0);assert(*(u32*)(play+0x52A0)==(failed?0xABCD:0x3456));
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
