"""Preserve the fan's native phase wrapping and table lookup without a model."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class LeafFan(unittest.TestCase):
    def test_all_phase_values_without_model(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint16_t u16;
typedef int16_t s16;typedef uint8_t u8;typedef float f32;
#define WWHD_REPLACE(a,r,n,p) r n p
static float table[8192*2];
#define WWHD_GAME_DATA(a,t) (*(t*)table)
#include "leaf_fan.h"
static _Alignas(16) unsigned char actor[0x7000],expected[0x7000];
int main(void){
 for(unsigned i=0;i<8192;i++){
  table[i*2]=((int)(i%33)-16)/16.0f;
  table[i*2+1]=999.0f; /* Never sample the adjacent cosine component. */
 }
 for(unsigned phase=0;phase<65536;phase++){
  memset(actor,0xA5,sizeof(actor));*(u32*)(actor+0x4440)=0;
  *(u16*)(actor+0x69AC)=phase;memcpy(expected,actor,sizeof(actor));
  const unsigned next=(phase+0x82F)&65535,shifted=(next+65536-0xE00)&65535;
  *(u16*)(expected+0x69AC)=next;
  *(s16*)(expected+0x69A8)=(s16)(1536.0f*table[(next/8)*2]);
  *(s16*)(expected+0x69AA)=(s16)(2048.0f*table[(shifted/8)*2]);
  assert(randomizer_leaf_fan_shape((u32)actor)==1);
  assert(!memcmp(actor,expected,sizeof(actor)));
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
