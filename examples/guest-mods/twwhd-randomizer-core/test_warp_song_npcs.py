"""Full Tott/Makar initialization, exact PMF/callback order and independent flags."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class WarpSongNpcs(unittest.TestCase):
    def test_complete_npc_consumers(self):
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint16_t u16;typedef int16_t s16;typedef uint8_t u8;typedef int8_t s8;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 memory[0x80000],actor_before[0x1200],save_before[0x2000];
static const u32 self=0x1000,info=0x10000,save=0x20000,value_ptr=0x23000;
#define DATA(a) (0x50000u+((a)&0x1FFFFu))
static void *at(u32 a){return a<sizeof memory?memory+a:(void*)a;}
#define R_WARP_NPC_u32(a) (*(uint32_t*)at(a))
#define R_WARP_NPC_s32(a) (*(int32_t*)at(a))
#define R_WARP_NPC_u16(a) (*(uint16_t*)at(a))
#define R_WARP_NPC_s16(a) (*(int16_t*)at(a))
#define R_WARP_NPC_u8(a) (*(uint8_t*)at(a))
#define R_WARP_NPC_s8(a) (*(int8_t*)at(a))
#define R_WARP_NPC_f32(a) (*(float*)at(a))
#define R_WARP_NPC(t,a) R_WARP_NPC_##t(a)
#define WWHD_ADDR_save_info_pointer 0x101F84DC
#define WWHD_GAME_DATA(a,t) R_WARP_NPC(t,DATA(a))
#define WARP_NPC_DATA(a) DATA(a)
static unsigned trace[32],n,events,actions,queries,ship_checks,mutate,substance_present,checks;
static s32 event_answer,ship_answer;static u32 action0,action1;
static void record(unsigned id){assert(n<32);trace[n++]=id;}
static u32 wwhd_dComIfG_getGameInfo(void){record(1);return info;}
#include "warp_song_npcs.h"
void warp_npc_dance(u32 actor,s32 force){record(2);assert(actor==self&&force==0);assert((R_WARP_NPC(u8,self+0x1111)&4)&&R_WARP_NPC(s16,self+0x1112)==0);R_WARP_NPC(s32,self+0x111C)=-17;}
u32 warp_npc_substance(u32 manager,s32 staff,u32 name,s32 type){record(3);assert(manager==info+0x52C4&&staff==-17&&name==DATA(0x10022F94)&&type==3);return substance_present?value_ptr:0;}
s32 warp_npc_event_index(u32 manager,u32 name,s32 map){record(4);assert(manager==info+0x52C4&&name==0x8000+events&&map==255);return 0x10000+(s32)events++-2;}
void warp_npc_makar_action(u32 actor,u32 pmf,u32 arg){record(5);assert(actor==self&&!arg);u32 *words=(u32*)pmf;action0=words[0];action1=words[1];actions++;WWHD_GAME_DATA(0x101BDC2A,u16)=0xA5FF;}
s32 warp_npc_makar_ship(u32 actor){record(6);assert(actor==self);ship_checks++;return ship_answer;}
s32 warp_npc_event(u32 bits,u32 flag){record(7);assert(bits==save+0x644&&flag==0x1610);queries++;if(mutate){R_WARP_NPC(u32,self+0xB0)=3;R_WARP_NPC(u32,self+0x3BC)=0xD0;}return event_answer;}
void warp_npc_status(u32 status,u32 weight,u32 type,u32 actor){record(8);assert(status==self+0x7C0&&weight==0xFE&&type==255&&actor==self);}
void warp_npc_cylinder(u32 cylinder,u32 source){record(9);assert((cylinder==self+0x7FC&&source==DATA(0x101BD958))||(cylinder==self+0x92C&&source==DATA(0x101BD99C)));if(cylinder==self+0x92C)assert(R_WARP_NPC(u32,self+0x840)==self+0x7C0);}
static void reset(void){memset(memory,0xA5,sizeof memory);n=events=actions=queries=ship_checks=mutate=substance_present=0;event_answer=ship_answer=0;WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)=save;
 for(unsigned i=0;i<5;i++)R_WARP_NPC(u32,DATA(0x101BD9E0)+i*4)=0x8000+i;
 R_WARP_NPC(u32,DATA(0x10018C48))=0x12345678;R_WARP_NPC(u32,DATA(0x10018C48)+4)=0x23456789;
 R_WARP_NPC(u32,DATA(0x10018BE0))=0x3456789A;R_WARP_NPC(u32,DATA(0x10018BE0)+4)=0x456789AB;
 WWHD_GAME_DATA(0x10466C08,u32)=0xFF812345;memcpy(save_before,memory+save,sizeof save_before);
}
int main(void){
 for(unsigned present=0;present<2;present++)for(s32 value=-512;value<=512;value++){
  reset();substance_present=present;R_WARP_NPC(s32,value_ptr)=value;memcpy(actor_before,memory+self,sizeof actor_before);
  randomizer_tott_pattern_init(self);unsigned expected[]={2,1,3};assert(n==3&&!memcmp(trace,expected,sizeof expected));
  assert(R_WARP_NPC(u8,self+0x1116)==(present?(u8)value:1));
  for(unsigned i=0;i<sizeof actor_before;i++)assert(i==0x1111||(i>=0x1112&&i<0x1114)||i==0x1116||(i>=0x111C&&i<0x1120)||actor_before[i]==memory[self+i]);checks++;
 }
 const s32 parameters[]={INT_MIN,-3,-1,0,1,2,3,4,5,6,7,INT_MAX};
 for(unsigned p=0;p<sizeof parameters/sizeof *parameters;p++)for(s32 event=-1;event<=2;event++)for(s32 ship=-1;ship<=2;ship++)for(unsigned change=0;change<2;change++){
  reset();const s32 parameter=parameters[p];R_WARP_NPC(s32,self+0xB0)=parameter;R_WARP_NPC(u32,self+0x3BC)=0xA5000000;event_answer=event;ship_answer=ship;mutate=change;memcpy(actor_before,memory+self,sizeof actor_before);
  assert(randomizer_makar_init(self)==1);const unsigned music=parameter==1||parameter==2||parameter==5,boss=parameter==0;
  const unsigned ship_test=!music&&!boss&&parameter!=3&&parameter!=4,ride=ship_test&&ship!=0,ordinary=!music&&!boss&&!ride;
  assert(events==5&&ship_checks==ship_test&&queries==ordinary&&actions==!ride);
  if(actions)assert(action0==(boss?0xFFFFu:music?0x12345678u:0x3456789Au)&&action1==(boss?0x02223048u:music?0x23456789u:0x456789ABu));
  assert(WWHD_GAME_DATA(0x101BDC2C,u8)==0&&WWHD_GAME_DATA(0x101BDC2D,u8)==0&&WWHD_GAME_DATA(0x101BDC28,s16)==0);
  assert(WWHD_GAME_DATA(0x101BDC2A,u16)==(ride?0:ordinary?0xA5EF:0xA5FF));
  const u32 initial=change&&ordinary?0xD0u:0xA5000040u;
  const u32 flags=(ordinary&&event?initial&~0x40u:initial)|((ordinary&&(change||parameter==3))?2:0);
  assert(R_WARP_NPC(u32,self+0x3BC)==flags);
  assert(R_WARP_NPC(u8,self+0x389)==(ride?0xAF:0xAB)&&R_WARP_NPC(u8,self+0x38B)==(ride?0xAF:0xAB)&&R_WARP_NPC(u8,self+0x38C)==8);
  assert(R_WARP_NPC(u32,self+0x104)==0x0221EC9C&&R_WARP_NPC(u32,self+0x100)==0x0221ED00);
  assert(R_WARP_NPC(s8,self+0xAEB)==-1&&R_WARP_NPC(s8,self+0xAE5)==-1);
  for(unsigned i=0;i<5;i++)assert(R_WARP_NPC(s16,self+0xAEC+i*2)==(s16)((s32)i-2));
  if(music)assert(R_WARP_NPC(s8,self+0x2FE)==-1&&R_WARP_NPC(s8,self+0x326)==-1&&R_WARP_NPC(f32,self+0x374)==-0.1f);
  else assert(R_WARP_NPC(u32,self+0x374)==0xFF812345);
  unsigned expected[32],length=0;for(unsigned i=0;i<5;i++){expected[length++]=1;expected[length++]=4;}
  if(ship_test)expected[length++]=6;if(ordinary)expected[length++]=7;if(actions)expected[length++]=5;expected[length++]=8;expected[length++]=9;expected[length++]=9;
  assert(n==length&&!memcmp(trace,expected,length*sizeof *expected));assert(!memcmp(save_before,memory+save,sizeof save_before));
  for(unsigned i=0;i<sizeof actor_before;i++){
   const int allowed=(i>=0x374&&i<0x378)||i==0x389||i==0x38B||i==0x38C||(i>=0xAEC&&i<0xAF6)||i==0xAEB||i==0xAE5||(i>=0x100&&i<0x108)||(i>=0x3BC&&i<0x3C0)||(i>=0x840&&i<0x844)||(i>=0x970&&i<0x974)||(music&&(i==0x2FE||i==0x326))||(ordinary&&change&&i>=0xB0&&i<0xB4);
   assert(allowed||actor_before[i]==memory[self+i]);
  }checks++;
 }
 printf("warp NPCs: %u complete initialization cases passed\n",checks);return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            c,exe=Path(directory)/'test.c',Path(directory)/'test'
            c.write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
