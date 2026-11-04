"""Bomb-shop complete create: constructor order, phases, callback reloads."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

class BombShopCreate(unittest.TestCase):
    def test_create(self):
        code=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int16_t s16;typedef uint16_t u16;typedef int8_t s8;typedef uint8_t u8;typedef float f32;
static unsigned char actor_mem[0xB00],data_mem[0x500000],save_mem[0x800],morf_mem[0x100];
static u32 actor=0x30000000,save=0x30010000,morf=0x30020000;
static void* memory(u32 a){if(a>=actor&&a<actor+sizeof actor_mem)return actor_mem+a-actor;if(a>=0x10000000&&a<0x10500000)return data_mem+a-0x10000000;if(a>=save&&a<save+sizeof save_mem)return save_mem+a-save;if(a>=morf&&a<morf+sizeof morf_mem)return morf_mem+a-morf;assert(0);return 0;}
#define W(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)memory(a))
#define R_BMS(t,a) W(t,a)
#define WWHD_GAME_DATA(a,t) W(t,a)
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static unsigned sequence[32],n,event_value,phase,heap_ok,init_ok,mutate,event_calls,load_calls,heap_calls,init_calls,hio_calls;
static u32 randomizer_daily_save(void){return save;}
static void trace(unsigned id){sequence[n++]=id;}
static s32 randomizer_bombshop_init(u32 a);
#include "bombshop_create.h"
#define CT(name,off,id) void name(u32 a){assert(a==actor+off);trace(id);}
CT(randomizer_bms_actor_construct,0,1)
CT(randomizer_bms_btp_construct,0x44C,2)
void randomizer_bms_acch_construct(u32 a){assert(a==actor+0x4D8);assert(W(u32,actor+0xB4)==0x10017D54);trace(3);}
void randomizer_bms_circle_construct(u32 a){assert(a==actor+0x69C);assert(W(u32,actor+0x4E8)==0x10017D24&&W(u32,actor+0x4EC)==0x10017D44&&W(u32,actor+0x4F8)==0x10017D34&&W(u8,actor+0x4F0)==1);trace(4);}
CT(randomizer_bms_cc_status_construct,0x6DC,5)
CT(randomizer_bms_dcc_status_construct,0x6F8,6)
void randomizer_bms_cc_cylinder_construct(u32 a){assert(a==actor+0x718);assert(W(u32,actor+0x6F4)==0x1004AE88&&W(u32,actor+0x6F8)==0x1004AEC0);trace(7);}
void randomizer_bms_geometric_cylinder_construct(u32 a){assert(a==actor+0x830&&W(u32,actor+0x82C)==0x100015A8&&W(u32,actor+0x828)==0x10017D14);trace(8);}
void randomizer_bms_joint_construct(u32 a){assert(a==actor+0x850&&W(u32,actor+0x754)==0x1004B108&&W(u32,actor+0x844)==0x1004B150&&W(u32,actor+0x82C)==0x1004B160);trace(9);}
CT(randomizer_bms_cut_construct,0x884,10)
void randomizer_bms_stick_params(u32 s,s32 a,s32 b,s32 c,s32 d,f32 rate,f32 threshold,s32 g,s32 h){assert(s==actor+0x8F0&&a==15&&b==15&&!c&&!d&&rate==.9f&&threshold==.5f&&!g&&h==0x2000&&W(u32,actor+0x914)==0x10050788);trace(11);}
CT(randomizer_bms_stick_init,0x8F0,12)
CT(randomizer_bms_cam_construct,0x968,13)
void randomizer_bms_items_construct(u32 a){assert(a==actor+0x9B8);trace(14);W(u32,actor+0x2E4)=0x12340000;}
s32 randomizer_bms_event_query(u32 e,u32 b){assert(e==save+0x644&&b==0xA02);event_calls++;trace(15);if(mutate)W(s8,actor+0xA06)=1;return event_value;}
s32 randomizer_bms_resource_load(u32 p,u32 arc){assert(p==actor+0x43C&&arc==0x101BD3A0&&!W(u8,actor+0xA0D));load_calls++;trace(16);return phase;}
s32 randomizer_bms_solid_heap(u32 a,u32 cb,u32 size){assert(a==actor&&cb==0x02209434&&size==0x18000);heap_calls++;trace(17);assert(W(s8,actor+0xA06)==(mutate?1:0));W(u32,actor+0x444)=morf;return heap_ok;}
s32 randomizer_bms_hio_child(u32 name,u32 hio){assert(name==0x10017EE4&&hio==0x10466650);hio_calls++;trace(18);W(u32,0x10466654)=11;return 0x180;}
static s32 randomizer_bombshop_init(u32 a){assert(a==actor);init_calls++;trace(19);return init_ok;}
int main(void){W(f32,0x10017ED0)=.9f;W(f32,0x10017DCC)=.5f;unsigned cases=0;
for(unsigned t=0;t<256;t++)for(unsigned constructed=0;constructed<2;constructed++)for(event_value=0;event_value<2;event_value++)for(phase=0;phase<6;phase++)for(heap_ok=0;heap_ok<2;heap_ok++)for(init_ok=0;init_ok<2;init_ok++)for(mutate=0;mutate<2;mutate++){
memset(actor_mem,0x5A,sizeof actor_mem);n=event_calls=load_calls=heap_calls=init_calls=hio_calls=0;W(u32,actor+0xB0)=t<<24;W(u32,actor+0x2E4)=constructed?0x8008:0x8000;W(s32,0x10466654)=-1;W(u32,morf+0x90)=0x12345600;
const s32 result=randomizer_bombshop_create(actor);const unsigned ready=!t&&phase==4;assert(result==(t?5:(ready&&(!heap_ok||!init_ok)?5:(s32)phase)));assert(W(u8,actor+0xA0D)==(t!=0));assert(event_calls==1&&load_calls==!t&&heap_calls==ready&&init_calls==(ready&&heap_ok));assert(W(u32,actor+0x2E4)==(constructed?0x8008:0x12340008));
unsigned pos=0;if(!constructed)for(unsigned i=1;i<=14;i++)assert(sequence[pos++]==i);assert(sequence[pos++]==15);if(!t)assert(sequence[pos++]==16);if(ready){assert(sequence[pos++]==17);if(heap_ok){assert(sequence[pos++]==18&&sequence[pos++]==19);assert(W(u32,actor+0x348)==0x123456C8&&W(s32,0x10466654)==12&&W(s8,0x10466650)==-128);}}assert(n==pos);assert(W(u8,actor+0xA0F)==0x5A);cases++;}
printf("Bomb shop create: %u cases passed\n",cases);return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);(p/'test.c').write_text(code)
            subprocess.run([os.environ.get("CC", "cc"),'-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)

if __name__=='__main__':unittest.main()
