"""Real shard gate, double threshold boundaries and captured-emitter mutation seams."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class HyruleWarpExecute(unittest.TestCase):
    def test_complete_consumer(self):
        header = Path(__file__).with_name('hyrule_warp_execute.h')
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <stdio.h>
typedef uint32_t u32;typedef uint8_t u8;typedef int32_t s32;typedef float f32;typedef double f64;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 memory[0x80000],before_actor[0x500],before_save[0x3000],before_emitters[0x600];
static const u32 actor=0x10000,game=0x20000,player=0x30000,save=0x40000,save2=0x42000,emitter1=0x50000,emitter2=0x50300;
static u32 save_pointer,zero_word;static f32 native_threshold;
#define R_HYRULE_EXEC(t,a) (*(volatile t*)(memory+(u32)(a)-0x10000))
#define WWHD_GAME_DATA(a,t) (*(t*)((a)==0x1004214C?(void*)&zero_word:(void*)&native_threshold))
static u32 randomizer_daily_save(void){return save_pointer;}
u32 wwhd_dComIfG_getGameInfo(void);
s32 wwhd_dSv_player_collect_c_getTriforceNum(u32 pointer);
#include "hyrule_warp_execute.h"
static unsigned trace[16],trace_size,checks;static s32 shards,warp_result;static f64 distance,square_token;
static u32 delta_words[3];static int mutate_getter,mutate_animation,mutate_warp;
static u32 captured_emitter;static f32 animate_threshold;static u32 animate_status;
static void mark(unsigned value){assert(trace_size<16);trace[trace_size++]=value;}
u32 wwhd_dComIfG_getGameInfo(void){mark(1);if(mutate_getter){R_HYRULE_EXEC(u32,actor+0x3CC)=emitter2;save_pointer=save2;}return game;}
void randomizer_hyrule_subtract(u32 source,f32* output,u32 other){
 mark(2);assert(source==player+0x314&&other==actor+0x314);
 memcpy(output,delta_words,sizeof delta_words);
}
f64 randomizer_hyrule_square_magnitude(const f32* vector){
 mark(3);u32 words[3];memcpy(words,vector,sizeof words);assert(words[0]==delta_words[0]&&words[1]==zero_word&&words[2]==delta_words[2]);return square_token;
}
f64 randomizer_hyrule_sqrt(f64 square){mark(4);assert(!memcmp(&square,&square_token,sizeof square));return distance;}
s32 wwhd_dSv_player_collect_c_getTriforceNum(u32 pointer){mark(5);assert(pointer==save_pointer+0xD4);return shards;}
void randomizer_hyrule_animate(u32 object){
 mark(6);assert(object==actor&&R_HYRULE_EXEC(u8,actor+0x3E8)==1);
 if(mutate_animation){R_HYRULE_EXEC(u32,actor+0x3CC)=emitter2;R_HYRULE_EXEC(u32,emitter1+0x254)=animate_status;native_threshold=animate_threshold;R_HYRULE_EXEC(u8,actor+0x3E8)=0;}
}
s32 randomizer_hyrule_check_warp(u32 object){
 mark(7);assert(object==actor);
 if(captured_emitter){const u32 expected=(distance<(f64)native_threshold)?(mutate_animation?animate_status:0xA5A5A5A5u)&~1u:(mutate_animation?animate_status:0xA5A5A5A5u)|1u;assert(R_HYRULE_EXEC(u32,captured_emitter+0x254)==expected);}
 if(mutate_warp){R_HYRULE_EXEC(s32,actor+0x3E4)=42;R_HYRULE_EXEC(u32,emitter2+0x254)=0x12345678;}
 return warp_result;
}
static void reset(void){
 memset(memory,0x71,0x500);memset(memory+save-0x10000,0x6B,0x3000);memset(memory+emitter1-0x10000,0x93,0x600);
 R_HYRULE_EXEC(u32,game+0x5B2C)=player;R_HYRULE_EXEC(u32,actor+0x3CC)=captured_emitter=emitter1;
 R_HYRULE_EXEC(u32,emitter1+0x254)=0xA5A5A5A5u;R_HYRULE_EXEC(u8,actor+0x3E8)=7;R_HYRULE_EXEC(s32,actor+0x3E4)=-77;
 trace_size=0;zero_word=0;native_threshold=2000.0f;save_pointer=save;
 delta_words[0]=0x80000000u;delta_words[1]=0x7FA12345u;delta_words[2]=0x7FA54321u;
 square_token=123456789.125;mutate_getter=mutate_animation=mutate_warp=0;animate_threshold=2000;animate_status=0x13579BDF;
}
static void remember(void){memcpy(before_actor,memory,0x500);memcpy(before_save,memory+save-0x10000,0x3000);memcpy(before_emitters,memory+emitter1-0x10000,0x600);}
static void isolated(void){
 assert(!memcmp(before_save,memory+save-0x10000,0x3000));
 for(unsigned i=0;i<0x500;i++)if(i!=0x3E8&&!(i>=0x3E4&&i<0x3E8)&&!(mutate_getter||mutate_animation?i>=0x3CC&&i<0x3D0:0))assert(memory[i]==before_actor[i]);
 for(unsigned i=0;i<0x600;i++)if(!(captured_emitter==emitter1&&shards>=8&&i>=0x254&&i<0x258)&&!(mutate_warp&&shards>=8&&i>=0x554&&i<0x558))assert(memory[emitter1-0x10000+i]==before_emitters[i]);checks++;
}
int main(void){
 const s32 counts[]={INT_MIN,-1,0,1,2,3,4,5,6,7,8,9,INT_MAX};
 const f64 distances[]={-INFINITY,-1.0,0.0,1999.0,1999.9999999999998,2000.0,2000.0000000000002,2001.0,INFINITY,NAN};
 const s32 results[]={0,1,2,-1};
 for(unsigned n=0;n<sizeof counts/sizeof *counts;n++)for(unsigned d=0;d<sizeof distances/sizeof *distances;d++)for(unsigned r=0;r<4;r++)for(unsigned present=0;present<2;present++)for(unsigned mutate=0;mutate<2;mutate++){
  reset();shards=counts[n];distance=distances[d];warp_result=results[r];mutate_getter=mutate;
  if(!present)R_HYRULE_EXEC(u32,actor+0x3CC)=captured_emitter=0;
  remember();randomizer_hyrule_warp_execute(actor);
  assert(trace_size==(shards<8?5u:7u));for(unsigned i=0;i<trace_size;i++)assert(trace[i]==i+1);
  if(shards<8){assert(R_HYRULE_EXEC(u8,actor+0x3E8)==7&&R_HYRULE_EXEC(s32,actor+0x3E4)==-77);}
  else{assert(R_HYRULE_EXEC(u8,actor+0x3E8)==1&&R_HYRULE_EXEC(s32,actor+0x3E4)==(warp_result?1:-77));}
  isolated();
 }
 // Locked state must not dereference a captured (but unusable) emitter token.
 reset();shards=7;R_HYRULE_EXEC(u32,actor+0x3CC)=0xDEADBEEF;randomizer_hyrule_warp_execute(actor);assert(trace_size==5);
 // Animation changes are consumed at the original flag/threshold read seam;
 // the emitter remains the one captured before the first native callback.
 for(unsigned present=0;present<2;present++)for(unsigned answer=0;answer<2;answer++){
  reset();shards=8;distance=1500;warp_result=answer;mutate_getter=mutate_animation=mutate_warp=1;animate_threshold=1000.0f;
  if(!present)R_HYRULE_EXEC(u32,actor+0x3CC)=captured_emitter=0;
  remember();randomizer_hyrule_warp_execute(actor);
  assert(R_HYRULE_EXEC(u8,actor+0x3E8)==0&&R_HYRULE_EXEC(s32,actor+0x3E4)==(answer?1:42));
  if(present)assert(R_HYRULE_EXEC(u32,emitter1+0x254)==(animate_status|1u));
  // Mock animation's write to emitter1 is deliberate even when original capture was null.
  if(!present)memcpy(before_emitters+0x254,memory+emitter1-0x10000+0x254,4);
  isolated();
 }
 printf("Hyrule warp execute: %u cases passed\n",checks);assert(checks==2084);return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            c, exe = Path(directory) / 'test.c', Path(directory) / 'test'
            c.write_text(source)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-I', str(header.parent), str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
