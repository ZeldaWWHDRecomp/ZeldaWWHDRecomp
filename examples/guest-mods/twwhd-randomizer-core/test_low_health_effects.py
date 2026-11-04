from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class LowHealthEffects(unittest.TestCase):
    def test_consumers(self):
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint16_t u16;typedef uint8_t u8;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(8) u8 actor[0x6B00],audio[0x300],play[0x5300],save[0x1400];
static unsigned init_quarter_hearts;static u32 saveptr=(u32)save;static f32 one=1,minus=-1;static unsigned guard,disabled,options,sounds,id,mutate;
#define WWHD_ADDR_save_info_pointer 1
static void* data(unsigned a){if(a==1)return &saveptr;if(a==0x10003894)return &one;assert(a==0x100038D0);return &minus;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
#define RLH_GUARD(a) (assert((a)==(u32)actor),guard)
#include "low_health_effects.h"
u32 rlh_game(void){return (u32)play;}
u32 rlh_options(u32 p){assert(p==(u32)save+0x12C0);++options;return 123;}
s32 rlh_alarm_disabled(u32 p){assert(p==123);return disabled;}
u32 rlh_sound(u32 a,u32 n,u32 p,u32 c,s32 f,f32 v,f32 pitch,f32 pan,f32 fx,u32 extra){assert(a==(u32)audio&&!p&&!c&&!f&&v==1&&pitch==1&&pan==-1&&fx==-1&&!extra);++sounds;id=n;if(mutate)audio[0x50]=17;return 0;}
#define A(t,o) (*(t*)(actor+(o)))
#define S(t,o) (*(t*)(save+(o)))
int main(void){
 unsigned cases=0;
 for(init_quarter_hearts=4;init_quarter_hearts<=12;init_quarter_hearts+=8)
 for(unsigned max=4;max<17;max++)for(unsigned hp=0;hp<18;hp++)
 for(guard=0;guard<2;guard++)for(unsigned upper=0;upper<2;upper++)for(unsigned lock=0;lock<2;lock++)for(unsigned demo=0;demo<3;demo++)for(unsigned ship=0;ship<2;ship++)for(unsigned mode=0;mode<2;mode++){
  memset(actor,0,sizeof actor);S(u16,0x20)=max;S(u16,0x22)=hp;A(u16,0x5888)=upper?1:0xFFFF;A(u32,0x65B0)=lock;A(u16,0x420)=demo;A(u32,0x6A70)=ship?0x2000:0;A(s32,0x430)=mode?18:0;
  unsigned eligible=!guard&&!upper&&!lock&&((!demo&&!ship&&hp<=6)||mode);
  int difference=(int)(max&~3u)-(int)hp;
  assert(randomizer_rest_health_animation((u32)actor)==(s32)(eligible&&(init_quarter_hearts>=8||difference>=2)));++cases;
 }
 for(init_quarter_hearts=4;init_quarter_hearts<=12;init_quarter_hearts+=8)
 for(unsigned max=4;max<17;max++)for(int hp=-1;hp<9;hp++)for(disabled=0;disabled<2;disabled++)for(unsigned event=0;event<2;event++)for(unsigned mute=0;mute<2;mute++)for(unsigned count=0;count<3;count++)for(mutate=0;mutate<2;mutate++){
  memset(audio,0,sizeof audio);play[0x5292]=event;audio[0x50]=count;audio[0x276]=mute;*(s32*)(audio+0x44)=hp;S(u16,0x20)=max;options=sounds=id=0;
  randomizer_heart_alarm((u32)audio);
  unsigned entered=!event&&count;unsigned queried=entered&&hp&&!mute;
  unsigned expected=0;if(queried&&!disabled){if(hp<=2)expected=0xD2;else if(hp<=4){if(init_quarter_hearts>=8||max>=8)expected=0xD1;}else if(hp<=6)expected=0xD0;}
  assert(options==queried&&sounds==!!expected&&id==expected);
  assert(audio[0x50]==(entered?(expected&&mutate?16:count-1):count));++cases;
 }
 assert(cases==57408);return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'test.c'
            executable = Path(directory) / 'test'
            source.write_text(code)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(Path(__file__).parent), str(source), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
