"""Firewall creation: exact switch branch, native prefix contracts and fused sound positions."""
from pathlib import Path
import os, subprocess, tempfile, unittest
class Firewall(unittest.TestCase):
 def test_creation(self):
  code=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <math.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef uint16_t u16;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static u8 actor[0x800],model[0x200],archive[8],cylinder_source[80],savea[64],saveb[64],play[0x5400],trig[0x10000];
static const char name_before[]="before GetInfo",name_after[]="after GetInfo";
static u32 saveptr,nameptr;
static f32 radius;
static void* memory(u32 a){if(a==1)return&saveptr;if(a==0x101C9440)return&nameptr;if(a==0x10028A20)return archive;if(a==0x10028AA4)return cylinder_source;if(a==0x10028B0C)return&radius;assert(a>=0x104A44F8&&a<0x104B44F8);return trig+a-0x104A44F8;}
#define WWHD_GAME_DATA(a,t) (*(t*)memory(a))
static u32 wwhd_dComIfG_getGameInfo(void);
/* Portable host oracle; the strict PPC object is separately checked for fmadds. */
#define RANDOMIZER_FIREWALL_FMADD fmaf
#include "fire_mountain_firewall.h"
static s32 load_result,heap_result,switch_result,event_result;
static unsigned sequence[16],calls,constructed,mutation,model_null;
static u32 parameter_result;
static void record(unsigned n){assert(calls<16);sequence[calls++]=n;}
static uint32_t bits(f32 f){uint32_t v;memcpy(&v,&f,4);return v;}
u32 randomizer_firewall_ctor(u32 a){assert(a==(u32)actor);record(1);*(unsigned int*)(actor+0x2E4)=0x400;return a+32;}
s32 randomizer_firewall_load(u32 phase,u32 name){record(2);assert(phase==(u32)actor+0x3AC&&name==(u32)archive);*(unsigned int*)phase=0xBAD;return load_result;}
s32 randomizer_firewall_heap(u32 a,u32 cb,u32 size){record(3);assert(a==(u32)actor&&cb==0x023418CC&&size==0x1420);*(u32*)(actor+0x520)=model_null?0:(u32)model;return heap_result;}
void randomizer_firewall_matrix(u32 a){record(4);assert(a==(u32)actor);assert(*(u32*)(actor+0x348)==(model_null?0:(u32)model+0xC8));if(mutation)*(u32*)(actor+0x520)=0;}
void randomizer_firewall_status(u32 status,u32 weight,u32 flags,u32 a){record(5);assert(status==(u32)actor+0x3B4&&weight==255&&flags==255&&a==(u32)actor);*(unsigned int*)(actor+0x430)=0x12340000;}
void randomizer_firewall_cylinder(u32 cylinder,u32 source){record(6);assert(cylinder==(u32)actor+0x3F0&&source==(u32)cylinder_source);*(unsigned int*)(actor+0x430)=0xABCD0010;radius=mutation?1000.5f:1000.0f;}
u32 randomizer_firewall_parameter(u32 a,u32 width,u32 shift){
 record(7);assert(a==(u32)actor&&width==8&&shift==0);assert(actor[0x6EC]==1);assert(*(u32*)(actor+0x434)==(u32)actor+0x3B4);assert(*(unsigned int*)(actor+0x430)==0xABCD0012);
 for(unsigned i=0;i<8;i++){
  const float sine=*(float*)(trig+i*0x2000),cosine=*(float*)(trig+i*0x2000+4),scale=*(float*)(actor+0x330);
  const float sx=sine*radius,cz=cosine*radius;
  assert(bits(*(float*)(actor+0x68C+i*12))==bits(fmaf(sx,scale,*(float*)(actor+0x314))));
  assert(*(uint32_t*)(actor+0x690+i*12)==0x7F800001);
  assert(bits(*(float*)(actor+0x694+i*12))==bits(fmaf(cz,scale,*(float*)(actor+0x31C))));
 }
 if(mutation)saveptr=(u32)saveb;
 return parameter_result;
}
s32 randomizer_firewall_switch(u32 save,s32 bit,s32 room){record(8);assert(save==saveptr+0x20&&bit==5&&room==0);assert(*(unsigned int*)(actor+0x644)==(uint32_t)parameter_result);nameptr=(u32)name_before;*(unsigned int*)(actor+0x6F0)=0xAABBCCDD;return switch_result;}
static u32 wwhd_dComIfG_getGameInfo(void){record(9);assert(*(unsigned int*)(actor+0x6F0)==1);nameptr=(u32)name_after;return(u32)play;}
s32 randomizer_firewall_event_index(u32 manager,u32 name,u32 hint){record(10);assert(manager==(u32)play+0x52C4&&name==(u32)name_before&&hint==255);*(unsigned int*)(actor+0x3AC)=0xCAFE;return event_result;}
static void reset(void){
 memset(actor,0xAA,sizeof actor);calls=0;saveptr=(u32)savea;nameptr=(u32)name_before;radius=3.0f;
 *(unsigned int*)(actor+0x2E4)=constructed?0x808:0;*(unsigned int*)(actor+0x684)=0x11223344;*(u16*)(actor+0x688)=0xBEEF;*(u16*)(actor+0x680)=0x1234;*(u16*)(actor+0x682)=0x5678;
 *(float*)(actor+0x330)=mutation?0x1.000002p0f:-0.0f;*(float*)(actor+0x314)=mutation?-0x1.000002p10f:0x1p20f;*(uint32_t*)(actor+0x318)=0x7F800001;*(float*)(actor+0x31C)=mutation?0x1p-20f:-0.0f;
 for(unsigned i=0;i<8;i++){*(float*)(trig+i*0x2000)=(i&1)?-0x1.000002p0f:(float)i/8;*(float*)(trig+i*0x2000+4)=(float)(8-i)/7;}
}
int main(void){
 unsigned scenarios=0;const s32 switches[]={-2147483647-1,-7,0,1,2,255,2147483647};const s32 events[]={-65537,-32769,-1,0,32767,65536};
 for(constructed=0;constructed<2;constructed++)for(mutation=0;mutation<2;mutation++)for(model_null=0;model_null<2;model_null++)for(load_result=-4;load_result<=7;load_result++)for(heap_result=-1;heap_result<=2;heap_result++)for(unsigned sw=0;sw<7;sw++)for(unsigned ev=0;ev<6;ev++){
  switch_result=switches[sw];event_result=events[ev];parameter_result=mutation?0xC0DE1234:0xFF;reset();const s32 result=randomizer_firewall_create((u32)actor);
  const unsigned prefix=constructed?0:1;assert(sequence[0]==(constructed?2:1));assert(*(unsigned int*)(actor+0x2E4)==(constructed?0x808:0x408));
  if(load_result!=4){assert(result==load_result&&calls==prefix+1);}
  else if(!heap_result){assert(result==5&&calls==prefix+2);}
  else if(switch_result==1){assert(result==5&&calls==prefix+7);assert(*(unsigned int*)(actor+0x6F0)==0xAABBCCDD);assert(*(u16*)(actor+0x688)==0xBEEF);assert(*(u16*)(actor+0x680)==0x1234&&*(u16*)(actor+0x682)==0x5678);assert(*(unsigned int*)(actor+0x684)==0x11223344);}
  else {assert(result==4&&calls==prefix+9);assert(*(unsigned int*)(actor+0x6F0)==1);assert(*(u16*)(actor+0x688)==(uint16_t)event_result);assert(*(u16*)(actor+0x680)==0&&*(u16*)(actor+0x682)==0xFFFF);assert(*(unsigned int*)(actor+0x684)==0x02342990);}
  for(unsigned i=0;i<calls;i++)assert(sequence[i]==i+(constructed?2:1));++scenarios;
 }
 assert(scenarios==16128);return 0;
}
'''
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);(p/'test.c').write_text(code)
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror','-I',str(Path(__file__).resolve().parent),str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
