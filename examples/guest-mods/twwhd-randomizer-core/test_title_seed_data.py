import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from title_seed_data import TITLE_FLOATS, validate_title_seed_data


class TitleSeedData(unittest.TestCase):
    def test_exact_values_and_unknown_variants(self):
        fields = {a: struct.pack('>f', v) for a, v in TITLE_FLOATS}
        validate_title_seed_data(lambda a, n: fields[a])
        for address in fields:
            for bit in range(32):
                altered = dict(fields)
                altered[address] = (int.from_bytes(fields[address], 'big') ^ (1 << bit)).to_bytes(4, 'big')
                with self.assertRaisesRegex(ValueError, 'unsupported'):
                    validate_title_seed_data(lambda a, n: altered[a])
            for invalid in (None, b'', bytes(1), bytes(3), bytes(5)):
                altered = dict(fields); altered[address] = invalid
                with self.assertRaisesRegex(ValueError, 'truncated'):
                    validate_title_seed_data(lambda a, n: altered[a])

    def test_only_reviewed_float_cells_change(self):
        code = r'''
#include <assert.h>
#include <stdint.h>
typedef float f32;
static float cells[9];
static unsigned writes;
static float *data(uint32_t address) {
 writes++;
 switch(address) {
 case 0x101F7048:return &cells[1];
 case 0x101F7044:return &cells[4];
 case 0x10108280:return &cells[7];
 default:assert(0);return 0;
 }
}
#define WWHD_GAME_DATA(a,t) (*data(a))
#define WWHD_HOOK(a,n,p) void n p
#include "title_seed_data.h"
int main(void) {
 for(unsigned n=0;n<256;n++) {
  for(unsigned j=0;j<9;j++)cells[j]=(float)(n+j);
  writes=0;randomizer_initialize_title_data();assert(writes==3);
  assert(cells[1]==1.4f && cells[4]==2.25f && cells[7]==-38.0f);
  for(unsigned j=0;j<9;j++)if(j!=1&&j!=4&&j!=7)assert(cells[j]==(float)(n+j));
  randomizer_initialize_title_data();assert(writes==6);
  assert(cells[1]==1.4f && cells[4]==2.25f && cells[7]==-38.0f);
 }
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'test.c';exe=Path(directory)/'test'
            source.write_text(code)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(source),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
