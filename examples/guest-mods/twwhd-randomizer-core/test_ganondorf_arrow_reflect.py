"""Full reflected-arrow consumer: health boundary, genuine queries and F1 velocity."""
import os
from pathlib import Path
import subprocess,tempfile,unittest
class GanondorfArrowReflect(unittest.TestCase):
 def test_full_consumer(self):
  code=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef uint16_t u16;typedef int16_t s16;typedef int32_t s32;typedef float f32;typedef double f64;
static unsigned char arena[0x20000],table[0x10000];static const u32 actor=0x30000000,game=0x30004000,player=0x30010000,boss=0x30011000,other=0x30012000;
static unsigned boss_x_reads;static void* mem(u32 a){if(a==boss+0x314)boss_x_reads++;if(a>=actor&&a<actor+sizeof arena)return arena+a-actor;if(a>=0x104A44F8&&a<0x104B44F8)return table+a-0x104A44F8;assert(0);return 0;}
#define W(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)mem(a))
#define R_ARROW(t,a) W(t,a)
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static f32 height=10,base=130;static s16 timer=2;static uint32_t zero;
static void* data(u32 a){if(a==0x1047BA90)return &height;if(a==0x1000744C)return &base;if(a==0x1047B68E)return &timer;if(a==0x100073D0)return &zero;assert(0);return 0;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static u32 wwhd_dComIfG_getGameInfo(void);static f64 randomizer_hyrule_sqrt(f64);static f64 randomizer_hyrule_square_magnitude(const f32*);
#include "ganondorf_arrow_reflect.h"
static unsigned found,nonnull,locked,matched,health,mutation,searches,locks,targets,angles,particles,atan_calls,squares,roots,game_calls;static unsigned int* output;static f64 velocity;static f32 expected_x,expected_y,expected_z;
static u32 wwhd_dComIfG_getGameInfo(void){game_calls++;return game;}
static f64 randomizer_hyrule_square_magnitude(const f32* v){squares++;if(squares==1){assert((u32)v==actor+0x33C);return 25;}assert(v[0]==expected_x&&v[1]==0&&v[2]==expected_z);return(f64)v[0]*v[0]+(f64)v[2]*v[2];}
static f64 randomizer_hyrule_sqrt(f64 d){roots++;return roots==1?velocity:sqrt(d);}
s32 randomizer_arrow_find(s32 name,u32 p){assert(name==0xF6);searches++;output=(unsigned int*)p;*output=nonnull?boss:0;return found;}
s32 randomizer_arrow_lock(u32 a){assert(a==game+0x5804);locks++;return locked;}
u32 randomizer_arrow_target(u32 a,s32 i){assert(a==game+0x5804&&!i);targets++;if(mutation==1)W(u8,boss+0x3A1)=26;return matched?boss:other;}
s32 randomizer_arrow_angle(u32 p,u32 chest){assert(p==player+0x314);angles++;const uint32_t* words=(const uint32_t*)chest;const f32* values=(const f32*)chest;assert(words[0]==0x7FC12345&&words[2]==0x80000000&&values[1]==140);if(mutation==2)*output=other;return -123;}
u32 randomizer_arrow_particle(u32 control,u32 group,u32 id,u32 position,u32 angle,u32 scale,u32 alpha,u32 cb,s32 room,u32 primary,u32 env,u32 extra){assert(control==0xABCDEF&&!group&&id==0x3EE&&position==player+0x314&&!angle&&!scale&&alpha==255&&!cb&&room==-1&&!primary&&!env&&!extra);particles++;assert(W(s16,actor+0x724)==17);return 0x12345678;}
s32 randomizer_arrow_atan(f64 y,f64 x){atan_calls++;if(atan_calls==1){assert(y==-(f64)expected_y&&x==-sqrt((f64)expected_x*expected_x+(f64)expected_z*expected_z));if(mutation==3){W(f32,actor+0x33C)=7;W(f32,actor+0x344)=9;}return 0x8001;}assert(y==-(f64)(mutation==3?7:expected_x)&&x==-(f64)(mutation==3?9:expected_z));return 0x12345;}
static f32 multiply(f64 a,f64 b){uint64_t bits;memcpy(&bits,&b,8);bits=(bits&UINT64_C(0xFFFFFFFFF8000000))+(bits&UINT64_C(0x08000000));memcpy(&b,&bits,8);return(f32)(a*b);}
static void expected(unsigned yaw,unsigned pitch){f32 sy=W(f32,0x104A44F8+(yaw&0xFFF8)),cx=W(f32,0x104A44FC+(pitch&0xFFF8)),sx=W(f32,0x104A44F8+(pitch&0xFFF8)),cy=W(f32,0x104A44FC+(yaw&0xFFF8));expected_x=multiply(multiply(sy,cx),velocity);expected_y=multiply(-sx,velocity);expected_z=multiply(multiply(cy,cx),velocity);}
static void reset(void){memset(arena,0,sizeof arena);W(u32,game+0x5B34)=player;W(u32,game+0x5AB0)=0xABCDEF;W(u32,boss+0x314)=0x7FC12345;W(u32,boss+0x31C)=0x80000000;W(u8,boss+0x3A1)=health;W(s16,player+0x32A)=0x7FFF;W(s16,player+0x3D2)=2;W(s16,player+0x3D0)=-321;searches=locks=targets=angles=particles=atan_calls=squares=roots=game_calls=boss_x_reads=0;}
int main(void){unsigned cases=0;for(unsigned i=0;i<65536;i+=8){W(f32,0x104A44F8+i)=(f32)sin((double)i*6.283185307179586/65536);W(f32,0x104A44FC+i)=(f32)cos((double)i*6.283185307179586/65536);}velocity=5.000000000000001;mutation=0;
for(health=0;health<256;health++)for(found=0;found<2;found++)for(nonnull=0;nonnull<2;nonnull++)for(locked=0;locked<2;locked++)for(matched=0;matched<2;matched++){
 reset();unsigned candidate=found&&nonnull&&locked&&matched,allow=candidate&&health<=25;expected(0x8001,allow?123:(uint16_t)-321);randomizer_ganondorf_arrow_reflect(actor);assert(searches==1&&locks==(found&&nonnull)&&targets==(found&&nonnull&&locked)&&angles==allow&&particles==allow&&boss_x_reads==candidate);assert(W(u32,boss+0xB0)==(allow?0x23:0));assert(W(s16,actor+0x328)==(s16)0x8001&&W(s16,actor+0x32A)==0x2345&&W(s16,actor+0x32C)==0&&squares==2&&roots==2);assert(W(f32,actor+0x33C)==expected_x&&W(f32,actor+0x340)==expected_y&&W(f32,actor+0x344)==expected_z);cases++;}
found=nonnull=locked=matched=1;health=25;
for(mutation=1;mutation<=3;mutation++){reset();expected(0x8001,mutation==1?(uint16_t)-321:123);randomizer_ganondorf_arrow_reflect(actor);assert(angles==(mutation!=1));if(mutation==2)assert(W(u32,boss+0xB0)==0&&W(u32,other+0xB0)==0x23);if(mutation==3)assert(W(f32,actor+0x33C)==7&&W(f32,actor+0x344)==9);cases++;}
mutation=0;found=0;for(unsigned yaw=0;yaw<65536;yaw++){reset();W(s16,player+0x32A)=(s16)yaw;W(s16,player+0x3D2)=0;expected(yaw,(uint16_t)-321);randomizer_ganondorf_arrow_reflect(actor);assert(!angles&&atan_calls==2);cases++;}
/* A wider F1 must survive callbacks and native fmuls'25-bit operand rounding. */
velocity=1.0000000596046448;reset();expected(0x8001,(uint16_t)-321);randomizer_ganondorf_arrow_reflect(actor);assert(W(f32,actor+0x33C)==expected_x);cases++;
printf("Ganondorf reflected arrow: %u cases passed\n",cases);}
'''
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory);(root/'test.c').write_text(code)
   subprocess.run([os.environ.get("CC", "cc"),'-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(root/'test.c'),'-lm','-o',str(root/'test')],check=True)
   subprocess.run([str(root/'test')],check=True)
if __name__=='__main__':unittest.main()
