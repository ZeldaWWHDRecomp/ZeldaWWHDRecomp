"""Bomb shop full initializer: action ABI, callback order and mutated state."""
from pathlib import Path
import subprocess
import tempfile
import unittest

class BombShopInit(unittest.TestCase):
    def test_init(self):
        code = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include <stdio.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int16_t s16;typedef uint16_t u16;typedef int8_t s8;typedef uint8_t u8;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static unsigned char actor_memory[0x2000],data_memory[0x200000];
static void* memory(u32 a){if(a>=0x30000000&&a<0x30002000)return actor_memory+(a-0x30000000);if(a>=0x10000000&&a<0x10200000)return data_memory+(a-0x10000000);assert(0);return 0;}
#define W(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)memory(a))
#define R_BMS(t,a) W(t,a)
#define WWHD_GAME_DATA(a,t) W(t,a)
static u32 actor=0x30000000;static unsigned step,oldcalls,waitcalls,mutate;
static void action(u32 target,u32 self){if(target==0x0220BDCC){assert(self==actor&&W(s8,actor+0xA08)==0);waitcalls++;}else{assert(target==0x12345678&&self==actor+16&&W(s8,actor+0xA08)==-1);oldcalls++;}}
#define RANDOMIZER_BMS_CALL(t,s) action(t,s)
#include "bombshop_init.h"
void randomizer_bms_status_init(u32 s,u32 w,u32 d,u32 a){assert(step++==0&&s==actor+0x6DC&&w==255&&d==255&&a==actor);assert(W(u32,actor+0x928)==0x7F812345);}
void randomizer_bms_cylinder_set(u32 c,u32 s){assert(step++==1&&c==actor+0x718&&s==0x101BD2D4);if(mutate)W(s8,actor+0xA06)=1;}
void randomizer_bms_item_list(u32 i){assert(step++==2&&i==actor+0x9B8&&W(s16,actor+0x9FA)==2&&W(s16,actor+0x9B6)==2);W(s8,actor+0x326)=-7;}
void randomizer_bms_items_create(u32 i,s32 c,s32 r){assert(step++==3&&i==actor+0x9B8&&c==3&&r==-7);W(u32,actor+0x318)=0x7F812345;}
void randomizer_bms_event_actor(u32 c,u32 name,u32 a){assert(step++==4&&c==actor+0x884&&name==0x10017EDC&&a==actor&&W(u32,actor+0x948)==0x7F812345);W(u32,actor+0x444)=0xABCDEF00;}
void randomizer_bms_morf_calc(u32 m){assert(step++==5&&m==0xABCDEF00&&W(u32,actor+0x8EC)==actor+0x850);}
void randomizer_bms_set_mtx(u32 a){assert(step++==6&&a==actor);}
int main(void){
W(f32,0x10017EC4)=-30;W(f32,0x10017EC8)=.4f;W(f32,0x10017ECC)=1.2f;W(f32,0x10017ED0)=.9f;W(f32,0x10017ED4)=.7f;W(f32,0x10017ED8)=.6f;W(f32,0x10017DD4)=1;
for(unsigned i=0;i<4;i++)W(u32,0x101E9C38+i*4)=0x7F812340+i;
unsigned cases=0;
for(unsigned t=0;t<256;t++)for(unsigned mode=0;mode<5;mode++)for(mutate=0;mutate<2;mutate++){
memset(actor_memory,0x5A,0xA10);step=oldcalls=waitcalls=0;W(s8,actor+0xA06)=t;W(u32,actor+0x314)=0x7F812345;W(s16,actor+0x962)=mode==0?0:(mode==4?2:-1);W(s16,actor+0x960)=mode>=2?16:0;W(u32,actor+0x964)=mode==1?0x0220BDCC:0x12345678;
if(mode==4){W(s16,actor+0x966)=32;W(u32,actor+16+32)=actor+0x1000;W(u32,actor+0x1000+20)=0x12345678;}
assert(randomizer_bombshop_init(actor)==1&&step==7);const unsigned selects=t==0||t==1;assert(waitcalls==(selects&&mode!=1));assert(oldcalls==(selects&&mode>=2));
assert(W(f32,actor+0x3CC)==((mutate||t==1)?1.2f:.4f));assert(W(f32,actor+0x3D8)==((mutate||t==1)?.7f:.4f));assert(W(f32,actor+0x3E4)==1&&W(f32,actor+0x3E8)==1);assert(W(u8,actor+0x95C)==255&&W(u8,actor+0x918)==0&&W(u8,actor+0x95D)==0);for(unsigned i=0;i<4;i++)assert(W(u32,actor+0x41C+i*4)==0x7F812340+i&&W(u32,actor+0x42C+i*4)==0x7F812340+i);assert(W(u8,actor+0xA0F)==0x5A);cases++;}
printf("Bomb shop init: %u cases passed\n",cases);return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);(p/'test.c').write_text(code)
            subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-Wno-int-to-pointer-cast','-Wno-pointer-to-int-cast','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)

if __name__=='__main__':unittest.main()
