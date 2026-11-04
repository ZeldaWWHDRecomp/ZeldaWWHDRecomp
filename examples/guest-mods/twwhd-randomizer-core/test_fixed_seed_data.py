import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from fixed_seed_data import STONE_HEAD_STATUS, validate_fixed_seed_data


class FixedSeedData(unittest.TestCase):
    def test_profile_validation(self):
        for original in (0, 0x80, 0xFFFFFFFF, *(1 << n for n in range(32))):
            expected = original & ~0x80
            def reader(value):
                def read(address, size):
                    if address == 0x100F7BD0:
                        self.assertEqual(size, 4)
                        return bytes(4)
                    self.assertEqual((address, size), (STONE_HEAD_STATUS, 4))
                    return value.to_bytes(4, 'big')
                return read
            validate_fixed_seed_data(reader(original), reader(expected))
            for bit in range(32):
                with self.assertRaises(ValueError):
                    validate_fixed_seed_data(reader(original), reader(expected ^ (1 << bit)))
        for short in (b'', b'\0', b'\0\0\0'):
            with self.assertRaises(ValueError):
                validate_fixed_seed_data(lambda a, n: short, lambda a, n: b'\0' * 4)
            with self.assertRaises(ValueError):
                validate_fixed_seed_data(lambda a, n: b'\0' * 4, lambda a, n: short)

    def test_ff_warp_quadrant_validation(self):
        def original(address, size):
            self.assertEqual((address, size), (STONE_HEAD_STATUS, 4))
            return bytes(4)
        for value in (bytes(4), *(int(1 << bit).to_bytes(4, 'big') for bit in range(32)), b'', b'\0', b'\0'*3, b'\0'*5):
            def randomized(address, size):
                self.assertEqual(size, 4)
                return bytes(4) if address == STONE_HEAD_STATUS else value
            if value == bytes(4):
                validate_fixed_seed_data(original, randomized)
            else:
                with self.assertRaisesRegex(ValueError, 'Fortress warp quadrant'):
                    validate_fixed_seed_data(original, randomized)

    def test_startup_preserves_other_status_bits(self):
        code = r'''
#include <stdint.h>
#include <assert.h>
typedef uint32_t u32;
static u32 status;
static u32* data(u32 address) { assert(address==0x101CA100); return &status; }
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
#define WWHD_HOOK(a,n,p) void n p
#include "fixed_seed_data.h"
int main(void) {
 for (uint32_t n=0;n<65536;n++) {
  const uint32_t before=(n<<16)|(65535-n);
  status=before;randomizer_initialize_fixed_seed_data();
  assert(status==(before&~0x80u));
  randomizer_initialize_fixed_seed_data();assert(status==(before&~0x80u));
 }
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'test.c'
            executable = Path(directory) / 'test'
            source.write_text(code)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(Path(__file__).parent), str(source), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
