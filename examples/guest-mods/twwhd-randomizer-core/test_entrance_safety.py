"""Auction SCLS exit and dungeon map invalidation preserve bounded native state."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class EntranceSafety(unittest.TestCase):
    def test_auction_exit_and_dungeon_pointer_changes(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef uint16_t u16;typedef int32_t s32;typedef float f32;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static unsigned char memory[0x10000] __attribute__((aligned(16)));
#define M8(a,o) memory[(a)+(o)]
#define M16(a,o) (*(uint16_t*)(memory+(a)+(o)))
#define M32(a,o) (*(uint32_t*)(memory+(a)+(o)))
#define R_ENT_U8 M8
#define R_ENT_U16 M16
#define R_ENT_U32 M32
static u32 ui_owner;
#define WWHD_GAME_DATA(a,t) ui_owner
#include "entrance_safety.h"
static unsigned getter_calls,exit_calls,native_calls,exit_result;
static const u32 actor=0x100,play=0x2000,msg=0x1200;
s32 randomizer_auction_exit(s32 exit,f32 speed,u32 mode,s32 room){
 assert(exit==1&&speed==0&&mode==0&&room==0);exit_calls++;M16(actor,0x916)=0x1234;return exit_result;
}
u32 randomizer_entrance_play(void){
 ++getter_calls;assert(getter_calls<=3);M16(actor,0x916)=getter_calls==1?0xBEEF:getter_calls==2?0xCAFE:0xD00D;return play;
}
u16 randomizer_native_auction_next_message(u32 a,u32 message){assert(a==actor&&message==msg&&M32(msg,0)!=0x1D1F);native_calls++;return 31;}
int main(void){
 for(exit_result=0;exit_result<2;exit_result++){
  memset(memory,0x5A,sizeof memory);M32(msg,0)=0x1D1F;getter_calls=0;exit_calls=0;
  assert(randomizer_auction_next_message(actor,msg)==16&&getter_calls==3&&exit_calls==1);
  assert(M8(play,0x514D)==0&&M16(play,0x5CEC)==0xBEEF&&M16(play,0x5BA6)==0xCAFE);
  assert(M16(actor,0x916)==0xD00D&&M8(actor,0x943)==1&&M32(actor,0x904)==0&&M32(msg,0)==0x1D1F);
 }
 for(unsigned m=0;m<65536;m+=257){M32(msg,0)=m;assert(randomizer_auction_next_message(actor,msg)==31);}
 M32(msg,0)=0x1D24;assert(randomizer_auction_next_message(actor,msg)==31&&native_calls==257);
 const u32 menu=0x100,ui=0x2000;ui_owner=0x1800;M32(ui_owner,0x218)=ui;
 for(unsigned variant=0;variant<16;variant++)for(unsigned mode=0;mode<256;mode++)for(unsigned old=0;old<8;old++){
  uint32_t maps[6];for(unsigned i=0;i<6;i++){maps[i]=variant&(1u<<(i%4))?0x800+4*(i%3):0;M32(menu,0xA0+4*i)=maps[i];}
  const uint32_t previous=old<6?maps[old]:old==6?0:0xDEADBEEF;
  M32(menu,0x920)=previous;M32(ui,0x50)=mode;M32(ui,0x64)=0xAABBCCDD;
  unsigned char before[sizeof memory];memcpy(before,memory,sizeof memory);
  randomizer_dungeon_select_map(menu);
  const uint32_t next=mode<6?maps[mode]:0;
  assert(M32(menu,0x920)==next);
  assert(M32(ui,0x64)==((mode<6&&next!=previous)?0:0xAABBCCDD));
  for(unsigned i=0;i<sizeof memory;i++)if((i<menu+0x920||i>=menu+0x924)&&(i<ui+0x64||i>=ui+0x68))assert(memory[i]==before[i]);
 }
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'test.c').write_text(source)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-I', str(Path(__file__).resolve().parent), str(root / 'test.c'),
                            '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
