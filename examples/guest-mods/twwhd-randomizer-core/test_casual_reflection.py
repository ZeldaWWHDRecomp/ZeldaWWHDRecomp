"""Full reflection traversal and seed outfit policy without mutating save flags."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class CasualReflection(unittest.TestCase):
    def test_complete_reflection_consumer(self):
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef uintptr_t u32;typedef uint16_t u16;typedef uint8_t u8;typedef int32_t s32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 memory[0x10000],snapshot[0x10000];
#define R_CLOTHES_u32(a) (*(uint32_t*)(memory+(a)))
#define R_CLOTHES_u16(a) (*(uint16_t*)(memory+(a)))
#define R_CLOTHES_s32(a) (*(int32_t*)(memory+(a)))
#define R_CLOTHES_u8(a) (*(uint8_t*)(memory+(a)))
#define R_CLOTHES(t,a) R_CLOTHES_##t(a)
#define WWHD_ADDR_save_info_pointer 1
static volatile u32 unused_save_allocation;
#define WWHD_GAME_DATA(a,t) (*(volatile t*)&unused_save_allocation)
#define CLOTHES_DATA(a) (0xA000u+((a)&0xFFFu))
static const u32 packet=0x100,context=0x200,model=0x1000,data=0x2000,tree=0x3000,joints=0x4000,mesh=0x5000,shape=0x6000,shape_data=0x6100,header=0x7000,indices=0x8000,materials=0x9000;
static unsigned init_should_start_with_heros_clothes,constructs,applies,set_materials,joint_queries,direct_calls,assure_calls,draws,mutation;
static u32 drawn[16],offsets[16],*literal_stack;static unsigned checks;
static void assure(u32 *s){
 assert(s[1]==CLOTHES_DATA(0x10008A1C));assure_calls++;
 if(mutation&&assure_calls==1)literal_stack=s;
 if(assure_calls==2&&mutation==1)literal_stack[0]=0x7100;
 if(assure_calls==2&&mutation==2){literal_stack[0]=0x7100;s[0]=0x7200;}
}
#define CLOTHES_ASSURE(s) assure(s)
static void draw(u32 p,u32 offset,u32 info_address){
 u32 *info=(u32*)info_address;assert(draws<16);drawn[draws]=p;offsets[draws++]=offset;
 assert(info[0]==0x1234&&info[1]==2&&info[3]==2&&info[5]==0x7777);
 for(unsigned i=0;i<13;i++)if(i!=0&&i!=1&&i!=3&&i!=5)assert(info[i]==0);
}
#define CLOTHES_DRAW(p,o,i) draw(p,o,i)
#include "casual_clothes.h"
#include "casual_reflection.h"
void clothes_state_construct(u32 state){constructs++;memset((void*)state,0xC3,0x11C);}
void clothes_state_apply(u32 state){applies++;if(applies==2)assert(state==CLOTHES_DATA(0x104B474C));}
void clothes_material(u32 actor,u32 state){assert(actor==packet&&constructs==1&&applies==0);assert(*(u8*)state==0xC3);set_materials++;}
u32 clothes_joint_tree(u32 d){assert(d==data);joint_queries++;return tree;}
void clothes_assure_direct(u32 address){u32 *s=(u32*)address;assert(s[1]==CLOTHES_DATA(0x10008A1C));direct_calls++;}
static void reset(void){
 memset(memory,0,sizeof memory);constructs=applies=set_materials=joint_queries=direct_calls=assure_calls=draws=mutation=0;
 R_CLOTHES(u32,context)=0x1234;R_CLOTHES(u32,context+0xC)=2;
 R_CLOTHES(u32,packet+0xA8)=0x7777;R_CLOTHES(u32,packet+0x98)=model;
 R_CLOTHES(u32,model+0xAC)=data;R_CLOTHES(u32,data+4)=1;R_CLOTHES(u32,data+8)=joints;R_CLOTHES(u16,tree+8)=1;
 R_CLOTHES(u32,joints+0x10)=mesh;R_CLOTHES(u32,mesh)=header;R_CLOTHES(u32,mesh+8)=shape;
 R_CLOTHES(u32,shape)=shape_data;R_CLOTHES(u8,shape+4)=1;
 R_CLOTHES(s32,header+4)=0xFC; // name at header+100
 R_CLOTHES(u32,model+0x128)=3;R_CLOTHES(u32,model+0x12C)=materials;
 R_CLOTHES(u32,model+0x130)=3;R_CLOTHES(u32,model+0x134)=indices;R_CLOTHES(u16,indices)=1;
 const char *names[]={"ear_3_","ear_8_","eyeL","eyeR","mayuL","mayuR"};
 const unsigned addr[]={0x10008CB4,0x10008CBC,0x10008CC4,0x10008CCC,0x10008CA4,0x10008CAC};
 for(unsigned i=0;i<6;i++)strcpy((char*)memory+CLOTHES_DATA(addr[i]),names[i]);
}
int main(void){
 const char *names[]={"ear_3_","ear_8_","eyeL","eyeR","mayuL","mayuR","ear_3_x","body",""};
 for(unsigned hero=0;hero<2;hero++)for(unsigned face=0;face<2;face++)for(unsigned visible=0;visible<2;visible++)for(unsigned name=0;name<9;name++)for(unsigned shape_index=0;shape_index<5;shape_index++)for(unsigned material_index=0;material_index<5;material_index++){
  reset();init_should_start_with_heros_clothes=hero;R_CLOTHES(u8,packet+0xAD)=face;R_CLOTHES(u8,shape+4)=visible;
  R_CLOTHES(u16,shape_data+0xC)=shape_index;for(unsigned i=0;i<3;i++)R_CLOTHES(u16,indices+i*2)=material_index;
  strcpy((char*)memory+header+0x100,names[name]);memcpy(snapshot,memory,sizeof memory);
  randomizer_reflection_draw(packet,context);
  const unsigned should_draw=visible?!(face&&!hero&&name<2):(face&&name>=2&&name<6);
  assert(draws==should_draw&&constructs==1&&set_materials==1&&applies==2&&joint_queries==2);
  if(draws)assert(drawn[0]==materials+(material_index<3?material_index*0xAC:0)&&offsets[0]==0xC);
  assert(!memcmp(memory,snapshot,sizeof memory));checks++;
 }
 // Context early return, absent helper/model and both additional packet types.
 for(unsigned kind=0;kind<4;kind++)for(unsigned helper=0;helper<2;helper++)for(unsigned has_model=0;has_model<2;has_model++){
  reset();R_CLOTHES(u32,context+0xC)=kind;R_CLOTHES(u32,packet+0xA8)=helper?0x7777:0;R_CLOTHES(u32,packet+0x98)=has_model?model:0;
  R_CLOTHES(u32,packet+0x9C)=0x3456;R_CLOTHES(u32,packet+0xA0)=0x4567;
  randomizer_reflection_draw(packet,context);assert(constructs==(kind==2)&&applies==(kind==2?2:0));
  assert(draws==(kind==2&&helper?2+has_model:0));
  if(kind==2&&helper){assert(drawn[draws-2]==0x3456&&offsets[draws-2]==0xC&&drawn[draws-1]==0x4567&&offsets[draws-1]==0x130);}checks++;
 }
 // Bounds fallbacks retain Buffer's first-entry semantics, including joint fallback.
 reset();R_CLOTHES(u32,data+4)=0;R_CLOTHES(u16,tree+8)=3;randomizer_reflection_draw(packet,context);assert(draws==3&&joint_queries==4);checks++;
 // Literal identity is captured before the final assurance callback, and byte
 // fallback reloads afterward; no fake ownership or save bytes are involved.
 reset();mutation=1;assert(clothes_name_equal(0x7300,0x7300)==1&&assure_calls==2);
 reset();mutation=2;strcpy((char*)memory+0x7100,"new");strcpy((char*)memory+0x7200,"new");assert(clothes_name_equal(0x7300,0x7400)==1);
 printf("casual reflection: %u complete traversal cases passed\n",checks);return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            c,exe=Path(directory)/'test.c',Path(directory)/'test'
            c.write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
