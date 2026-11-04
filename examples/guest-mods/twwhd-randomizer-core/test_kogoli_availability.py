from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class KogoliAvailability(unittest.TestCase):
    def test_query_order_action_result_and_nested_initialization(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
typedef uintptr_t u32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static u32 save_pointer=0x10000;
#define WWHD_GAME_DATA(a,t) ((t)save_pointer)
#include "kogoli_availability.h"
static u32 event_value, result, calls, nested, depth;
u32 randomizer_kogoli_event(u32 events,u32 flag) {
 assert(events==save_pointer+0x644 && flag==0x1620);
 ++calls;
 if(nested && !depth){
  depth=1;assert(randomizer_kogoli_init(0x4321)==result);depth=0;
 }
 return event_value;
}
u32 randomizer_kogoli_init_action(u32 actor) {
 assert(calls>0 && actor==(depth?0x4321u:0x1234u));
 ++calls; return result;
}
int main(void) {
 for(event_value=0;event_value<256;event_value++)
 for(result=0;result<256;result++)for(nested=0;nested<2;nested++) {
  calls=depth=0;
  assert(randomizer_kogoli_init(0x1234)==result);
  assert(calls==(nested?4u:2u));
 }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'test.c').write_text(source)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2',
                            '-Wall', '-Wextra', '-Werror', '-I', str(Path(__file__).parent),
                            str(root/'test.c'), '-o', str(root/'test')], check=True)
            subprocess.run([str(root/'test')], check=True)


if __name__ == '__main__':
    unittest.main()
