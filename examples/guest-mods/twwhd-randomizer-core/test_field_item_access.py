"""Verify spoil/bait preservation and unchanged native conversion for other IDs."""
from pathlib import Path
import os, subprocess, tempfile, unittest
class FieldItemAccess(unittest.TestCase):
 def test_native_fallback(self):
  source=r'''
#include <stdint.h>
#include <assert.h>
typedef uint8_t u8;typedef int32_t s32;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static unsigned calls;static s32 argument;
static int wwhd_isEmono(u8 id){return id>=0x45 && id<=0x4C;}
static int wwhd_isEsa(u8 id){return id==0x82 || id==0x83 || id==0x84;}
#include "field_item_access.h"
u8 randomizer_native_check_itemno(s32 item){++calls;argument=item;return (u8)(item^0xA5);}
int main(void){
 for(s32 item=-256;item<512;item++){
  calls=0;const u8 id=(u8)item;const int preserved=wwhd_isEmono(id)||wwhd_isEsa(id);
  const u8 result=randomizer_check_itemno(item);
  assert(result==(preserved?id:(u8)(item^0xA5)) && calls==(preserved?0:1));
  if(!preserved)assert(argument==item);
 }
 return 0;
}
'''
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);(p/'test.c').write_text(source)
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-I',str(Path(__file__).resolve().parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
