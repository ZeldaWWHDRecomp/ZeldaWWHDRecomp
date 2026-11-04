"""Deku questions are reachable for every original dialogue type."""
from pathlib import Path
import subprocess,tempfile,unittest
class DekuQuestions(unittest.TestCase):
 def test_dialogue(self):
  code=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t u32;typedef uint8_t u8;
#define WWHD_REPLACE(a,r,n,p) r n p
#include "deku_chart_dialogue.h"
int main(void){u8 actor[0xA00],expected[0xA00];for(unsigned type=0;type<256;type++)for(unsigned seen=0;seen<256;seen++){
 memset(actor,0xA5,sizeof actor);actor[0x935]=type;actor[0x8BF]=seen;memcpy(expected,actor,sizeof actor);if(!seen)expected[0x8BF]=1;
 assert(randomizer_deku_chart_dialogue((u32)actor)==(seen?0x13AC:0x13A8));assert(!memcmp(actor,expected,sizeof actor));assert(randomizer_deku_chart_dialogue((u32)actor)==0x13AC);
 }return 0;}
'''
  with tempfile.TemporaryDirectory() as t:
   p=Path(t);(p/'test.c').write_text(code)
   subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
