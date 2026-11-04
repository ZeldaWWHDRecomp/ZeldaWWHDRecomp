"""Packed roundtrips preserve gaps, boundaries, low-health seeds and callback order."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class SaveRestoreCaps(unittest.TestCase):
    def test_serialization_and_state_boundaries(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint16_t u16;typedef uint8_t u8;
#define WWHD_GAME_FUNC(a,r,n,p) static r n p
#define WWHD_GAME_ORIGINAL(a,r,n,p) static r n p
#define WWHD_REPLACE(a,r,n,p) static r n p
#define WWHD_ADDR_save_info_pointer 1
static u32 save_ptr,graphics_ptr;
#define WWHD_GAME_DATA(a,t) ((a)==1?save_ptr:graphics_ptr)
static unsigned calls,original_calls;static u8 *allocation;
static void randomizer_card_audio(u32 mode){
 assert(mode==2);assert(allocation[0x1C5]==2);
 /* Still old tail bytes and unrefilled MP when the native sound callback runs. */
 assert(allocation[0x1CC]==0xA5);assert(allocation[0x34]==7);calls++;
}
static u32 randomizer_gameover_original(u32 a){(void)a;original_calls++;return 91;}
#include "save_restore_caps.h"
static _Alignas(16) u8 save[0x1300],expected[0x1300],card[3*0xA94+32],before[sizeof(card)];
static _Alignas(16) u8 graphics[0x300],actor[0x120];
/* Independent description of the serialized player fields, excluding padding. */
static const unsigned offsets[]={0,24,48,60,81,102,110,118,144,156,180,196,328,420,428,444,556,668,780,896,1472,1572,1828};
static const unsigned sizes[]={24,24,12,21,21,8,8,24,12,24,13,132,92,5,16,112,112,112,112,576,100,256,80};
int main(void){
 save_ptr=(u32)save;graphics_ptr=(u32)graphics;allocation=save;
 for(unsigned max=0;max<25;max++)for(unsigned hp=0;hp<25;hp++)for(unsigned slot=0;slot<3;slot++){
  for(unsigned i=0;i<sizeof(save);i++)save[i]=(u8)(i*37+max);
  *(u16*)(save+0x20)=max;*(u16*)(save+0x22)=hp;save[0x33]=32;save[0x34]=7;
  memcpy(expected,save,sizeof(save));memset(card,0xCC,sizeof(card));memcpy(before,card,sizeof(card));
  assert(randomizer_memory_to_card(0,(u32)card,slot)==0);
  assert(!memcmp(save,expected,sizeof(save)));
  unsigned p=slot*0xA94;
  for(unsigned b=0;b<23;b++){
   memcpy(before+p,save+0x20+offsets[b],sizes[b]);p+=sizes[b];
  }
  if(hp<12)*(u16*)(before+slot*0xA94+2)=max/4*4<12?max/4*4:12;
  assert(p-slot*0xA94==0x768); /* packed payload excludes checksums/reserved slot tail */
  assert(!memcmp(card,before,sizeof(card)));
  memset(save,0xA5,sizeof(save));memcpy(expected,save,sizeof(save));p=slot*0xA94;
  for(unsigned b=0;b<23;b++){memcpy(expected+0x20+offsets[b],card+p,sizes[b]);p+=sizes[b];}
  expected[0x1C5]=2;expected[0x34]=expected[0x33];
  assert(randomizer_card_to_memory((u32)save+0x20,(u32)card,slot)==0);
  assert(!memcmp(save,expected,sizeof(save)));assert(!memcmp(card,before,sizeof(card)));
 }
 assert(calls==25*25*3);
 for(unsigned max=0;max<25;max++)for(unsigned state=0;state<8;state++){
  memset(actor,0xA5,sizeof(actor));memset(graphics,0xA5,sizeof(graphics));
  *(u16*)(save+0x20)=max;*(u16*)(save+0x22)=99;actor[0x110]=state;
  unsigned old=original_calls;
  assert(randomizer_gameover_execute((u32)actor)==(state==3?1:91));
  if(state==3){assert(actor[0x110]==4);assert(graphics[0x248]==1);assert(*(u16*)(save+0x22)==(max/4*4<12?max/4*4:12));assert(original_calls==old);}
  else{assert(original_calls==old+1);assert(actor[0x110]==state);assert(graphics[0x248]==0xA5);assert(*(u16*)(save+0x22)==99);}
 }
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'test.c').write_text(source)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-I', str(Path(__file__).resolve().parent), str(root/'test.c'), '-o', str(root/'test')], check=True)
            subprocess.run([str(root/'test')], check=True)


if __name__ == '__main__':
    unittest.main()
