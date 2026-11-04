"""Bomb shop talk keeps cursor/message lifecycle with patched ownership path."""
import os
from pathlib import Path
import subprocess,tempfile,unittest
class BombShopTalk(unittest.TestCase):
 def test_messages(self):
  code=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int16_t s16;typedef uint16_t u16;typedef uint8_t u8;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static u8 actor[0xB00],manager[0xA00],cursor[0xC0];static u32 managerptr;static unsigned trigger,sent,handled,nextcalls,setcalls,messagecalls,democalls,statuscalls;static u32 status,recheck;
static void* data(u32 a){assert(a==0x101F4B5C);return&managerptr;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
#include "bombshop_talk.h"
s32 randomizer_bomb_trigger(u32 s,u32 i,u32 m,u32 f,u32 a){assert(s==(u32)actor+0x8F0&&i==(u32)actor+0x9B8&&m==(u32)actor+0x94C&&!f&&!a);assert(cursor[0xB4]==1);return trigger;}
u32 randomizer_bomb_message_status(u32 m){assert(m==(u32)manager);statuscalls++;return statuscalls==1?status:recheck;}
void randomizer_bomb_set_status(u32 m,u32 s){assert(m==(u32)manager&&s==19);setcalls++;}
u32 randomizer_bomb_next_message(u32 a,u32 c,u32 n){assert(a==(u32)actor&&c==a+0x950&&n==a+0x94C);assert(*(unsigned*)c==0x12345678);*(unsigned*)c=0xABCDEF01;nextcalls++;return 19;}
void randomizer_bomb_message_set(u32 m,u32 id,u32 p){assert(m==(u32)manager&&id==0xABCDEF01&&!p);messagecalls++;}
void randomizer_bomb_demo_flag(void){democalls++;}
int main(void){u32 states[]={0,1,14,15,18,19,0x1000E,0x1000F,0xFFFFFFFF};u32 checks[]={0,15,0x1000F,0xFFFFFFFF};
 for(unsigned s=0;s<9;s++)for(unsigned r=0;r<4;r++)for(trigger=0;trigger<2;trigger++)for(sent=0;sent<2;sent++)for(handled=0;handled<2;handled++){
 memset(actor,0,sizeof actor);memset(manager,0,sizeof manager);managerptr=(u32)manager;*(u32*)(actor+0x9FC)=(u32)cursor;*(unsigned*)(actor+0x94C)=0x12345678;*(unsigned*)(actor+0x950)=0x87654321;actor[0x918]=handled;manager[0x922]=sent;*(s16*)(actor+0x9B8)=-1;
 status=states[s];recheck=checks[r];nextcalls=setcalls=messagecalls=democalls=statuscalls=0;assert(randomizer_bombshop_talk((u32)actor)==(status&65535));unsigned st=status&65535,advance=(st==14||st==15)&&!trigger&&!handled&&!sent;
 assert(nextcalls==advance&&messagecalls==(advance&&recheck==15));assert(statuscalls==1+advance);assert(setcalls==advance+(st==18));assert(democalls==(st==1));
 if(st==14||st==15)assert(actor[0x918]==0);if(st==18)assert(*(u16*)(actor+0x9B8)==65535);
 }
return 0;}
'''
  with tempfile.TemporaryDirectory() as t:
   p=Path(t);(p/'test.c').write_text(code)
   subprocess.run([os.environ.get("CC", "cc"),'-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
