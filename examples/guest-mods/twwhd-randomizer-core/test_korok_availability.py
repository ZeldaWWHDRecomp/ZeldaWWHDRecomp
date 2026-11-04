"""Six pearl-gate scopes must leave inventory and other queries unchanged."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class KorokAvailability(unittest.TestCase):
    def test_scopes_and_native_branch_inputs(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
typedef uintptr_t u32;typedef int32_t s32;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static u32 save_pointer=0x10000;
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_GAME_DATA(a,t) (*(t*)&save_pointer)
static unsigned symbols,events,nesting,nested,tree_mode,early,mail_depth,mail_early;
#include "korok_availability.h"
s32 randomizer_native_korok_symbol(u32 collect,u32 index){
 assert((collect==save_pointer+0xD4 || collect==0x9000) && index<3);
 return !!(symbols&(1u<<index));
}
static unsigned first_query(u32 actor){
 assert(actor==77);
 assert(randomizer_korok_symbol(save_pointer+0xD4,0)==!!(symbols&1));
 assert(randomizer_korok_symbol(0x9000,2)==!!(symbols&4));
 if(nesting && !nested){
  nested=1;assert(randomizer_korok_bjx1_init(77)==1);
  assert(randomizer_korok_first_symbol==1);
 }
 const unsigned result=randomizer_korok_symbol(save_pointer+0xD4,2);
 /* Native callbacks after the targeted predicate must see real ownership. */
 assert(randomizer_korok_symbol(save_pointer+0xD4,2)==!!(symbols&4));
 return result;
}
u32 randomizer_native_korok_bj4_init(u32 actor){return !first_query(actor) && (events&1) ? 1 : 0;}
u32 randomizer_native_korok_bj6_init(u32 actor){return !first_query(actor) && (events&1) ? 1 : 0;}
u32 randomizer_native_korok_bjx0_init(u32 actor){return !first_query(actor) && (events&1) ? 1 : 0;}
u32 randomizer_native_korok_bjx1_init(u32 actor){return first_query(actor)?1:0;}
u32 randomizer_native_korok_bj7_message(u32 actor){
 if(early)return 0x1421;
 if(first_query(actor)){
  if(!(events&2))return 0x1433;
  if(!(events&4))return 0x1431;
 }else if(!(events&2))return 0x1416;
 return events&8?0x141E:0x141F;
}
void randomizer_native_tree_first_state(u32 actor){
 if(!first_query(actor))tree_mode=0;
 else if(events&1)tree_mode=3;
 else tree_mode=events&2?2:1;
}
s32 randomizer_native_mailbox_execute(u32 actor){
 assert(actor==77);
 assert(randomizer_korok_symbol(0x9000,1)==!!(symbols&2));
 assert(randomizer_korok_symbol(save_pointer+0xD4,0)==!!(symbols&1));
 if(nesting && !mail_depth){
  mail_depth=1;assert(randomizer_mailbox_execute(77)==(s32)events);mail_depth=0;
  assert(randomizer_mailbox_first_symbol==1);
 }
 if(mail_early)return events;
 assert(randomizer_korok_symbol(save_pointer+0xD4,1)==1);
 assert(randomizer_korok_symbol(save_pointer+0xD4,1)==!!(symbols&2));
 return events;
}
int main(void){
 for(symbols=0;symbols<256;symbols++)for(events=0;events<16;events++)
 for(nesting=0;nesting<2;nesting++){
  const unsigned before=symbols;
  for(mail_early=0;mail_early<2;mail_early++){
   assert(randomizer_mailbox_execute(77)==(s32)events && !randomizer_mailbox_first_symbol);
   assert(!randomizer_korok_first_symbol);
  }
  mail_early=0;
  /* A nested mailbox consumes only its Din scope; outer Farore stays armed. */
  randomizer_korok_first_symbol=1;
  assert(randomizer_mailbox_execute(77)==(s32)events);
  assert(randomizer_korok_first_symbol==1 && !randomizer_mailbox_first_symbol);
  randomizer_korok_first_symbol=0;
  nested=0;assert(randomizer_korok_bj4_init(77)==0 && !randomizer_korok_first_symbol);
  nested=0;assert(randomizer_korok_bj6_init(77)==0 && !randomizer_korok_first_symbol);
  nested=0;assert(randomizer_korok_bjx0_init(77)==0 && !randomizer_korok_first_symbol);
  nested=0;assert(randomizer_korok_bjx1_init(77)==1 && !randomizer_korok_first_symbol);
  for(early=0;early<2;early++){
   nested=0;const unsigned expected=early?0x1421:!(events&2)?0x1433:!(events&4)?0x1431:events&8?0x141E:0x141F;
   assert(randomizer_korok_bj7_message(77)==expected && !randomizer_korok_first_symbol);
  }
  nested=0;randomizer_tree_first_state(77);
  assert(tree_mode==(events&1?3:events&2?2:1) && !randomizer_korok_first_symbol);
  for(unsigned index=0;index<3;index++)
   assert(randomizer_korok_symbol(save_pointer+0xD4,index)==!!(symbols&(1u<<index)));
  assert(symbols==before);
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
