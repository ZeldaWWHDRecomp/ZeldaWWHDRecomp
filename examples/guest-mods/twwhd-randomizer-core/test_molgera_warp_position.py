"""Local Molgera target safety helper; full camera consumer integration pending."""
import os
from pathlib import Path
import subprocess,tempfile,unittest
class MolgeraWarpPosition(unittest.TestCase):
 def test_radius_and_callback_order(self):
  source=r'''
#include <assert.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef uintptr_t u32;typedef float f32;typedef double f64;
static f32 pos[3];static unsigned calls,mutate,override;static f64 supplied;static f32 expected_square;
static f64 native_sqrt(f64 value){assert(value==(f64)expected_square||isnan(value)&&isnan(expected_square));calls++;if(mutate){pos[0]=1234;pos[1]=-200;pos[2]=5678;}return override?supplied:sqrt(value);}
#define RANDOMIZER_MOLGERA_SQRT(v) native_sqrt(v)
#include "molgera_warp_position.h"
int main(void){unsigned cases=0;for(int x=-1000;x<=1000;x+=5)for(int z=-1000;z<=1000;z+=5)for(mutate=0;mutate<2;mutate++){
 pos[0]=x;pos[1]=12.5f;pos[2]=z;calls=0;override=0;const f32 xx=(f32)x*x,zz=(f32)z*z;expected_square=xx+zz;randomizer_molgera_safe_target((u32)pos);unsigned inside=x*x+z*z<=450*450;assert(calls==1&&pos[1]==(mutate?-200:12.5f));assert(pos[0]==(inside?0:mutate?1234:x)&&pos[2]==(inside?777:mutate?5678:z));cases++;}
const f64 edges[]={449.99999999999994,450,450.00000000000006,INFINITY,-INFINITY,NAN};
for(unsigned i=0;i<6;i++)for(mutate=0;mutate<2;mutate++){
 pos[0]=100;pos[1]=-0.0f;pos[2]=300;expected_square=100000;calls=0;override=1;supplied=edges[i];randomizer_molgera_safe_target((u32)pos);unsigned inside=!(supplied>450);assert(calls==1&&pos[0]==(inside?0:mutate?1234:100)&&pos[2]==(inside?777:mutate?5678:300));if(!mutate){uint32_t y;memcpy(&y,&pos[1],4);assert(y==0x80000000);}cases++;}
const f32 special[]={NAN,INFINITY,-INFINITY,0x1p100f};override=0;mutate=0;
for(unsigned i=0;i<4;i++){pos[0]=special[i];pos[1]=1;pos[2]=0;expected_square=special[i]*special[i];calls=0;randomizer_molgera_safe_target((u32)pos);assert(calls==1&&pos[1]==1);if(i==0)assert(pos[0]==0&&pos[2]==777);else assert(pos[2]==0);cases++;}
printf("Molgera safe target helper: %u cases passed\n",cases);}
'''
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory);(root/'test.c').write_text(source)
   subprocess.run([os.environ.get("CC", "cc"),'-O2','-Wall','-Wextra','-Werror','-Wno-logical-op-parentheses','-I',str(Path(__file__).parent),str(root/'test.c'),'-lm','-o',str(root/'test')],check=True)
   subprocess.run([str(root/'test')],check=True)
if __name__=='__main__':unittest.main()
