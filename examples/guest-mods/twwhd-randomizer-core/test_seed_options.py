"""Honor the optional full chart without clearing native or unrelated flags."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from seed_options import REVEAL_CHART_SITE, extract_startup_options


class SeedOptions(unittest.TestCase):
    def read_word(self, word):
        def read(address, size):
            self.assertEqual((address, size), (REVEAL_CHART_SITE, 4))
            return word.to_bytes(4, "big")
        return extract_startup_options(read)

    def test_known_option_encodings(self):
        move = (31 << 26) | (12 << 21) | (10 << 16) | (12 << 11) | (444 << 1)
        immediate = (14 << 26) | (10 << 21) | 1
        self.assertEqual(self.read_word(move), {"reveal_full_sea_chart": 0})
        self.assertEqual(self.read_word(immediate), {"reveal_full_sea_chart": 1})
        for bad in (move | 1, move ^ (1 << 16), immediate ^ (1 << 21), immediate + 1, 0):
            with self.subTest(word=bad), self.assertRaises(ValueError):
                self.read_word(bad)
        with self.assertRaises(ValueError):
            extract_startup_options(lambda address, size: b"")

    def test_map_cells_and_unrelated_bits(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;
static unsigned calls;
static unsigned char save[0x200],before[0x200];
void wwhd_dSv_player_map_c_onFmapBit(u32 map,u32 index,u32 bit){
 assert(map==(u32)save+0xE4 && index<49 && bit==0);++calls;
 ((unsigned char*)map)[0x40+index]|=1;
}
#include "chart_reveal.h"
int main(void){
 for(unsigned old=0;old<256;old++)for(unsigned enabled=0;enabled<2;enabled++){
  memset(save,old,sizeof(save));memcpy(before,save,sizeof(save));calls=0;
  randomizer_reveal_starting_chart((u32)save,enabled);
  assert(calls==(enabled?49:0));
  for(unsigned i=0;i<sizeof(save);i++)
   assert(save[i]==(enabled && i>=0x124 && i<0x155 ? (before[i]|1) : before[i]));
 }
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "test.c").write_text(source)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(Path(__file__).resolve().parent), str(root / "test.c"),
                            "-o", str(root / "test")], check=True)
            subprocess.run([str(root / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
