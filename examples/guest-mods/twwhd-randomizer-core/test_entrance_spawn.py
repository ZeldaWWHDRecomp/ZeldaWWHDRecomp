"""Force crawl only at the audited late player-init resource sequence."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class EntranceSpawn(unittest.TestCase):
    def test_late_spawn_mode_and_scope(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef uint16_t u16;typedef int32_t s32;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_HOOK_RETURN(a,n,p) void n p
static unsigned char memory[0x6000] __attribute__((aligned(16)));
#define M8(a,o) memory[(a)+(o)]
#define M16(a,o) (*(uint16_t*)(memory+(a)+(o)))
#define M32(a,o) (*(uint32_t*)(memory+(a)+(o)))
#define R_SPAWN_U8 M8
#define R_SPAWN_U16 M16
#define R_SPAWN_U32 M32
#define WWHD_ADDR_save_info_pointer 1
static const u32 save_address=0x2000,resource_system=0x456;
#define WWHD_GAME_DATA(a,t) ((a)==1?save_address:resource_system)
#include "entrance_spawn.h"
static unsigned mode,flag,sequence,nest,depth,cases,tail_calls;
static const u32 outer=0x100,inner=0x800,always=0x1000,link=0x1010,wrong=0x1020;
void randomizer_native_player_init(u32 a){
 assert(a==outer||a==inner);assert(randomizer_spawn_actor==a&&randomizer_spawn_resource_phase==0);
 const unsigned before=M8(save_address,0x1177);
 /* Heap/model callbacks and same-name earlier requests must see the old mode. */
 randomizer_spawn_resource_ready(resource_system,link,0x72);assert(M8(save_address,0x1177)==before);
 randomizer_spawn_sound_ready(a+4);randomizer_spawn_resource_ready(resource_system,always,0x71);randomizer_spawn_resource_ready(resource_system,link,0x72);assert(M8(save_address,0x1177)==before);
 if(sequence==0)return;
 randomizer_spawn_sound_ready(a);assert(randomizer_spawn_resource_phase==1);
 randomizer_spawn_resource_ready(resource_system,link,0x72);assert(M8(save_address,0x1177)==before);
 randomizer_spawn_resource_ready(resource_system+4,always,0x71);assert(randomizer_spawn_resource_phase==1);
 if(sequence!=2)randomizer_spawn_resource_ready(resource_system,always,0x71);
 randomizer_spawn_resource_ready(resource_system,wrong,0x72);assert(M8(save_address,0x1177)==before);
 if(nest&&!depth){const u32 active=randomizer_spawn_actor,phase=randomizer_spawn_resource_phase;depth=1;M16(inner,0x2F8)=0;
  randomizer_player_init(inner);assert(randomizer_spawn_actor==active&&randomizer_spawn_resource_phase==phase);depth=0;assert(M8(save_address,0x1177)==before);}
 if(sequence==3)return;
 randomizer_spawn_resource_ready(resource_system,link,0x72);
 const unsigned want=sequence==1&&(M16(a,0x2F8)&1)&&before!=2&&before!=3?2:before;
 assert(M8(save_address,0x1177)==want);
 if(sequence==1){assert(randomizer_spawn_resource_phase==0);M8(save_address,0x1177)=7;randomizer_spawn_resource_ready(resource_system,link,0x72);assert(M8(save_address,0x1177)==7);M8(save_address,0x1177)=want;}
 ++tail_calls;
}
int main(void){
 memset(memory,0,sizeof memory);M32(always,0)=0x1100;memcpy(memory+0x1100,"Always",7);M32(link,0)=0x1120;memcpy(memory+0x1120,"Link",5);M32(wrong,0)=0x1140;memcpy(memory+0x1140,"LinkX",6);
 for(mode=0;mode<256;mode++)for(flag=0;flag<4;flag++)for(sequence=0;sequence<4;sequence++)for(nest=0;nest<2;nest++){
  M8(save_address,0x1177)=mode;M16(outer,0x2F8)=flag==3?0xFFFF:flag;M16(inner,0x2F8)=0;
  unsigned char before[sizeof memory];memcpy(before,memory,sizeof memory);
  randomizer_player_init(outer);
  assert(!randomizer_spawn_actor&&!randomizer_spawn_resource_phase);
  const unsigned want=sequence==1&&(M16(outer,0x2F8)&1)&&mode!=2&&mode!=3?2:mode;
  assert(M8(save_address,0x1177)==want);
  for(unsigned i=0;i<sizeof memory;i++)if(i!=save_address+0x1177)assert(memory[i]==before[i]);
  cases++;
 }
 assert(cases==8192&&tail_calls);
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
