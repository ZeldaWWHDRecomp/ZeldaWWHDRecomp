"""Independent shortcut ownership, bit transitions and complete constructor contracts."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class DpadShortcuts(unittest.TestCase):
    def test_complete_shortcut_consumers(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef uint32_t u32;typedef int32_t s32;typedef uint8_t u8;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 memory[0x10000],before[0x80],save_before[0x200];
static const u32 self=0x100,info=0x1000,save=0x8000;
static u32 save_pointer=0x8000;
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_GAME_DATA(a,t) (*(t*)&save_pointer)
#define R_SHORTCUT(t,a) (*(volatile t*)(memory+(a)))
#define SHORTCUT_DATA(a) (a)
static unsigned n,info_reads,mutation,allocation_failure,allocated,constructed,checks;
static u32 kinds[8],arg1[8],arg2[8];
static void record(u32 kind,u32 a,u32 b){assert(n<8);kinds[n]=kind;arg1[n]=a;arg2[n++]=b;}
static u32 wwhd_dComIfG_getGameInfo(void){info_reads++;assert(R_SHORTCUT(u32,self+0x54)==0xA5A5A5A5u);return info;}
#include "dpad_shortcuts.h"
void shortcut_start_animation(u32 actor,u32 animation){assert(actor==self);record(1,animation,0);if(mutation==1)R_SHORTCUT(u32,self+0x54)=6;if(mutation==2)R_SHORTCUT(u32,self+0x60)=9;}
void shortcut_set_frame(u32 actor,u32 animation,u32 frame){assert(actor==self);record(2,animation,frame);if(mutation==3)R_SHORTCUT(u32,self+0x54)^=6;}
void shortcut_miiverse(u32 actor){assert(actor==self&&R_SHORTCUT(u32,self+0x64)==2);record(3,0,0);}
u32 shortcut_allocate(u32 size){assert(size==0x6C&&!constructed);allocated++;return allocation_failure?0:self;}
u32 shortcut_base_construct(u32 actor){assert(actor==self&&R_SHORTCUT(u32,self+0x54)==0xA5A5A5A5u);constructed++;return 0xDEADBEEFu;}
static void reset(void){memset(memory,0xA5,sizeof memory);n=info_reads=mutation=allocation_failure=allocated=constructed=0;save_pointer=save;memcpy(before,memory+self,sizeof before);memcpy(save_before,memory+save,sizeof save_before);}
static void untouched(unsigned lo,unsigned hi){for(unsigned i=0;i<sizeof before;i++)assert((i>=lo&&i<hi)||before[i]==memory[self+i]);assert(!memcmp(save_before,memory+save,sizeof save_before));}
int main(void){
 // Every possible byte in each inventory slot, with all independent combinations
 // in the other slots and unrelated status bits preserved.
 for(unsigned slot=0;slot<3;slot++)for(unsigned value=0;value<256;value++)for(unsigned others=0;others<4;others++)for(unsigned ship=0;ship<2;ship++){
  reset();unsigned bytes[3]={0x22,0x25,0x31},offsets[3]={0x5E,0x5F,0x69},expected_items[3]={0x22,0x25,0x31};
  bytes[slot]=value;unsigned bit=0;for(unsigned i=0;i<3;i++)if(i!=slot)bytes[i]=(others&(1u<<bit++))?expected_items[i]:255;
  for(unsigned i=0;i<3;i++)R_SHORTCUT(u8,save+offsets[i])=bytes[i];memcpy(save_before,memory+save,sizeof save_before);
  R_SHORTCUT(u32,info+0x5CD8)=0xCAFEFFFFu|(ship?0x10000u:0);
  // Clear only the ship bit in the off-ship case; retain every other bit.
  if(!ship)R_SHORTCUT(u32,info+0x5CD8)&=~0x10000u;
  randomizer_shortcut_kind(self);
  unsigned expected=(bytes[0]==0x22?1:0)|(ship&&bytes[1]==0x25?2:0)|(ship&&bytes[2]==0x31?4:0);
  assert(info_reads==1&&!n&&R_SHORTCUT(u32,self+0x54)==expected);untouched(0x54,0x58);checks++;
 }
 // All old/new low-byte flags, including invalid/reserved bits, animate only
 // independent changes to the salvage/cannon bits.
 for(unsigned old=0;old<256;old++)for(unsigned current=0;current<256;current++){
  reset();R_SHORTCUT(u32,self+0x58)=old;R_SHORTCUT(u32,self+0x54)=current;memcpy(before,memory+self,sizeof before);
  randomizer_shortcut_change(self);unsigned changed=old^current,expected_count=0;
  u32 grapple=(changed&2)?(current&2?1:3):9,bombs=(changed&4)?(current&4?2:4):9;
  assert(R_SHORTCUT(u32,self+0x5C)==grapple&&R_SHORTCUT(u32,self+0x60)==bombs);
  if(grapple!=9){assert(kinds[expected_count]==1&&arg1[expected_count]==grapple);expected_count++;}
  if(bombs!=9){assert(kinds[expected_count]==1&&arg1[expected_count]==bombs);expected_count++;}
  assert(n==expected_count&&!info_reads);untouched(0x5C,0x64);checks++;
 }
 for(unsigned current=0;current<256;current++){
  reset();R_SHORTCUT(u32,self+0x54)=current;memcpy(before,memory+self,sizeof before);randomizer_shortcut_in(self);
  assert(n==5&&kinds[0]==1&&arg1[0]==0&&kinds[1]==2&&arg1[1]==6&&arg2[1]==(current==0));
  assert(kinds[2]==2&&arg1[2]==(current&2?3:1)&&arg2[2]==0);
  assert(kinds[3]==2&&arg1[3]==(current&4?4:2)&&arg2[3]==0&&kinds[4]==3);untouched(0x64,0x68);checks++;
 }
 // Show/frame callbacks change mKind between calls, so each original reload matters.
 reset();mutation=1;R_SHORTCUT(u32,self+0x54)=0;randomizer_shortcut_in(self);assert(arg2[1]==0&&arg1[2]==3&&arg1[3]==4);
 reset();mutation=3;R_SHORTCUT(u32,self+0x54)=0;randomizer_shortcut_in(self);assert(arg2[1]==1&&arg1[2]==3&&arg1[3]==2);
 reset();mutation=2;R_SHORTCUT(u32,self+0x54)=6;R_SHORTCUT(u32,self+0x58)=0;randomizer_shortcut_change(self);assert(n==1&&arg1[0]==1&&R_SHORTCUT(u32,self+0x60)==9);
 // Native base constructor is called once and its return does not replace self.
 for(unsigned null_input=0;null_input<2;null_input++){
  reset();assert(randomizer_shortcut_construct(null_input?0:self)==self);assert(allocated==null_input&&constructed==1&&!n);
  assert(R_SHORTCUT(u32,self+0x54)==8&&R_SHORTCUT(u32,self+0x58)==8&&R_SHORTCUT(u32,self+0x64)==2);
  assert(R_SHORTCUT(u32,self+0x30)==0x10104CD8&&R_SHORTCUT(u32,self+4)==0x10104B88&&R_SHORTCUT(u32,self+0x2C)==0x10104CC8);
  assert(R_SHORTCUT(u32,self+0x50)==0&&R_SHORTCUT(u32,self+0x5C)==0&&R_SHORTCUT(u32,self+0x60)==0&&R_SHORTCUT(u8,self+0x68)==255);
  unsigned allowed[0x80]={0};unsigned words[]={4,0x2C,0x30,0x50,0x54,0x58,0x5C,0x60,0x64};
  for(unsigned j=0;j<9;j++)for(unsigned i=0;i<4;i++)allowed[words[j]+i]=1;allowed[0x68]=1;
  for(unsigned i=0;i<sizeof before;i++)assert(allowed[i]||before[i]==memory[self+i]);checks++;
 }
 reset();allocation_failure=1;assert(randomizer_shortcut_construct(0)==0&&allocated==1&&!constructed&&!memcmp(before,memory+self,sizeof before));
 printf("independent D-pad shortcuts: %u cases passed\n",checks);return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            c, exe = Path(directory)/'test.c', Path(directory)/'test'
            c.write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
