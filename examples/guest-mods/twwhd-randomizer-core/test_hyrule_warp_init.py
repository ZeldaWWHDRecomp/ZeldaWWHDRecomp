"""Warp initialization preserves callback reloads and does not force mail."""
import os
from pathlib import Path
import subprocess,tempfile,unittest
class HyruleWarp(unittest.TestCase):
 def test_init(self):
  code=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef uint16_t u16;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static u8 actor[0x500],model[0x200],other[0x200],save[0x700],game[0x6000],eventname[32];static unsigned int matrix[12];
static float constants[32];static unsigned seq,shards,awakened,sentinel,ended,on;static float returned;
static void* data(u32 a){if(a==0x1048D0CC)return matrix;if(a==0x100421BC)return eventname;assert(a>=0x10042148&&a<=0x100421B8);return(u8*)constants+a-0x10042148;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static u32 randomizer_daily_save(void){return(u32)save;}
static u32 randomizer_daily_event(void){return(u32)save+0x644;}
static u32 wwhd_dComIfG_getGameInfo(void){return(u32)game;}
static s32 wwhd_dSv_player_collect_c_getTriforceNum(u32 p){assert(p==(u32)save+0xD4);return (s32)shards;}
static u32 wwhd_dSv_event_c_isEventBit(u32 e,u32 f){assert(e==(u32)save+0x644&&f==0x2D02);return awakened;}
static void wwhd_dSv_event_c_onEventBit(u32 e,u32 f){assert(e==(u32)save+0x644&&f==0x1820);on++;}
static void randomizer_mail_cull(u32 a,f32 x,f32 y,f32 z,f32 X,f32 Y,f32 Z){assert(seq++==0&&a==(u32)actor);assert(x==-300&&y==0&&z==-300&&X==300&&Y==500&&Z==300);assert(*(u32*)(actor+0x348)==(u32)model+0xC8);}
#include "hyrule_warp_init.h"
f32 randomizer_hyrule_water(u32 a,u32 p){assert(seq++==1&&a==(u32)actor);float* v=(float*)p;assert(v[0]==1&&v[1]==2002&&v[2]==3);assert(*(float*)(actor+0x364)==1);*(u32*)(actor+0x3B4)=(u32)other;*(float*)(actor+0x330)=4;*(float*)(actor+0x334)=5;*(float*)(actor+0x338)=6;return returned;}
void randomizer_hyrule_translation(u32 m,f32 x,f32 y,f32 z){assert(seq++==2&&m==(u32)matrix);assert(x==1&&y==(sentinel?0:42)&&z==3);assert(*(float*)(other+0xBC)==4&&*(float*)(other+0xC0)==5&&*(float*)(other+0xC4)==6);for(unsigned i=0;i<12;i++)matrix[i]=0x3F800000+i;*(u32*)(actor+0x3B4)=(u32)model;}
void randomizer_hyrule_end_animation(u32 a){assert(a==(u32)actor&&actor[0x3E8]==1);ended++;}
s32 randomizer_hyrule_event_index(u32 m,u32 n,u32 h){assert(seq++==3&&m==(u32)game+0x52C4&&n==(u32)eventname&&h==255);for(unsigned i=0;i<12;i++)assert(*(unsigned*)(model+0xC8+i*4)==matrix[i]);assert(*(float*)(actor+0x408)==1&&*(float*)(actor+0x40C)==1&&*(float*)(actor+0x410)==1);return -123;}
int main(void){for(shards=0;shards<256;shards++)for(awakened=0;awakened<2;awakened++)for(sentinel=0;sentinel<2;sentinel++){
 memset(actor,0,sizeof actor);memset(constants,0,sizeof constants);constants[0]=1;constants[1]=0;constants[24]=-300;constants[25]=300;constants[26]=500;constants[27]=2000;constants[28]=-1000000000.0f;
 *(u32*)(actor+0x3B4)=(u32)model;*(float*)(actor+0x314)=1;*(float*)(actor+0x318)=2;*(float*)(actor+0x31C)=3;seq=ended=on=0;returned=sentinel?-1000000000.0f:42;
 randomizer_hyrule_warp_init((u32)actor);assert(seq==4&&ended==(shards>=8)&&on==awakened);assert(*(u16*)(actor+0x3EE)==(u16)-123);assert(*(unsigned*)(actor+0x3E4)==0);
 }
return 0;}
'''
  with tempfile.TemporaryDirectory() as t:
   p=Path(t);(p/'test.c').write_text(code)
   subprocess.run([os.environ.get("CC", "cc"),'-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
