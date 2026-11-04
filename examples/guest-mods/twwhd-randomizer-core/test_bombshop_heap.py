"""Bomb-shop complete heap lifecycle, resource failures and joint callbacks."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

class BombShopHeap(unittest.TestCase):
    def test_heap(self):
        code=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int16_t s16;typedef uint16_t u16;typedef int8_t s8;typedef uint8_t u8;typedef float f32;
static unsigned char arena[0x20000],game_data[0x500000];
static const u32 actor=0x30000000,save=0x30001000,morf=0x30002000,model=0x30003000,headmodel=0x30004000,data=0x30005000,head=0x30006000,block=0x30007000,records=0x30008000;
static void* memory(u32 a){if(a>=0x30000000&&a<0x30020000)return arena+a-0x30000000;if(a>=0x10000000&&a<0x10500000)return game_data+a-0x10000000;assert(0);return 0;}
#define W(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)memory(a))
#define R_BMS(t,a) W(t,a)
#define WWHD_GAME_DATA(a,t) W(t,a)
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static unsigned alloc,fail_alloc,morf_failure,tex_ok,cursor_ok,event_value,events,texsets,asserts,wall,cursor_calls,resources[32],nr,mutate_type,initial_type,joint_calls;
static u32 randomizer_daily_save(void){return save;}
s32 randomizer_bms_event_query(u32 e,u32 b);
#include "bombshop_heap.h"
u32 randomizer_bms_object_res(u32 controller,u32 string,s32 i){const u32* str=(const u32*)string;assert(controller==0x1234&&str[0]==0x101BD3A0&&str[1]==0x10017CFC&&nr<32);resources[nr++]=i;if(i==0x1D||i==0x1A)return data;if(i==0x23)return head;return 0x40000000+i;}
u32 randomizer_bms_morf_create(u32 s,u32 d,u32 c1,u32 c2,u32 animation,s32 attr,f32 rate,s32 start,s32 end,s32 enabled,u32 sound,u32 flags,u32 mf){assert(!s&&d==data&&!c1&&!c2&&attr==2&&rate==1&&!start&&end==-1&&enabled==1&&!sound&&!flags&&mf==0x11020203);assert(animation==(initial_type?0x40000012:0x40000014));if(initial_type)assert(W(u8,actor+0xA02)==3);if(mutate_type)W(s8,actor+0xA06)=0;return morf_failure==1?0:morf;}
u32 randomizer_bms_joint_names(u32 d){assert(d==data||d==head);joint_calls++;return d;}
s32 randomizer_bms_name_index(u32 names,u32 name){assert(names==data+0x110||names==head+0x110);switch(name){case 0x10017E1C:return 1;case 0x10017E6C:return 2;case 0x10017E24:return -1;case 0x10017E0C:return 3;case 0x10017E14:return 4;default:assert(0);return 0;}}
void randomizer_bms_assert(u32 file,u32 line,u32 condition){assert(file==0x10017E58&&line==0x84A&&condition==0x10017E78);asserts++;}
u32 randomizer_bms_model_create(u32 d,u32 flags,u32 mf){alloc++;if(d==head){assert(alloc==1&&flags==0x80000&&mf==0x11020002);return fail_alloc==alloc?0:headmodel;}assert(!flags&&mf==0x11020203);assert(d>=0x4000001B&&d<=0x40000020);return fail_alloc==alloc?0:0x30009000+alloc*0x100;}
s32 randomizer_bms_tex_init(u32 a,u32 modify){assert(a==actor&&!modify&&W(u8,actor+0xA00)==1);return tex_ok;}
u32 randomizer_bms_tex_set(u32 a,s32 index){assert(a==actor&&index==(W(s8,actor+0xA06)==0?!!event_value:1));texsets++;return 0;}
s32 randomizer_bms_event_query(u32 e,u32 b){assert(e==save+0x644&&b==0xA02);events++;return event_value;}
u32 randomizer_bms_joint_block(u32 d){assert(d==data||d==head);return block;}
void randomizer_bms_wall(u32 c,f32 radius,f32 height){assert(c==actor+0x69C&&radius==30&&height==0);assert(W(u32,model+0xB8)==actor&&W(u32,headmodel+0xB8)==actor);wall++;}
void randomizer_bms_acch_set(u32 c,u32 p,u32 old,u32 a,s32 enabled,u32 circle,u32 speed,u32 rot,u32 shape){assert(wall==1&&c==actor+0x4D8&&p==actor+0x314&&old==actor+0x300&&a==actor&&enabled==1&&circle==actor+0x69C&&speed==actor+0x33C&&!rot&&!shape);}
u32 randomizer_bms_cursor_create(u32 m,u32 brk,f32 scale){assert(m==0x40000008&&brk==0x4000000B&&scale==.75f&&wall==1);cursor_calls++;return cursor_ok?0x30019000:0;}
int main(void){W(u32,0x101F4F28)=0x1234;W(f32,0x10017DD4)=1;W(f32,0x10017E08)=30;W(f32,0x10466684)=.75f;unsigned cases=0;
for(initial_type=0;initial_type<256;initial_type++)for(event_value=0;event_value<2;event_value++)for(fail_alloc=0;fail_alloc<6;fail_alloc++)for(morf_failure=0;morf_failure<3;morf_failure++)for(tex_ok=0;tex_ok<2;tex_ok++)for(cursor_ok=0;cursor_ok<2;cursor_ok++)for(mutate_type=0;mutate_type<2;mutate_type++){
memset(arena,0,sizeof arena);alloc=events=texsets=asserts=wall=cursor_calls=nr=joint_calls=0;W(s8,actor+0xA06)=initial_type;W(u32,morf+0x90)=morf_failure==2?0:model;W(u32,model+0xAC)=data;W(u32,headmodel+0xAC)=head;W(u32,data+0x10)=W(u32,head+0x10)=0x100;W(u32,data+4)=W(u32,head+4)=5;W(u32,data+8)=W(u32,head+8)=records;W(u16,block+8)=5;
const s32 result=randomizer_bombshop_heap(actor);const unsigned type=mutate_type?0:initial_type;const unsigned body=type==0;const unsigned allocation_ok=fail_alloc!=1&&fail_alloc!=2&&(!body||(fail_alloc!=4&&fail_alloc!=5));const unsigned success=!morf_failure&&allocation_ok&&tex_ok&&cursor_ok;assert(result==(s32)success);assert(resources[0]==(initial_type?0x1A:0x1D)&&resources[1]==(initial_type?0x12:0x14));
if(morf_failure){assert(nr==2&&W(u32,actor+0x444)==0&&!alloc&&!joint_calls);}else {assert(W(u32,actor+0x444)==morf);assert(asserts==body);if(fail_alloc!=1&&tex_ok){assert(texsets==(type==0||type==1));if(fail_alloc!=2&&(!body||(fail_alloc!=4&&fail_alloc!=5))){assert(wall==1&&cursor_calls==1&&resources[nr-2]==11&&resources[nr-1]==8);assert(W(u32,records+3*0x1C+8)==0x02208634&&W(u32,records+4*0x1C+8)==0x02208634);}}}cases++;}
printf("Bomb shop heap: %u cases passed\n",cases);return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);(p/'test.c').write_text(code)
            subprocess.run([os.environ.get("CC", "cc"),'-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)

if __name__=='__main__':unittest.main()
