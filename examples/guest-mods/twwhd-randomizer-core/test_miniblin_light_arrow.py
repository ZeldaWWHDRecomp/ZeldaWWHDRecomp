"""Miniblin complete creation and corrected actor-relative death-switch metadata."""
from pathlib import Path
import os,subprocess,tempfile,unittest
class Miniblin(unittest.TestCase):
 def test_consumer(self):
  code=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef int8_t s8;typedef int16_t s16;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static u8 actor[0x1200],save[0x100],play[0x6000],morf[0x100],hio[0x100],hio_set,body_src[64],attack_src[64],joints[10];static u32 saveptr=(u32)save;static s8 room=-5;static f32 regs[12],thirty=30,six=0.6f,forty=40;static unsigned int scales[10];
static void* data(u32 a){switch(a){case 1:return&saveptr;case 0x1047E6C8:return&room;case 0x101CEF78:return&hio_set;case 0x1046D334:return hio;case 0x101CEF9C:return body_src;case 0x101CEFDC:return attack_src;case 0x1047B608:return regs;case 0x10038278:return&thirty;case 0x100382A8:return&six;case 0x1003828C:return&forty;case 0x101CF044:return joints;case 0x101CF01C:return scales;}assert(0);return 0;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static u32 ptr(u32 a){if(a==(u32)actor+0x3DC)return(u32)morf;assert(a==(u32)morf+0x90);return 0x101000;}
#define RML_PTR(a,o) ptr((a)+(o))
static u32 wwhd_dComIfG_getGameInfo(void);
#include "miniblin_light_arrow.h"
static unsigned phase,heap_ok,switch_on,ctor_calls,load_calls,heap_calls,spheres,hio_calls,execute_calls,mutation;static u8 live_behavior,live_switch;
static u32 wwhd_dComIfG_getGameInfo(void){return(u32)play;}
u32 rml_constructor(u32 self){assert(self==(u32)actor);++ctor_calls;RML32(self,0x2E4)=0x100;return self;}
s32 rml_load(u32 p,u32 arc){assert(p==(u32)actor+0x3C8&&arc==0x100382AC);++load_calls;return phase;}
s32 rml_switch(u32 s,u32 index,s32 r){assert(s==(u32)save+0x20&&index==RML8((u32)actor,0x3D3)&&r==-5);return switch_on;}
u32 rml_character(u32 manager,u32 name,u32 argument){assert(manager==(u32)play+0x50AC&&name==0x100382B0&&!argument);return 12;}
s32 rml_heap(u32 self,u32 callback,u32 size){assert(self==(u32)actor&&callback==0x02446A4C&&size==0x4B000);++heap_calls;return heap_ok;}
u32 rml_hio(u32 name,u32 value){assert(name==0x100382B8&&value==(u32)hio&&hio_set==1&&RML8((u32)actor,0xEE0)==1);++hio_calls;return 11;}
void rml_acch(u32 acch,u32 current,u32 old,u32 self,s32 count,u32 circle,u32 speed,u32 angle,u32 shape){assert(acch==(u32)actor+0x480&&current==self+0x314&&old==self+0x300&&self==(u32)actor&&count==1&&circle==self+0x440&&speed==self+0x33C&&!angle&&!shape);}
void rml_wall(u32 circle,f32 h,f32 r){assert(circle==(u32)actor+0x440&&h==50&&r==50);}
void rml_status(u32 status,s32 weight,s32 damage,u32 self){assert(status==self+0x644&&weight==100&&!damage&&self==(u32)actor&&RML8(self,0x3A1)==2&&RML8(self,0x3A0)==2);}
void rml_sphere(u32 sphere,u32 source){++spheres;assert(sphere==(u32)actor+(spheres==1?0x680:0x7AC)&&source==(u32)(spheres==1?body_src:attack_src));if(spheres==2){assert(RML32((u32)actor,0x6C4)==(unsigned int)((u32)actor+0x644));assert(RML8((u32)actor,0xAAD)==0xA5);if(mutation){RML8((u32)actor,0x3D0)=live_behavior;RML8((u32)actor,0x3D4)=live_switch;}}}
s32 rml_execute(u32 self){assert(self==(u32)actor&&spheres==2);++execute_calls;return -1;}
static void reset(void){memset(actor,0,sizeof actor);hio_set=1;ctor_calls=load_calls=heap_calls=spheres=hio_calls=execute_calls=mutation=0;RML8((u32)actor,0xAAD)=0xA5;RML8((u32)actor,0xA14)=0x61;RML8((u32)actor,0xA18)=0x72;RML8((u32)actor,0x10F1)=0x83;}
int main(void){unsigned cases=0;
for(unsigned behavior=0;behavior<16;behavior++)for(unsigned death=0;death<256;death++)for(unsigned enabled=0;enabled<2;enabled++)for(unsigned heap=0;heap<2;heap++)for(unsigned initialized=0;initialized<2;initialized++){
 reset();phase=4;heap_ok=heap;switch_on=enabled;RML32((u32)actor,0xB0)=(enabled?0xFF000000u:0x02000000u)|(death<<16)|0xC800|behavior;RML16((u32)actor,0x320)=-123;RML32((u32)actor,0x2E4)=initialized?8:0;
 assert(randomizer_miniblin_create((u32)actor)==(heap?4:5));assert(ctor_calls==!initialized&&load_calls==1&&heap_calls==1);assert(spheres==2*heap&&execute_calls==heap);assert(RML8((u32)actor,0xAAD)==(heap&&behavior?death:0xA5));assert(RML16((u32)actor,0x3F4)==-123&&!RML16((u32)actor,0x320));assert(RML8((u32)actor,0x3D5)==!enabled);assert(RML8((u32)actor,0xA14)==0x61&&RML8((u32)actor,0xA18)==0x72&&RML8((u32)actor,0x10F1)==0x83);++cases;
}
for(phase=0;phase<7;phase++){if(phase==4)continue;reset();heap_ok=1;assert(randomizer_miniblin_create((u32)actor)==(s32)phase&&!heap_calls&&!spheres&&!execute_calls);++cases;}
for(unsigned behavior=0;behavior<2;behavior++){reset();phase=4;heap_ok=1;switch_on=1;hio_set=0;mutation=1;live_behavior=behavior;live_switch=17;RML32((u32)actor,0xB0)=0xFF110001;assert(randomizer_miniblin_create((u32)actor)==4&&hio_calls==1&&hio[0]==11);assert(RML8((u32)actor,0xAAD)==(behavior?17:0xA5));++cases;}
printf("Miniblin light-arrow: %u cases passed\n",cases);return 0;}
'''
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'test.c').write_text(code)
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
