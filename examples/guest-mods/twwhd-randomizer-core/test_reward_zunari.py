"""Zunari reward callback order, raw IDs, repeat flags and independent scopes."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class ZunariReward(unittest.TestCase):
    def test_all_rewards_and_native_callback_boundaries(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint16_t u16;typedef int16_t s16;typedef uint8_t u8;typedef int8_t s8;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static _Alignas(16) u8 save[0x1300],play[0x6000],link[0x400],actor[0xB00],other[0xB00],morf[0xC0],cursor[0xC0];
#define WWHD_GAME_DATA(a,t) ((u32)save)
static u8 init_reward_zunari[1];static const u8 init_reward_ivan=0,init_reward_battlesquid[3]={0};
static u32 randomizer_ivan_actor,randomizer_battlesquid_actor;
static unsigned status,mode,step,present_calls,flag_calls,query_calls,fail_present;
static u32 captured_item,after_item,callback_actor,callback_target;
static u32 native_action(u32 target,u32 self){callback_actor=self;callback_target=target;assert(*(u8*)(actor+0xACB)==255);return 1;}
#define RANDOMIZER_ZUNARI_CALL(t,s) native_action(t,s)
static u32 wwhd_dComIfG_getGameInfo(void){return(u32)play;}
static void wwhd_dSv_event_c_onEventBit(u32 object,u32 bit){assert(object==(u32)save+0x644&&bit==0x6940);assert(step==3);step=4;flag_calls++;save[0x6AD]|=0x40;}
#include "reward_zunari.h"
#include "reward_present.h"
static s32 query(u8 item,s32 flag){if(randomizer_zunari_item_query(item,flag))return !!(save[0x6AD]&0x40);query_calls++;return 73;}
u16 randomizer_zunari_talk(u32 self){assert(self==(u32)actor);step=1;return status;}
void randomizer_zunari_camera_reset(u32 camera){assert(camera==(u32)actor+0x928);assert(step==1);step=2;if(mode==2)*(u32*)(actor+0x8EC)=0x285F;}
void randomizer_zunari_animation(u32 self,s8 animation){assert(self==(u32)actor&&!animation);assert(step==2);step=3;if(mode==3)*(u32*)(actor+0x8EC)=0x285F;}
s32 randomizer_zunari_shop_message(u32 m){assert(m==0x285F);return 1;}
u32 randomizer_zunari_buy_message(u32 items){assert(items==0x1234);return 99;}
s32 randomizer_zunari_staff(u32 manager,u32 name,u32 a,s32 i){assert(manager==(u32)play+0x52C4&&name==0x10021860&&!a&&!i);assert(!randomizer_zunari_actor);step=1;return 19;}
u32 randomizer_native_present_item(u32 p,u32 item,u32 f,u32 b,u32 r,u32 a,u32 scale){assert(step==2);assert(p==(u32)actor+0x314&&!f&&b==0xFFFFFFFFu&&r==(u32)(s32)-3&&!a&&!scale);step=3;captured_item=item;present_calls++;if(mode==4){randomizer_zunari_actor=(u32)other;*(u8*)(actor+0xACB)=0x11;}return fail_present?0xFFFFFFFFu:0xA123;}
u32 randomizer_zunari_present(u32 p,u32 i,u32 f,u32 b,u32 r,u32 a,u32 s){return randomizer_present_item(p,i,f,b,r,a,s);}
void randomizer_zunari_cut_end(u32 manager,s32 staff){assert(manager==(u32)play+0x52C4&&staff==19);assert(step==3||step==4||step==1);step=5;if(mode==5)*(u8*)(actor+0xAC8)=126;}
s32 randomizer_zunari_end_check(u32 manager,u32 name){assert(manager==(u32)play+0x52C4&&name==0x10021868&&step==5);*(u8*)(actor+0xACB)=after_item;return 1;}
s32 randomizer_zunari_received_query(u8 item,s32 flag){return query(item,flag);}
s32 randomizer_zunari_owned(u32 item){assert(item==0x78||item==0x30);return 1;}
s32 randomizer_zunari_event_query(u32 object,u32 flag){assert(object==(u32)save+0x644);(void)flag;return 0;}
s32 randomizer_zunari_shop_count(void){return 3;}

static void reset(void){memset(actor,0,sizeof(actor));memset(save,0,sizeof(save));memset(play,0,sizeof(play));memset(morf,0,sizeof(morf));memset(cursor,0,sizeof(cursor));*(u32*)(actor+0x3B4)=(u32)morf;*(u32*)(actor+0xABC)=(u32)cursor;*(u32*)(play+0x5B34)=(u32)link;*(u32*)(actor+0x978)=0x1234;*(u8*)(actor+0x326)=(u8)-3;*(f32*)(morf+0xB0)=0.5f;*(u16*)(play+0x52B8)=0x20;*(u8*)(actor+0xAC5)=7;*(u32*)(link+0x3B8)=0xFFFFFFFF;mode=step=present_calls=flag_calls=query_calls=fail_present=0;randomizer_zunari_actor=randomizer_zunari_query=0;}
int main(void){
 for(unsigned reward=0;reward<256;reward++){
  init_reward_zunari[0]=reward;
  for(mode=0;mode<4;){unsigned scenario=mode;reset();mode=scenario;status=0x12;*(u32*)(actor+0x8EC)=mode?0x1111:0x285F;assert(randomizer_zunari_talk01((u32)actor)==1);if(mode==0||mode>=2){assert(*(u8*)(actor+0xACB)==reward&&*(u8*)(actor+0xAC3)==4);}assert(step==3);assert((*(u32*)(link+0x3B8)&0x8000000)==0);assert(*(u16*)(play+0x52B8)==0x28);mode=scenario+1;}
  for(unsigned fail=0;fail<2;fail++)for(unsigned changed=0;changed<2;changed++){
   reset();fail_present=fail;mode=changed?4:0;*(u8*)(actor+0xACB)=reward;
   assert(randomizer_zunari_getdemo((u32)actor,0)==1);assert(captured_item==reward&&present_calls==1&&flag_calls==1&&(save[0x6AD]&0x40));assert(*(u8*)(actor+0xAC8)==1);assert(*(u32*)(play+0x52A0)==(fail?0:0xA123));assert(!randomizer_zunari_actor);
  }
  reset();*(u8*)(actor+0xAC8)=1;after_item=reward;*(s16*)(actor+0x922)=0;
  assert(randomizer_zunari_getdemo((u32)actor,0)==1);assert(*(u32*)(actor+0x8F4)==(reward==0x8C?0x2857:reward==0x78?0x288F:0x2860));assert(callback_target==0x022DA748&&callback_actor==(u32)actor);assert(*(u8*)(actor+0xACB)==255);
 }
 for(unsigned flag=0;flag<2;flag++){reset();save[0x6AD]=flag?0x40:0;*(s32*)(actor+0x8FC)=-1;assert(randomizer_zunari_message((u32)actor)==(flag?0x2840:0x285D));assert(query_calls==0&&!randomizer_zunari_query);*(u32*)(actor+0x8F4)=0x1234;assert(randomizer_zunari_message((u32)actor)==0x1234);assert(!randomizer_zunari_query);}
 reset();randomizer_zunari_actor=(u32)other;*(u8*)(actor+0xAC8)=255;assert(randomizer_zunari_getdemo((u32)actor,0)==1);assert(randomizer_zunari_actor==(u32)other);
 reset();assert(query(0x2A,1)==73);randomizer_zunari_query=1;
 assert(query(0x2A,0)==73&&randomizer_zunari_query);assert(query(0x2B,1)==73&&randomizer_zunari_query);assert(query(0x2A,1)==0&&!randomizer_zunari_query);assert(query(0x2A,1)==73);
 randomizer_zunari_actor=(u32)actor;assert(!randomizer_zunari_present_source((u32)actor+0x318,init_reward_zunari[0],init_reward_zunari[0]));
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'test.c').write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).resolve().parent),str(root/'test.c'),'-o',str(root/'test')],check=True)
            subprocess.run([str(root/'test')],check=True)


if __name__=='__main__':
    unittest.main()
