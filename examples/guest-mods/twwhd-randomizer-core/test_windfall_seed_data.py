import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from seed_windfall_data import (
    ARYLL_ANIMATIONS, ARYLL_ANIMATION_SIZE, ARYLL_ANIMATION_COUNT,
    ARYLL_DOOR_ANIMATION_OFFSET, validate_windfall_seed_data,
)


class WindfallSeedData(unittest.TestCase):
    def test_complete_animation_table(self):
        size = ARYLL_ANIMATION_SIZE * ARYLL_ANIMATION_COUNT
        original = bytes((index * 37 + 19) & 255 for index in range(size))
        expected = bytearray(original)
        expected[ARYLL_DOOR_ANIMATION_OFFSET] = 5

        def reader(value):
            def read(address, length):
                self.assertEqual((address, length), (ARYLL_ANIMATIONS, size))
                return value
            return read

        validate_windfall_seed_data(reader(original), reader(expected))
        for index in range(size):
            for bit in range(8):
                changed = bytearray(expected)
                changed[index] ^= 1 << bit
                with self.assertRaisesRegex(ValueError, 'unsupported Aryll'):
                    validate_windfall_seed_data(reader(original), reader(changed))
        for length in range(size):
            with self.assertRaisesRegex(ValueError, 'short Aryll'):
                validate_windfall_seed_data(reader(original[:length]), reader(expected))
            with self.assertRaisesRegex(ValueError, 'short Aryll'):
                validate_windfall_seed_data(reader(original), reader(expected[:length]))

    def test_startup_changes_only_bck_byte(self):
        code = r'''
#include <assert.h>
#include <stdint.h>
typedef uint8_t u8;
static u8 table[14 * 16 + 2];
static u8* data(uint32_t address) {
    assert(address == 0x101BFFC4);
    return table + 1 + 4 * 16;
}
#define WWHD_GAME_DATA(address,type) (*(type*)data(address))
#define WWHD_HOOK(address,name,args) void name args
#include "windfall_seed_data.h"
int main(void) {
    for (unsigned value = 0; value < 256; ++value) {
        for (unsigned i = 0; i < sizeof(table); ++i) table[i] = (u8)(i + value);
        randomizer_initialize_windfall_seed_data();
        randomizer_initialize_windfall_seed_data();
        for (unsigned i = 0; i < sizeof(table); ++i)
            assert(table[i] == (i == 65 ? 5 : (u8)(i + value)));
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'test.c'
            executable = Path(directory) / 'test'
            source.write_text(code)
            subprocess.run([
                os.environ.get('CC', 'clang'), '-std=c11', '-O2', '-Wall',
                '-Wextra', '-Werror', '-I', str(Path(__file__).parent),
                str(source), '-o', str(executable),
            ], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    unittest.main()
