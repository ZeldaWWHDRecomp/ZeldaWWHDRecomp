"""Creation-only predicate changes must not change inventory or ambient queries."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class NpcAvailability(unittest.TestCase):
    def test_scoped_progression_queries_and_nested_creation(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
typedef uintptr_t u32;typedef uint8_t u8;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static u32 save_pointer=0x10000;
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_GAME_DATA(a,t) (*(t*)&save_pointer)
static unsigned owned,boss,nest,observed,query_calls;
static u8 info[0x6000];
u32 wwhd_dComIfG_getGameInfo(void){return (u32)info;}
int wwhd_save_event_test(u32 event){(void)event;return 0;}
int wwhd_dSv_player_get_bag_item_c_isBeast(u32 self,u32 item){(void)self;(void)item;return 0;}
#include "quest_reward_flags.h"
#include "npc_availability.h"
u32 randomizer_native_maggie_message(u32 actor){return actor;}
u32 randomizer_native_lenzo_message(u32 actor){return actor;}
u8 randomizer_native_quest_item_owned(u32 item){assert(item<256);++query_calls;return owned;}
u32 randomizer_native_fish_boss(u32 stage){assert(stage<32);return boss;}
u32 randomizer_native_fish_create(u32 actor){
 if(nest&&actor){nest=0;assert(randomizer_fish_create(actor-1)==0x2345+actor-1);nest=1;++observed;}
 assert(randomizer_fish_boss(4)==boss);
 assert(randomizer_quest_item_owned(0x3E)==owned);
 assert(randomizer_fish_boss(3)==1);
 assert(randomizer_fish_boss(3)==boss);
 return 0x2345+actor;
}
int main(void){
 for(owned=0;owned<4;owned++)for(boss=0;boss<4;boss++)
 for(nest=0;nest<2;nest++)for(u32 actor=0;actor<8;actor++){
  assert(randomizer_fish_create(actor)==0x2345+actor);
  assert(!randomizer_spawn_query.fish);
  assert(randomizer_fish_boss(3)==boss);
  assert(randomizer_quest_item_owned(0x3E)==owned);
 }
 assert(observed && query_calls);
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
