"""Six complete traversal consumers with callback-visible ordering and precision."""
from pathlib import Path
import subprocess
import tempfile
import unittest

class TraversalAnimationSmall(unittest.TestCase):
    def test_consumers(self):
        code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
typedef uintptr_t u32;typedef uint16_t u16;typedef uint8_t u8;typedef int32_t s32;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static unsigned char arena[0x20000],data[0x500000];static const u32 player=0x30000000,game=0x30010000;
static void* mem(u32 a){if(a>=player&&a<player+sizeof arena)return arena+a-player;if(a>=0x10000000&&a<0x10500000)return data+a-0x10000000;if(a>0xFFFFFFFFu)return(void*)a;assert(0);return 0;}
#define W(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)mem(a))
#define R_TAS(t,a) W(t,a)
#define WWHD_GAME_DATA(a,t) W(t,a)
#include "traversal_animation_small.h"
static unsigned calls,keep,kind,mutation;static s32 angle,foot;static f32 expected_radians,new_rate,new_sine;
s32 rtas_rad_to_short(f32 value){assert(value==expected_radians||isnan(value)&&isnan(expected_radians));calls++;if(mutation){W(f32,player+0x589C)=999;W(f32,player+0x5898)=new_rate;W(f32,0x104A44F8+((u16)angle>>3)*8)=new_sine;}return angle;}
s32 rtas_push_keep(u32 p,s32 mode){assert(p==player&&!mode&&calls==0);calls++;if(mutation)W(s32,p+0x69C0)=77;return keep;}
void rtas_common(u32 p,s32 proc){assert(p==player&&calls==(kind<=1?1:0));assert(proc==(kind==0?0x33:kind==1?0x34:0x39));calls++;if(mutation)W(s32,p+0x69C0)=99;}
void rtas_animation(u32 p,s32 anim,f32 rate,f32 start,s32 end,f32 morph){assert(p==player&&!start&&end==-1);assert(calls==(kind<=1?2:kind==2?1:0));assert(anim==(kind==0?0x7F:kind==1?0x80:kind==2?(foot?0x86:0x85):0x84));assert(rate==(kind==3?1.6f:1.4f)&&morph==(kind<=1?5:kind==2?3:1.6f));if(mutation){assert(kind==3||W(s32,p+0x69C0)==99);W(s32,p+0x69C0)=100;W(f32,p+0x370)=111;W(f32,p+0x374)=222;W(f32,p+0x340)=333;W(f32,p+0x6A14)=444;W(u8,p+0x68DE)=55;}calls++;}
u32 rtas_game(void){assert(calls==(kind<=1?3:kind==2?2:1));if(kind<=1||kind==3)assert(W(s32,player+0x69C0)==1);if(kind==2)assert(W(f32,player+0x370)==0&&W(f32,player+0x374)==0&&W(f32,player+0x340)==0&&W(f32,player+0x6A14)==0&&W(u8,player+0x68DE)==7);if(kind==3)assert(W(u8,player+0x68DE)==4);calls++;if(mutation){W(u32,game+0x5CD8)=0x12345678;W(u32,game+0x5CDC)=0x23456789;}return game;}
static void reset(void){memset(arena,0,sizeof arena);calls=mutation=0;keep=1;kind=0;foot=0;W(f32,0x10036084)=.184799567f;}
int main(void){unsigned cases=0;
 for(unsigned a=0;a<65536;a++){
  reset();angle=(s32)(0x12340000u|a);W(f32,player+0x589C)=a%34;W(f32,player+0x5898)=2;W(f32,0x104A44F8+(a>>3)*8)=.25f;f32 frame=(f32)(a%34);if(!(frame<17))frame-=17;expected_radians=frame*W(f32,0x10036084);assert(randomizer_crawl_move_speed(player)==3&&calls==1);cases++;
 }
 const f32 frames[]={-INFINITY,-1,0,16.999998f,17,17.000002f,34,INFINITY,NAN};
 for(unsigned i=0;i<9;i++)for(unsigned mut=0;mut<2;mut++){
  reset();mutation=mut;angle=0xFFFF;W(f32,player+0x589C)=frames[i];W(f32,player+0x5898)=2;W(f32,0x104A44F8+(0xFFFF>>3)*8)=.25f;new_rate=.125f;new_sine=-.5f;f32 frame=frames[i];if(!(frame<17))frame-=17;expected_radians=frame*W(f32,0x10036084);assert(randomizer_crawl_move_speed(player)==(mut?-.375f:3)&&calls==1);cases++;
 }
 for(unsigned k=0;k<4;k++)for(unsigned mut=0;mut<2;mut++)for(unsigned flag=0;flag<256;flag++){
  reset();kind=k;mutation=mut;W(u32,game+0x5CD8)=flag;W(u32,game+0x5CDC)=flag;foot=(s32)(flag-128);
  if(k==0)assert(randomizer_push_move_initialize(player)==1);else if(k==1)assert(randomizer_pull_move_initialize(player)==1);else if(k==2)assert(randomizer_ladder_up_end_initialize(player,foot)==1);else randomizer_climb_up_start_sub(player);
  assert(calls==(k<=1?4:k==2?3:2));const u32 original=mut?(k==3?0x23456789:0x12345678):flag;assert(W(u32,game+(k==3?0x5CDC:0x5CD8))==(original|(k<=1?0x04000000:k==2?0x02000000:0x10000)));cases++;
 }
 for(unsigned k=0;k<2;k++){reset();kind=k;keep=0;mutation=1;assert((k?randomizer_pull_move_initialize(player):randomizer_push_move_initialize(player))==0&&calls==1&&W(s32,player+0x69C0)==77);cases++;}
 const f32 sticks[]={-INFINITY,-2,-1,-0.0f,0,.5f,1,2,INFINITY,NAN};
 for(unsigned i=0;i<10;i++){reset();W(f32,player+0x6A08)=sticks[i];const f32 expected=1+sticks[i],actual=randomizer_ladder_move_animation_speed(player);assert(actual==expected||isnan(actual)&&isnan(expected));cases++;}
 printf("%u complete small traversal cases passed\n",cases);return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);(path/'test.c').write_text(code)
            subprocess.run(['clang','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror','-Wno-logical-op-parentheses','-I',str(Path(__file__).parent),str(path/'test.c'),'-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)

if __name__=='__main__':unittest.main()
