"""Randomized rewards must not determine whether a chart needs deciphering."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class SalvageCharts(unittest.TestCase):
    def test_chart_gate_and_query_scope(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
typedef uint32_t u32;typedef int32_t s32;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static unsigned chart,item,opened,complete,decoded,cleared,nesting,nested;
static unsigned open_calls,triforce_calls,decipher_calls;
#include "salvage_chart_gate.h"
u32 randomizer_native_salvage_open_map(u32 map,s32 index){
 assert(map==123 && index==(s32)chart-1);++open_calls;return opened;
}
u32 randomizer_native_salvage_triforce(u32 value){++triforce_calls;return value>=0x61 && value<=0x68;}
u32 randomizer_native_salvage_deciphered(u32 map,s32 index){
 assert(map==123);++decipher_calls;
 return index>=0 && index<8 ? !!(decoded&(1u<<index)) : 0;
}
/* Native proc_wait's unchanged kind-0 control flow, with other native work
 * represented by its distinctive return. The upstream oracle below uses slot
 * identity directly, independently of this original item-driven branch. */
u32 randomizer_native_salvage_wait(u32 actor){
 assert(actor==77);cleared=0;
 if(randomizer_salvage_open_map(123,(s32)chart-1) && !complete){
  if(nesting && !nested){
   const unsigned old_chart=chart,old_item=item;const s32 saved=randomizer_salvage_chart_index;
   nested=1;chart=2;item=0x61;assert(randomizer_salvage_wait(77)==0x1234);
   chart=old_chart;item=old_item;cleared=0;assert(randomizer_salvage_chart_index==saved);
  }
  if(!randomizer_salvage_triforce(item) ||
     randomizer_salvage_deciphered(123,(s32)(uint8_t)(item-0x60)-1))cleared=1;
 }
 return 0x1234;
}
int main(void){
 for(chart=0;chart<256;chart++)for(item=0;item<256;item++)
 for(unsigned bits=0;bits<8;bits++)for(opened=0;opened<2;opened++)for(complete=0;complete<2;complete++){
  decoded=((bits&1)?1:0)|((bits&2)?4:0)|((bits&4)?16:0);
  open_calls=triforce_calls=decipher_calls=0;
  assert(randomizer_salvage_wait(77)==0x1234);
  const unsigned needs=chart==1 || chart==3 || chart==5;
  const unsigned is_decoded=chart && chart<=8 && (decoded&(1u<<(chart-1)));
  assert(cleared==(opened && !complete && (!needs || is_decoded)));
  assert(open_calls==1 && !triforce_calls && decipher_calls==(opened && !complete && needs));
  assert(!randomizer_salvage_wait_depth);
 }
 chart=5;item=0x61;opened=1;complete=0;decoded=16;nesting=1;
 assert(randomizer_salvage_wait(77)==0x1234 && cleared && !randomizer_salvage_wait_depth);
 /* Helpers outside the exact enclosing function keep stock return/arguments. */
 for(item=0;item<256;item++)assert(randomizer_salvage_triforce(item)==(item>=0x61 && item<=0x68));
 for(s32 index=-1;index<256;index++){
  chart=(unsigned)(index+1);assert(randomizer_salvage_open_map(123,index)==opened);
  assert(randomizer_salvage_deciphered(123,index)==(index>=0 && index<8 ? !!(decoded&(1u<<index)) : 0));
 }
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "test.c").write_text(source)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-I", str(Path(__file__).resolve().parent), str(root / "test.c"),
                            "-o", str(root / "test")], check=True)
            subprocess.run([str(root / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
