"""The static source flag must not replace any seed item or neighboring metadata."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class AuctionSource(unittest.TestCase):
    def test_only_source_halfword_changes(self):
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uint16_t u16;
static _Alignas(16) unsigned char data[64],expected[64];
#define WWHD_GAME_DATA(a,t) (*(t*)(data+((a)-0x10190168u)))
#include "auction_reward_source.h"
int main(void){
 for(unsigned item=0;item<256;item++)for(unsigned old=0;old<65536;old+=257){
  for(unsigned i=0;i<64;i++)data[i]=(unsigned char)(i*37+item);
  data[0x25]=(unsigned char)item;*(u16*)(data+0x2A)=(u16)old;
  memcpy(expected,data,sizeof(data));*(u16*)(expected+0x2A)=0x6B01;
  randomizer_initialize_auction100_source();assert(!memcmp(data,expected,sizeof(data)));assert(data[0x25]==item);
  randomizer_initialize_auction100_source();assert(!memcmp(data,expected,sizeof(data)));
 }
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'test.c').write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).resolve().parent),str(root/'test.c'),'-o',str(root/'test')],check=True)
            subprocess.run([str(root/'test')],check=True)


if __name__=='__main__':
    unittest.main()
