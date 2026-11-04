"""Rotation byte selection, native fallbacks and post-spawn damage callbacks."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class CustomDrops(unittest.TestCase):
    def test_all_rotation_values_and_callback_reloads(self):
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint16_t u16;typedef int16_t s16;typedef uint8_t u8;typedef int8_t s8;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 actor[0xA00],effect_table[0x90],vtable[32];static f32 one=1.0f;static char file[]="file",condition[]="condition";
static void* data(u32 a){return a==0x10040CE0?effect_table:a==0x1002A9E0?(void*)&one:a==0x10040CAC?file:condition;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static unsigned stone,mode,step,created,custom_call,killed,pillars,asserts,effects,sounds;static s32 table_id,bit_id,expected_action;static u32 received,expected_custom;
static void effect_call(u32 target,u32 self){assert(self==(u32)actor);assert(target==0x1234||target==0x5678);assert(step==5||step==6||step==7);effects++;step=8;*(u32*)(actor+0x2E0)=0x100000;}
#define RANDOMIZER_DROP_EFFECT_CALL(t,s) effect_call(t,s)
#include "custom_item_drops.h"
s32 randomizer_pot_parameter(u32 a,u32 width,u32 shift){assert(!stone&&a==(u32)actor);if(width==6){assert(shift==0&&step==0);step=1;return table_id;}assert(width==7&&shift==16&&step==1);step=2;if(mode==3)*(u16*)(actor+0x798)=0x42AA;return bit_id;}
s32 randomizer_stone_parameter(u32 a,u32 width,u32 shift){assert(stone&&a==(u32)actor);if(width==6){assert(shift==12&&step==1);step=2;return table_id;}assert(width==7&&shift==0&&step==2);step=3;if(mode==3)*(u16*)(actor+0x2F8)=0xAA42;return bit_id;}
void randomizer_drop_angles(u32 p,s16 x,s16 y,s16 z){assert(x==0&&z==0);s16*a=(s16*)p;a[0]=x;a[1]=y;a[2]=z;if(stone){assert(step==0&&y==0);step=1;}else{assert(step==2&&y==1337);step=3;}if(mode==1){if(stone)*(u16*)(actor+0x2F8)=0xAA42;else *(u16*)(actor+0x798)=0x42AA;}}
static u32 create(u32 p,s32 item,s32 bit,s32 room,s32 type,u32 angles,s32 action,u32 scale,unsigned custom){assert(step==3&&p==(u32)actor+0x314&&bit==bit_id&&room==-3);assert(*(s16*)angles==0&&*((s16*)angles+1)==(stone?0:1337)&&*((s16*)angles+2)==0);if(stone){f32*s=(f32*)scale;assert(s[0]==one&&s[1]==one&&s[2]==one);}else assert(!scale);assert(type==(custom?3:0)&&action==(custom?5:expected_action));assert(item==(s32)(custom?expected_custom:(u32)table_id));created++;custom_call=custom;received=(u32)item;step=4;return 0xFFFFFFFFu;}
u32 randomizer_drop_table(u32 p,s32 i,s32 b,s32 r,s32 t,u32 a,s32 action,u32 scale){return create(p,i,b,r,t,a,action,scale,0);}
u32 randomizer_drop_item(u32 p,s32 i,s32 b,s32 r,s32 t,u32 a,s32 action,u32 scale){return create(p,i,b,r,t,a,action,scale,1);}
void randomizer_pot_cancel_carry(u32 a){assert(a==(u32)actor&&step==4);step=5;*(u32*)(actor+0x2E0)=0xFFFFFFFFu;}
void randomizer_pot_kill_special(u32 a){assert(a==(u32)actor&&step==5&&(*(u32*)(actor+0x2E0)&0x100000)==0);killed++;step=6;}
u32 randomizer_pot_break_pillar(s16 t,s32 r,u32 p,u32 scale,u32 cb){assert(t==0x17&&r==-7&&p==0xDEAD&&!scale&&!cb&&step==5);pillars++;step=6;*(s32*)(actor+0x790)=mode==4?19:3;return 0;}
void randomizer_pot_assert(u32 f,s32 line,u32 c){assert(f==(u32)file&&line==0xD62&&c==(u32)condition&&step==6);asserts++;step=7;*(s32*)(actor+0x790)=3;}
void randomizer_pot_break_sound(u32 a,u32 poly){assert(a==(u32)actor&&poly==0x1111&&step==8);sounds++;step=9;}
static void setup(unsigned rotation,unsigned is_stone){memset(actor,0xA5,sizeof(actor));memset(effect_table,0,sizeof(effect_table));stone=is_stone;step=created=custom_call=killed=pillars=asserts=effects=sounds=0;*(u16*)(actor+0x798)=rotation;*(u16*)(actor+0x2F8)=rotation;*(s16*)(actor+0x2FA)=1337;*(s8*)(actor+0x2FE)=-3;*(s8*)(actor+0x326)=-3;*(s8*)(actor+0x1C9)=-7;*(s32*)(actor+0x790)=0;}
int main(void){
 for(unsigned r=0;r<65536;r++)for(unsigned which=0;which<2;which++){
  setup(r,which);mode=0;table_id=(r>>10)&63;bit_id=r&127;expected_custom=which?(r&255):(r>>8);s32 action=(s32)(r&31)-8;expected_action=which?1:action<0?0:action>=16?15:action;
  if(which)randomizer_stone_make_item((u32)actor);else randomizer_pot_damaged((u32)actor,action,0x1111,0,0);
  assert(created==1&&custom_call==(expected_custom!=0&&expected_custom!=255));assert(killed==!which);assert(*(u16*)(actor+(which?0x2F8:0x798))==r);
 }
 for(unsigned which=0;which<2;which++)for(mode=1;mode<=3;mode+=2){setup(0,which);table_id=63;bit_id=-1;expected_custom=0x42;expected_action=which?1:0;if(which)randomizer_stone_make_item((u32)actor);else randomizer_pot_damaged((u32)actor,-1,0x1111,0,0);assert(created==1&&custom_call&&received==0x42);}
 for(unsigned virtual_call=0;virtual_call<2;virtual_call++)for(unsigned type=0;type<16;type++){
  setup(0,0);mode=0;table_id=63;bit_id=127;expected_custom=0;expected_action=15;*(s32*)(actor+0x790)=type;unsigned selected=type==2?3:type;u8*d=effect_table+selected*8;*(s16*)d=0;*(s16*)(d+2)=virtual_call?1:-1;if(virtual_call){*(u32*)(actor+0xB4)=(u32)vtable;*(s16*)(d+6)=0xB4;*(u32*)(vtable+12)=0x5678;}else *(u32*)(d+4)=0x1234;
  randomizer_pot_damaged((u32)actor,0x7FFFFFFF,0x1111,1,0xDEAD);assert(created==1&&effects==1&&sounds==1&&!killed&&pillars==(type==2));assert(*(u32*)(actor+0x2E0)==0x100000);
 }
 setup(0,0);mode=4;table_id=1;bit_id=1;expected_action=0;*(s32*)(actor+0x790)=2;u8*d=effect_table+3*8;*(s16*)d=0;*(s16*)(d+2)=-1;*(u32*)(d+4)=0x1234;randomizer_pot_damaged((u32)actor,-2147483647-1,0x1111,1,0xDEAD);assert(asserts==1&&effects==1&&sounds==1);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'test.c').write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).resolve().parent),str(root/'test.c'),'-o',str(root/'test')],check=True)
            subprocess.run([str(root/'test')],check=True)


if __name__=='__main__':
    unittest.main()
