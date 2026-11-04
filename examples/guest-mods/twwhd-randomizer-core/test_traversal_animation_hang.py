"""Complete hanging and sidle consumers: native math, callbacks and tails."""
from pathlib import Path
import subprocess
import tempfile
import unittest

class TraversalAnimationHang(unittest.TestCase):
    def test_consumers(self):
        code=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
typedef uintptr_t u32;typedef uint16_t u16;typedef uint8_t u8;typedef int16_t s16;typedef int32_t s32;typedef float f32;typedef double f64;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static unsigned char arena[0x20000],data[0x500000];static const u32 player=0x30000000,game=0x30010000;
static void* mem(u32 a){if(a>=player&&a<player+sizeof arena)return arena+a-player;if(a>=0x10000000&&a<0x10500000)return data+a-0x10000000;if(a>0xFFFFFFFFu)return(void*)a;assert(0);return 0;}
#define W(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)mem(a))
#define R_TAH(t,a) W(t,a)
#define WWHD_GAME_DATA(a,t) W(t,a)
#include "traversal_animation_hang.h"
static unsigned calls,grounds,mutation,moves;static f64 distance,height;static f32 expected_x,expected_z,expected_ratio,expected_blend,expected_rate,expected_morph;static s32 expected_move,expected_idle;static f32 last_ratio;
f64 rtah_square_distance(u32 a,u32 b){assert(W(f32,a)==10&&W(f32,a+4)==0&&W(f32,a+8)==30&&W(f32,b)==10&&W(f32,b+4)==0&&W(f32,b+8)==30);calls++;return 0;}
f64 rtah_sqrt(f64 square){assert(!square&&calls==1);calls++;if(mutation==1)W(f32,player+0x780)=-19;return distance;}
f64 rtah_ground(u32 bg,u32 check){assert(bg==game+0x12A0&&check==player+0xB14&&calls==3&&W(f32,player+0xB38)==expected_x&&W(f32,player+0xB3C)==30&&W(f32,player+0xB40)==expected_z);grounds++;calls++;if(mutation==2)W(f32,player+0x7330)=(f32)height+31;if(mutation==3)W(f32,player+0x7330)=(f32)height;return height;}
void rtah_common(u32 p,s32 proc){assert(p==player&&proc==0x31&&calls==4);calls++;if(mutation==3)W(s16,p+0x697C)=0x1234;}
void rtah_animation(u32 p,s32 anim,f32 rate,f32 start,s32 end,f32 morph){assert(p==player&&anim==0x4E&&rate==1.6f&&!start&&end==5&&morph==2.5f&&calls==5);calls++;if(mutation==3){height=9999;W(s16,p+0x697C)=0x2345;}}
u32 rtah_game(void){if(calls==2){assert(!grounds);if(mutation==3){W(f32,player+0x732C)=999;W(f32,player+0x7334)=888;}}else assert(calls==6&&W(f32,player+0x340)==0&&W(f32,player+0x6A14)==0&&W(f32,player+0x314)==expected_x&&W(f32,player+0x31C)==expected_z);calls++;W(u32,game+0x5CD8)=0x12345678;return game;}
static unsigned same(f32 a,f32 b){return a==b||isnan(a)&&isnan(b);}
void rtah_move_animation(u32 p,f32 blend,f32 rate,f32 start,s32 moving,s32 idle,s32 mode,f32 morph){assert(p==player&&same(blend,expected_blend)&&rate==expected_rate&&!start&&moving==expected_move&&idle==expected_idle&&mode==5&&same(morph,expected_morph));assert(same(W(f32,p+0x69F0),expected_ratio));moves++;last_ratio=W(f32,p+0x69F0);if(mutation){W(s16,p+0x58B2)=-123;W(u32,p+0x580C)=0x30018000;W(f32,p+0x69F0)=55;}}
static void reset(void){memset(arena,0,sizeof arena);calls=grounds=mutation=moves=0;distance=0;height=20;W(f32,player+0x732C)=10;W(f32,player+0x7330)=20;W(f32,player+0x7334)=30;W(f32,player+0x314)=10;W(f32,player+0x31C)=30;W(f32,player+0x780)=10;W(u32,player+0x580C)=0x30019000;W(s16,player+0x58B2)=30;}
static void hang_offsets(unsigned angle){W(u16,player+0x697C)=angle;const f32 sine=(f32)((int)(angle%17)-8)/8,cosine=(f32)((int)(angle%19)-9)/9;W(f32,0x104A44F8+(angle>>3)*8)=sine;W(f32,0x104A44FC+(angle>>3)*8)=cosine;expected_x=-fmaf(1.5f,sine,-10);expected_z=-fmaf(1.5f,cosine,-30);}
int main(void){unsigned cases=0;
 for(unsigned angle=0;angle<65536;angle++){reset();hang_offsets(angle);assert(randomizer_hang_wall_catch_initialize(player)==1&&calls==7&&grounds==1&&W(f32,player+0x318)==20&&W(u16,player+0x32A)==(u16)(angle+0x8000)&&W(u32,game+0x5CD8)==(0x12345678|0x100));cases++;}
 const f64 distances[]={29.999999999999996,30,30.000000000000004,INFINITY,NAN};
 for(unsigned i=0;i<5;i++){reset();hang_offsets(0);distance=distances[i];const unsigned pass=i==0||i==1||i==4;assert(randomizer_hang_wall_catch_initialize(player)==(s32)pass&&grounds==pass);cases++;}
 const f64 heights[]={20,50.099999,50.100001,-10.099999,-10.100001,INFINITY,NAN};
 for(unsigned i=0;i<7;i++){reset();hang_offsets(0);height=heights[i];const unsigned pass=!(__builtin_fabsf((f32)(height-20))>30.1f);assert(randomizer_hang_wall_catch_initialize(player)==(s32)pass);if(pass)assert(same(W(f32,player+0x318),(f32)height));cases++;}
 for(unsigned mut=1;mut<=3;mut++){reset();hang_offsets(100);mutation=mut;distance=10;const unsigned pass=mut==3;assert(randomizer_hang_wall_catch_initialize(player)==(s32)pass);if(pass)assert(W(f32,player+0x318)==20&&W(u16,player+0x32A)==0xA345);cases++;}
 const f32 numbers[]={-INFINITY,-2,-.9f,-0.0f,0,.25f,.8999999f,.9f,.9000001f,1,2,INFINITY,NAN};
 for(unsigned crouch=0;crouch<2;crouch++)for(unsigned direction=0;direction<4;direction++)for(unsigned n=0;n<13;n++)for(unsigned d=0;d<13;d++)for(unsigned mut=0;mut<2;mut++){
  reset();mutation=mut;W(u32,player+0x3B8)=crouch?0x10000:0;W(u8,player+0x68D4)=direction;W(f32,player+0x6A14)=numbers[n];W(f32,player+0x3C4)=numbers[d];const f32 ratio=numbers[n]/numbers[d];expected_rate=crouch?1.8f:2;expected_idle=crouch?0x43:0x42;expected_move=crouch?(direction==2?0x47:0x46):(direction==2?0x45:0x44);expected_morph=1.25f;
  if(ratio<0){expected_blend=1;expected_ratio=1;}else if(ratio<.9f){expected_blend=1-ratio/.9f;expected_ratio=1;}else{expected_blend=0;const f32 first=1-(crouch?.98f:1),second=ratio-.9f,product=first*second;expected_ratio=1-product/.1f;}
  randomizer_sidle_blend_animation(player,.25);assert(moves==1&&W(s16,player+0x58A2)==28&&W(f32,player+0x58A8)==0&&W(f32,player+0x58AC)==(mut?-123.001f:29.999f)&&W(f32,mut?0x30018000:0x30019000)==W(f32,player+0x58AC));assert(same(W(f32,player+0x69F0),mut?55:last_ratio));cases++;
 }
 const f32 edge[]={0,-0.0f,1,-1,0x1p-126f,0x1p100f,INFINITY,-INFINITY,NAN};
 for(unsigned a=0;a<9;a++)for(unsigned b=0;b<9;b++)for(unsigned c=0;c<9;c++){
  f32 (*volatile oracle)(f32,f32,f32)=fmaf; const f32 expected=-oracle(edge[a],edge[b],-edge[c]),actual=rtah_fnmsub(edge[a],edge[b],edge[c]);assert(same(actual,expected));if(actual==0&&expected==0)assert(!!signbit(actual)==!!signbit(expected));cases++;
 }
 for(unsigned i=0;i<65536;i++){
  union {f64 value;unsigned long long bits;} input={.bits=0x3FF0000000000000ull+((unsigned long long)i<<16)}, rounded=input;
  rounded.bits=(rounded.bits&0xFFFFFFFFF8000000ull)+(rounded.bits&0x8000000ull);
  const f32 actual=rtah_morph(input.value),expected=(f32)(5.0*rounded.value);
  assert(same(actual,expected));cases++;
 }
 printf("%u complete hanging/sidle cases passed\n",cases);return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);(path/'test.c').write_text(code)
            subprocess.run(['clang','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror','-Wno-logical-op-parentheses','-I',str(Path(__file__).parent),str(path/'test.c'),'-lm','-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)

if __name__=='__main__':unittest.main()
