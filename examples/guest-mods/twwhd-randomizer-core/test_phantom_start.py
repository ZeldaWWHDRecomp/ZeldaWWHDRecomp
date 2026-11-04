"""Complete Phantom start: one-sided height gate and callback-sensitive modes."""
from pathlib import Path
import subprocess
import tempfile
import unittest

class PhantomStart(unittest.TestCase):
    def test_consumer(self):
        source=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef int8_t s8;typedef uint16_t u16;typedef int16_t s16;typedef int32_t s32;typedef float f32;typedef double f64;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static unsigned char arena[0x30000];static const u32 actor=0x30000000,game=0x30010000,player=0x30020000;
static void* mem(u32 a){if(a>=actor&&a<actor+sizeof arena)return arena+a-actor;if(a>0xFFFFFFFFu)return(void*)a;assert(0);return 0;}
#define R_PGM(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)mem(a))
static unsigned games,subtractions,sqrts,animations,mutes,voices,sounds,subbgms,transforms,positions,calcs;static int mutation;static s16 reg;static f64 distance;static f32 square;static s32 anim;static f32 target,maxstep;static f32 offset_seen[3];
static u32 pgm_game(void){games++;if(mutation==1&&games==1)R_PGM(s16,actor+0x5BC)=2;return game;}
static void pgm_vector_subtract(u32 a,u32 out,u32 b){assert(a==player+0x314&&b==actor+0x2EC);for(unsigned i=0;i<3;i++)R_PGM(f32,out+i*4)=R_PGM(f32,a+i*4)-R_PGM(f32,b+i*4);subtractions++;}
static f32 pgm_fma(f32 a,f32 b,f32 c){return fmaf(a,b,c);}
static f64 pgm_sqrt(f64 s){sqrts++;square=s;if(mutation==2)R_PGM(u8,actor+0x3D9)=3;return distance!=-1?distance:sqrt(s);}
static void pgm_animation(u32 a,s32 index,f32 blend,u32 loop,f32 speed,s32 sound){assert(a==actor&&loop==2&&speed==1&&sound==-1);assert((index==0x16&&blend==0)||(index==0x17&&blend==5)||(index==0x10&&blend==10));animations++;anim=index;if(mutation==3&&index==0x10){reg=25;R_PGM(s16,actor+0x5D8)=100;}}
static s32 pgm_search_angle(u32 a,u32 t){assert(a==actor&&t==player&&games==2);return 0xF123;}
static void pgm_voice(u32 a,u32 id){assert(a==actor&&id==0x4939);voices++;R_PGM(s16,actor+0x5BC)=99;}
static s16 pgm_reg_short(u32 c,u32 i){assert(c==0&&i==0);return reg;}
static f32 pgm_reg_float(u32 c,u32 i){assert(c==0&&(i==5||i==7));return i==5?2.0f:3.0f;}
static void pgm_effect_sound(u32 a,u32 id,u32 volume){assert(a==actor&&id==0x5152&&volume==0);sounds++;if(mutation==5)R_PGM(f32,actor+0x370)=40;}
static u32 pgm_matrix(void){return 123;}
static void pgm_y_rotation(u32 m,s32 angle){assert(m==123&&angle==-4);if(mutation==6)R_PGM(f32,actor+0x370)=900;}
static void pgm_transform(u32 input,u32 out){assert(out==actor+0x33C);for(unsigned i=0;i<3;i++){offset_seen[i]=R_PGM(f32,input+4*i);R_PGM(f32,out+4*i)=offset_seen[i];}transforms++;}
static void pgm_vector_add_inplace(u32 a,u32 b,u32 out){assert(a==actor+0x314&&b==actor+0x33C&&out==a);for(unsigned i=0;i<3;i++)R_PGM(f32,out+4*i)=R_PGM(f32,a+4*i)+R_PGM(f32,b+4*i);positions++;if(mutation==7)R_PGM(s16,actor+0x5D8)=0;}
static void pgm_calculate(u32 p,f32 t,f32 ratio,f32 maximum){assert(p==actor+0x370&&ratio==1);calcs++;target=t;maxstep=maximum;}
#include "phantom_start.h"
void pgs_bgm_mute(s32 frames){assert(frames==30&&R_PGM(s16,actor+0x5BC)==1&&R_PGM(s16,actor+0xD88)==1);mutes++;}
void pgs_sub_bgm(u32 id){assert(id==0x80000041&&R_PGM(s8,actor+0x63C)==1);subbgms++;if(mutation==4){reg=-50;R_PGM(s16,actor+0x5D8)=26;}}
static void reset(void){memset(arena,0,sizeof arena);games=subtractions=sqrts=animations=mutes=voices=sounds=subbgms=transforms=positions=calcs=0;mutation=0;reg=0;distance=-1;square=-1;anim=0;target=maxstep=-1;R_PGM(u32,game+0x5B2C)=player;R_PGM(u8,actor+0x3D9)=10;R_PGM(u32,actor+0x39C)=0xFFFF;R_PGM(f32,actor+0x370)=20;R_PGM(s16,actor+0x32A)=-4;}
int main(void){unsigned cases=0;
 for(int y=-2000;y<=1200;y+=25)for(int x=-120;x<=120;x+=5){reset();R_PGM(f32,player+0x314)=x;R_PGM(f32,player+0x318)=y;randomizer_phantom_start(actor);const unsigned pass=y<1000&&abs(x)<100;assert(animations==pass&&mutes==pass&&sqrts==(y<1000));assert(R_PGM(u32,actor+0x39C)==0&&R_PGM(s16,actor+0x5E2)==3);cases++;}
 const f32 ys[]={999.99994f,1000,1000.00006f,-INFINITY,INFINITY,NAN};
 for(unsigned i=0;i<6;i++){reset();R_PGM(f32,player+0x318)=ys[i];randomizer_phantom_start(actor);assert(mutes==(i==0||i==3)&&sqrts==(i==0||i==3));cases++;}
 const f64 ds[]={99.99999999999999,100,100.00000000000001,INFINITY,NAN};
 for(unsigned i=0;i<5;i++){reset();distance=ds[i];randomizer_phantom_start(actor);assert(mutes==(i==0));cases++;}
 reset();mutation=1;randomizer_phantom_start(actor);assert(voices==1&&R_PGM(s16,actor+0x5BC)==3&&!subtractions);cases++;
 reset();mutation=2;distance=40;randomizer_phantom_start(actor);assert(!mutes);cases++;
 reset();R_PGM(s16,actor+0x5BC)=1;randomizer_phantom_start(actor);assert(games==2&&R_PGM(s16,actor+0x32A)==(s16)0xF123);cases++;
 reset();R_PGM(s16,actor+0x5BC)=4;mutation=3;randomizer_phantom_start(actor);assert(anim==0x10&&subbgms==1&&sounds==1&&positions==1&&target==23&&maxstep==2.5f);cases++;
 reset();R_PGM(s16,actor+0x5BC)=5;R_PGM(s16,actor+0x5D8)=75;mutation=4;randomizer_phantom_start(actor);assert(subbgms==1&&!sounds&&positions==1);cases++;
 for(int timer=-100;timer<=100;timer++)for(int r=-100;r<=100;r+=10){reset();R_PGM(s16,actor+0x5BC)=5;R_PGM(s16,actor+0x5D8)=timer;reg=r;randomizer_phantom_start(actor);assert(subbgms==(timer==(s16)(r+75))&&sounds==(timer<=(s16)(r+75)));assert(offset_seen[0]==0&&offset_seen[1]==10&&offset_seen[2]==-20&&target==(timer?23:0)&&maxstep==2.5f);cases++;}
 for(int mut=5;mut<=7;mut++){reset();R_PGM(s16,actor+0x5BC)=5;mutation=mut;randomizer_phantom_start(actor);assert(offset_seen[1]==(mut==5?20:10)&&offset_seen[2]==(mut==5?-40:-20));assert(target==0);cases++;}
 for(int mode=-10;mode<15;mode++)if(mode!=0&&mode!=1&&mode!=2&&mode!=4&&mode!=5){reset();R_PGM(s16,actor+0x5BC)=mode;randomizer_phantom_start(actor);assert(!animations&&!positions&&!subtractions&&R_PGM(s16,actor+0x5BC)==mode);cases++;}
 printf("%u complete Phantom start cases passed\n",cases);return 0;}
'''
        source=source.replace('#include <stdio.h>','#include <stdio.h>\n#include <stdlib.h>')
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);(path/'test.c').write_text(source)
            subprocess.run(['clang','-O2','-ffp-contract=off','-I',str(Path(__file__).parent),str(path/'test.c'),'-lm','-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)

if __name__=='__main__':unittest.main()
