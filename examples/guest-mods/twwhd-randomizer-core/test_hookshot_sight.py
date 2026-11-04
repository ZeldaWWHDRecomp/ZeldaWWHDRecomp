from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class HookshotSight(unittest.TestCase):
    def test_collision_order_precision_and_null_actor(self):
        code = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <math.h>
typedef uintptr_t u32;typedef double f64;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#include "hookshot_sight.h"
static unsigned int actor[0xD540/4],collision[0x80/4],target[0x300/4];
static double first,second;static unsigned calls,mutate;
f64 randomizer_hookshot_square_distance(u32 a,u32 b) {
 assert(b==(u32)actor+0x314);
 if(calls++==0){assert(a==(u32)actor+0xD528);return first;}
 assert(calls==2&&a==(u32)collision+0x70);
 if(mutate){collision[0x70/4]=0x7FC01234;collision[0x74/4]=0x80000000;collision[0x78/4]=0xFFFFFFFF;target[0x2E0/4]^=0xFFFFFFFF;}
 return second;
}
int main(void) {
 const double values[][2]={{2,1},{1,2},{1,1},{NAN,1},{1,NAN},{INFINITY,1},{1,INFINITY},{1.0000000001,1.0}};
 unsigned cases=0;
 for(unsigned v=0;v<8;v++)for(unsigned bit=0;bit<33;bit++)for(unsigned null=0;null<2;null++)for(mutate=0;mutate<2;mutate++) {
  memset(actor,0xA5,sizeof actor);memset(collision,0x5A,sizeof collision);memset(target,0,sizeof target);
  target[0x2E0/4]=bit==32?0:1u<<bit;first=values[v][0];second=values[v][1];calls=0;
  randomizer_hookshot_sight((u32)actor,(u32)collision,null?0:(u32)target,0x1234);
  assert(calls==2);
  if(first>second){assert(actor[0xD528/4]==collision[0x70/4]);assert(actor[0xD52C/4]==collision[0x74/4]);assert(actor[0xD530/4]==collision[0x78/4]);assert(actor[0xD50C/4]==(!null&&!!(target[0x2E0/4]&0x10280000)));}
  else {assert(actor[0xD528/4]==0xA5A5A5A5&&actor[0xD52C/4]==0xA5A5A5A5&&actor[0xD530/4]==0xA5A5A5A5&&actor[0xD50C/4]==0xA5A5A5A5);}
  for(unsigned i=0;i<sizeof(actor)/4;i++)if(i!=0xD528/4&&i!=0xD52C/4&&i!=0xD530/4&&i!=0xD50C/4)assert(actor[i]==0xA5A5A5A5);
  ++cases;
 }
 assert(cases==1056);return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'test.c'
            executable = Path(directory) / 'test'
            source.write_text(code)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(Path(__file__).parent), str(source), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
