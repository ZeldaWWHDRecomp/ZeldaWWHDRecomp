"""Exercise talk gates, animation branches and late reward-flag updates."""
from pathlib import Path
import os, subprocess, tempfile, unittest

class LenzoReward(unittest.TestCase):
    def test_gates_and_flag_priority(self):
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef uint16_t u16;typedef uint8_t u8;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static u8 actor[0xC00],info[0x6000],animation[16];
#define WWHD_GAME_DATA(a,t) (*(t*)animation)
static const u8 init_reward_lenzo[]={0x27,0x38};
static unsigned marks;
static unsigned common_calls,present_calls,talk_calls,mode_calls,table_calls,tag_calls;
static unsigned present_end,late_flag;static u16 talk_status;
u8 wwhd_daNpcPhoto_c__executeCommon(void* p){assert(p==actor);++common_calls;return 1;}
u32 wwhd_dComIfG_getGameInfo(void){return (u32)info;}
u16 wwhd_daNpcPhoto_c__talk2(void* p,int mode){assert(p==actor && mode==1);++talk_calls;return talk_status;}
void wwhd_daNpcPhoto_c__executeSetMode(void* p,u32 mode){assert(p==actor && !mode && !actor[0xB30]);++mode_calls;if(late_flag)actor[0xB3A]=2;}
u8 wwhd_daNpcPhoto_c__setAnmTbl(void* p,void* table){assert(p==actor && table==animation);++table_calls;return 1;}
void wwhd_daNpcPhoto_c__setAnmFromMsgTag(void* p){assert(p==actor);++tag_calls;}
int wwhd_save_event_set(u32 bit,u32 value){assert(bit==0x6920 && value==1);++marks;return 0;}
#include "reward_lenzo.h"
u32 randomizer_lenzo_present_end(u32 manager){assert(manager==(u32)info+0x52C4);++present_calls;return present_end;}
static void reset(unsigned flags,unsigned button,unsigned end,u16 status){
 memset(actor,0,sizeof(actor));memset(info,0,sizeof(info));
 actor[0xB30]=1;actor[0xB32]=9;actor[0xB39]=0xAA;actor[0xB3A]=(u8)flags;actor[0xB3D]=0x80;
 info[0x52B0]=(u8)button;present_end=end;talk_status=status;late_flag=0;
 marks=0;common_calls=present_calls=talk_calls=mode_calls=table_calls=tag_calls=0;
}
int main(void){
 const unsigned buttons[]={0,1,4,5,255};const u16 states[]={0xE,0x12,0xFFFF};
 for(unsigned b=0;b<5;b++)for(unsigned end=0;end<2;end++)
 for(unsigned s=0;s<3;s++)for(unsigned flags=0;flags<256;flags++){
  reset(flags,buttons[b],end,states[s]);randomizer_lenzo_talk((u32)actor);
  const unsigned valid=buttons[b]>=1 && buttons[b]<=4,allowed=!valid || end;
  assert(common_calls==1 && present_calls==valid && talk_calls==allowed);
  const unsigned closed=allowed && states[s]==0x12;
  assert(mode_calls==closed && tag_calls==(allowed && !closed));
  assert(marks==(closed && (flags&1)!=0));
  unsigned f=flags,item=0xAA,mode=9,extra=0x80,table=0;
  if(closed){
   if(flags&1){f&=~1u;item=0x27;mode=4;}
   else if(flags&2){f&=~2u;item=0x38;mode=4;}
   else if(flags&4){f&=~4u;mode=3;extra|=4;table=1;}
   else f|=0x40;
  }
  assert(actor[0xB30]==!closed && actor[0xB3A]==f && actor[0xB39]==item);
  assert(actor[0xB32]==mode && actor[0xB3D]==extra && table_calls==table);
 }
 reset(0,0,0,0x12);late_flag=1;randomizer_lenzo_talk((u32)actor);
 assert(actor[0xB39]==0x38 && actor[0xB32]==4 && actor[0xB3A]==0);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);(p/'test.c').write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                            '-I',str(Path(__file__).resolve().parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)

if __name__=='__main__': unittest.main()
