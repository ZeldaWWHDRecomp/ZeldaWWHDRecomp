"""First-potion arguments, native completion order and the one bottle branch."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class DocBandam(unittest.TestCase):
    def test_rewards_and_callback_boundaries(self):
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint16_t u16;typedef int16_t s16;typedef uint8_t u8;typedef int8_t s8;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static _Alignas(16) u8 actor[0xB00],player[0x400],play[0x6000],save[0x800];
#define WWHD_GAME_DATA(a,t) ((u32)save)
static u8 init_reward_doc_bandam[2];static u32 message,second,received,pid;static unsigned mode,step,staffs,presents,cuts,shows,drugs,flags,actions,empty_calls,delegates;
static u32 native_action(u32 target,u32 self){assert(target==0x022311F4&&self==(u32)actor&&*(u8*)(actor+0xA60)==0);actions++;return 1;}
#define RANDOMIZER_DOC_CALL(t,s) native_action(t,s)
static u32 wwhd_dComIfG_getGameInfo(void){return(u32)play;}
static void wwhd_dSv_event_c_onEventBit(u32 e,u32 b){assert(e==(u32)save+0x644&&(b==0xD04||b==0xD02));assert(step==6);flags|=b&255;}
#include "reward_doc_bandam.h"
s32 randomizer_doc_staff(u32 manager,u32 name,u32 a,s32 i){assert(manager==(u32)play+0x52C4&&name==0x10019BD8&&!a&&!i);staffs++;step=1;if(mode==1)*(u32*)(actor+0x998)=7627;return 19;}
u32 randomizer_doc_present(u32 p,s32 item,u32 f,s32 b,s32 room,u32 a,u32 scale){assert(p==(u32)actor+0x314&&f==0&&b==-1&&room==-3&&!a&&!scale);assert(step==1);step=2;presents++;received=(u32)item;if(mode==2)*(u8*)(actor+0xA63)=0x49;return pid;}
void randomizer_doc_cut_end(u32 m,s32 staff){assert(m==(u32)play+0x52C4&&staff==19);cuts++;if(*(u8*)(actor+0xA60)==0){assert(step==2);step=3;if(mode==3)*(u8*)(actor+0xA60)=126;}else {assert(step==4);step=5;}}
void randomizer_doc_demo_message(void){assert(step==1);step=4;}
s32 randomizer_doc_end_check(u32 m,s16 e){assert(m==(u32)play+0x52C4&&e==-9&&step==5);if(mode==4)*(u8*)(actor+0x9A8)=0xFF;return 1;}
void randomizer_doc_show_items(u32 items){assert(items==(u32)actor+0xA04&&*(uint32_t*)(actor+0x9A4)==0x1DDA);shows++;step=6;*(u8*)(actor+0x9A8)=0x10;}
s32 randomizer_doc_create_drug(u32 code){assert(code==*(u8*)(actor+0xA63));drugs++;step=6;if(mode==5)*(u8*)(actor+0xA63)=0x4B;return mode==6;}
s32 randomizer_doc_empty_bottle(u32 items){assert(items==(u32)save+0x5C);empty_calls++;if(mode==7)message=0x1111;return mode&1;}
u32 randomizer_native_doc_next_message(u32 a,u32 m,u32 s){assert(a==(u32)actor&&m==(u32)&message&&s==(u32)&second);delegates++;return 0xA5A50100u;}
static void reset(void){memset(actor,0xA5,sizeof(actor));memset(play,0,sizeof(play));memset(player,0x55,sizeof(player));*(u32*)(play+0x5B2C)=(u32)player;*(u8*)(actor+0x326)=(u8)-3;*(s16*)(actor+0x9AE)=0;*(s16*)(actor+0xA56)=-9;*(u8*)(actor+0xA60)=0;mode=step=staffs=presents=cuts=shows=drugs=flags=actions=empty_calls=delegates=0;pid=0x3456;}
int main(void){
 for(unsigned reward=0;reward<256;reward++)for(unsigned bought=0;bought<256;bought++)for(unsigned fresh=0;fresh<2;fresh++){
  reset();init_reward_doc_bandam[0]=reward;init_reward_doc_bandam[1]=255-reward;*(u32*)(actor+0x998)=fresh?7627:7628;*(u8*)(actor+0x9A8)=bought;*(u8*)(actor+0xA63)=0x4A;
  assert(randomizer_doc_getdemo((u32)actor,0)==1);unsigned raw=bought==255?0x52:bought;assert(received==(fresh&&raw==0x52?reward:fresh&&raw==0x53?255-reward:raw));assert(presents==1&&cuts==1&&*(u8*)(actor+0xA60)==1);assert(*(u32*)(play+0x52A0)==pid);
 }
 for(unsigned code=0;code<256;code++)for(unsigned failed=0;failed<2;failed++){
  reset();*(u32*)(actor+0x998)=7626;mode=1;*(u8*)(actor+0x9A8)=255;*(u8*)(actor+0xA63)=code;pid=failed?0xFFFFFFFFu:0x3456;
  assert(randomizer_doc_getdemo((u32)actor,0)==1);unsigned raw=code==0x49?0x51:code==0x4A?init_reward_doc_bandam[0]:init_reward_doc_bandam[1];assert(received==raw);assert(*(u32*)(play+0x52A0)==(failed?0:pid));
 }
 for(unsigned scenario=0;scenario<7;scenario++)for(unsigned code=0x49;code<=0x4B;code++){
  reset();mode=scenario;*(u8*)(actor+0xA60)=1;*(u8*)(actor+0x9A8)=scenario&1?255:0x52;*(u8*)(actor+0xA63)=code;assert(randomizer_doc_getdemo((u32)actor,0)==1);assert(!presents&&cuts==1&&actions==1);assert(*(u8*)(actor+0xA4F)==1);
  unsigned fallback=(scenario&1)||scenario==4;assert(drugs==fallback&&shows==!fallback);assert(*(uint32_t*)(actor+0x9A4)==(!fallback?0x1DDA:scenario==6?0x1DC6:0x1DCD));unsigned finalcode=fallback&&scenario==5?0x4B:code;assert(flags==(finalcode==0x4A?4:finalcode==0x4B?2:0));assert(*(u16*)(play+0x52B8)&8);
 }
 for(unsigned m=0x1DC4;m<=0x1DCD;m++)for(unsigned scenario=0;scenario<8;scenario++){
  reset();mode=scenario;message=m;second=0xCAFE;u32 result=randomizer_doc_next_message((u32)actor,(u32)&message,(u32)&second);assert(second==0xCAFE);assert(result==(m==0x1DCB?0x10:0xA5A50100u));assert(empty_calls==(m==0x1DCB)&&delegates==(m!=0x1DCB));assert(message==(m==0x1DCB&&mode==7?0x1111:m));
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
