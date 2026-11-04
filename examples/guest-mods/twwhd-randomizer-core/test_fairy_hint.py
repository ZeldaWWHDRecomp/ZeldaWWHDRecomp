"""Fairy item hint is independent of bow tier without touching other messages."""
from pathlib import Path
import subprocess,tempfile,unittest
class FairyHint(unittest.TestCase):
 def test_message_and_memory(self):
  source=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t u32;typedef uint8_t u8;
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static u8 save[0x100];static u32 saveptr;static unsigned reads;
static void* data(unsigned a){assert(a==1);reads++;return &saveptr;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
#include "fairy_hint.h"
int main(void){
 uint8_t actor[0x700],expected[0x700],saved[0x100];
 const uint32_t ids[]={0,12013,12014,12015,12017,0xFFFFFFFF,0x80002EEE};
 for(unsigned bow=0;bow<256;bow++)for(unsigned m=0;m<7;m++){
  memset(actor,0xA5,sizeof actor);memset(save,0x5A,sizeof save);save[0x68]=bow;saveptr=(u32)save;reads=0;
  memcpy(actor+0x584,&ids[m],4);memcpy(expected,actor,sizeof actor);memcpy(saved,save,sizeof save);
  uint32_t want=ids[m];if(want==12014){want=12015;memcpy(expected+0x584,&want,4);}
  assert(randomizer_fairy_hint_message((u32)actor)==want);
  assert(reads==(ids[m]==12014));assert(!memcmp(actor,expected,sizeof actor));assert(!memcmp(save,saved,sizeof save));
 }
 return 0;
}
'''
  with tempfile.TemporaryDirectory() as t:
   p=Path(t);(p/'test.c').write_text(source)
   subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
