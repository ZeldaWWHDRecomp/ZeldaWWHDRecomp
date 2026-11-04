import os
from pathlib import Path
import subprocess,tempfile,unittest
class TowerDoorGlow(unittest.TestCase):
 def test_real_queries_and_callback_reload(self):
  source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef int8_t s8;typedef int32_t s32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static _Alignas(uintptr_t) u8 glow[32],door[0x400],saved[2][0x1300];static u32 saveptr;
#define WWHD_GAME_DATA(a,t) saveptr
#include "tower_door_glow.h"
static unsigned mode,model,song,bits,enable,with_door,calls,expected_index,queries;
s32 randomizer_tower_door_event_query(u32 self,u32 flag){assert(self==saveptr+(mode==1?0x1178u:0x644u));queries++;saveptr=(u32)saved[1];glow[0x11]=99;unsigned bit=flag==0x0108?0:flag==0x0110?1:flag==0x1710?2:flag==0x1704?3:4;return(bits>>bit)&1;}
s32 randomizer_tower_door_song_query(u32 self,u32 value){assert(self==(u32)saved[0]+0xD4&&value==2);queries++;saveptr=(u32)saved[1];return song;}
void randomizer_tower_door_animation(u32 self,u32 idx){assert(self==(u32)glow&&idx==expected_index&&calls++==0);glow[0x10]=enable;*(uint32_t*)(glow+12)=0x12345678;}
s32 randomizer_tower_door_play(u32 animation){assert(animation==0x12345678&&calls++==1);door[0x326]=(u8)-9;return 0;}
s32 randomizer_tower_door_reverb(s32 room){assert(room==-9&&calls++==2);return -7;}
void randomizer_tower_door_sound(u32 sound,u32 position,u32 flags,s32 reverb){assert(sound==0x61D4&&position==(u32)door+0x37C&&!flags&&reverb==-7&&calls++==3);}
int main(void){for(mode=0;mode<256;mode++)for(model=0;model<2;model++)for(song=0;song<2;song++)for(bits=0;bits<32;bits++)for(enable=0;enable<2;enable++)for(with_door=0;with_door<2;with_door++){
 memset(glow,0,sizeof(glow));saveptr=(u32)saved[0];glow[0x11]=mode;*(uint32_t*)(glow+8)=model;calls=queries=0;expected_index=0;
 if(mode==1)expected_index=bits&1?1:bits&2?2:0;else if(mode==4)expected_index=bits&4?0:3;else if(mode==3)expected_index=bits&8?0:3;else if(mode==2)expected_index=bits&16?0:3;
 randomizer_tower_door_proc((u32)glow,with_door?(u32)door:0);
 if(!mode||!model){assert(!calls&&!queries);continue;}
 assert(calls==(enable?(with_door?4u:2u):1u));unsigned expected_queries=mode==1?(bits&1?1:2):mode==4?1:mode==2||mode==3?2:0;assert(queries==expected_queries);
 }}
'''
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory);(root/'tower_door_glow.h').write_text(Path(__file__).with_name('tower_door_glow.h').read_text().replace('volatile u32*','volatile uint32_t*'));(root/'test.c').write_text(source)
   subprocess.run([os.environ.get("CC", "cc"),'-std=c11','-O2','-Wall','-Wextra','-Werror',str(root/'test.c'),'-o',str(root/'test')],check=True);subprocess.run([str(root/'test')],check=True)
if __name__=='__main__':unittest.main()
