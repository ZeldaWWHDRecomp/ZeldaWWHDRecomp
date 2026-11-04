"""Ho Ho execution keeps dispatch and callback-driven position updates."""
import os
from pathlib import Path
import subprocess,tempfile,unittest
class HoHoExecute(unittest.TestCase):
 def test_execute(self):
  code=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int16_t s16;typedef uint16_t u16;typedef uint8_t u8;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static u8 actor[0x1000],game[0x6000],table[32],matrix[64];static u32 saveptr=123;
static u8 entries[32];static float dat[32],height=150;static unsigned arg,slotmode,eventmode,command,sw,seq,dispatches;
static u32 wwhd_dComIfG_getGameInfo(void){return(u32)game;}
static u32 randomizer_hoho_argument(u32 a){assert(a==(u32)actor);return arg;}
static u32 randomizer_hoho_switch_bit(u32 a){assert(a==(u32)actor);sw++;return 255;}
static void* memory(u32 a){if(a==1)return&saveptr;if(a==0x101BB348)return entries;if(a==0x1048D0CC)return matrix;if(a==0x10015FB0)return&height;assert(a>=0x101BB378&&a<0x101BB3F8);return(u8*)dat+a-0x101BB378;}
#define WWHD_GAME_DATA(a,t) (*(t*)memory(a))
static void call(u32 target,u32 object);
#define RANDOMIZER_HOHO_CALL call
#include "hoho_execute.h"
static void step(unsigned expected){assert(seq++==expected);}
void randomizer_hoho_attention(u32 a){assert(a==(u32)actor);step(0);game[0x5292]=eventmode;*(u16*)(actor+0xF8)=command;}
void randomizer_hoho_order_check(u32 a){assert(a==(u32)actor);step(1);actor[0x8BA]=slotmode*2;}
static void call(u32 target,u32 object){step(2);assert(target==0x12345678&&object==(u32)actor+4);dispatches++;}
void randomizer_hoho_event(u32 a){assert(a==(u32)actor);step(2);}
void randomizer_hoho_order(u32 a){assert(a==(u32)actor);step(3);}
void randomizer_hoho_texture(u32 a){assert(a==(u32)actor);step(4);}
void randomizer_hoho_animation(u32 a){assert(a==(u32)actor);step(5);}
void randomizer_hoho_move(u32 a,u32 v){assert(a==(u32)actor&&v==a+0x654);step(6);}
void randomizer_hoho_correct(u32 a,u32 b){assert(a==(u32)actor+0x450&&b==(u32)game+0x12A0);step(7);*(float*)(actor+0x314)=1;*(float*)(actor+0x318)=2;*(float*)(actor+0x31C)=3;}
void randomizer_hoho_collision(u32 a,u32 c,u32 v,f32 r,f32 h){assert(a==(u32)actor&&c==a+0x690);step(8);assert(r==7&&h==150);float* p=(float*)v;assert(p[0]==1&&p[1]==2&&p[2]==3);dat[6]=10;dat[7]=20;dat[8]=30;*(s16*)(actor+0x322)=-123;}
void randomizer_hoho_rotate(u32 m,s32 angle){assert(m==(u32)matrix&&angle==-123);step(9);}
void randomizer_hoho_transform(u32 m,u32 s,u32 t){assert(m==(u32)matrix&&s==t);step(10);float* p=(float*)s;assert(p[0]==10&&p[1]==20&&p[2]==30);p[0]+=1;p[1]+=2;p[2]+=3;}
void randomizer_hoho_add(u32 a,u32 b,u32 t){step(11);assert(a==t&&b==(u32)actor+0x314);float* p=(float*)a;for(unsigned i=0;i<3;i++)p[i]+=((float*)b)[i];dat[9]=100;*(float*)(actor+0x318)=200;}
void randomizer_hoho_look(u32 a){step(12);assert(a==(u32)actor);assert(*(float*)(actor+0x390)==12&&*(float*)(actor+0x394)==24&&*(float*)(actor+0x398)==36);assert(*(float*)(actor+0x37C)==1&&*(float*)(actor+0x380)==300&&*(float*)(actor+0x384)==3);}
void randomizer_hoho_matrix(u32 a){assert(a==(u32)actor);step(13);}
int main(void){for(arg=0;arg<256;arg++)for(slotmode=0;slotmode<2;slotmode++)for(eventmode=0;eventmode<2;eventmode++)for(command=0;command<3;command++){
 memset(actor,0,sizeof actor);memset(entries,0,sizeof entries);memset(dat,0,sizeof dat);seq=sw=dispatches=0;dat[14]=7;
 /* HD member descriptors: direct entry0 and virtual entry1. Host table pointers use pointer width. */
 *(s16*)(entries)=4;*(s16*)(entries+2)=-1;*(uint32_t*)(entries+4)=0x12345678;
 *(s16*)(entries+16)=4;*(s16*)(entries+18)=0;*(s16*)(entries+22)=16;
 *(u32*)(actor+20)=(u32)table;*(u32*)(table+4)=0x12345678;
 assert(randomizer_hoho_execute((u32)actor)==0);assert(seq==14&&sw==(arg==9));assert(dispatches==(!eventmode||command==1));
 }
return 0;}
'''
  with tempfile.TemporaryDirectory() as t:
   p=Path(t);(p/'test.c').write_text(code)
   subprocess.run([os.environ.get("CC", "cc"),'-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
