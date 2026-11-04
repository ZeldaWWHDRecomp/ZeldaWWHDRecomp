"""Source tablet decisions must preserve real queries and native callback effects."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class TempleTablets(unittest.TestCase):
    def test_consumers_and_nested_queries(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint16_t u16;typedef uint8_t u8;typedef float f32;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static unsigned char memory[0x10000] __attribute__((aligned(16)));
#define M8(a,o) memory[(a)+(o)]
#define M16(a,o) (*(uint16_t*)(memory+(a)+(o)))
#define M32(a,o) (*(uint32_t*)(memory+(a)+(o)))
#define R_TABLET_U8 M8
#define R_TABLET_U16 M16
#define R_TABLET_U32 M32
#define RANDOMIZER_TABLET_EVENT_NAME(i) (0x1000u+(i))
#define RANDOMIZER_LEAF_CONSTRUCTED(a) ((M32(a,0x2E4)&8)!=0)
static s32 wwhd_save_event_test(u32 bit){assert(bit==0x6940);return 1;}
#include "temple_tablets.h"
static unsigned owned,song,nest,depth,checks,queries,indices[8],num_indices,cut_calls,talk_calls,matrix_calls,bgm_calls,friend_calls,delegates;
static unsigned talk_result,run_event,farore_owned,leaf_depth,leaf_nest;
static const u32 actor=0x100,player=0x1000,play=0x2000;
static void nested_query(void){assert(randomizer_tablet_query(song,1)==(s32)owned);checks++;}
s32 randomizer_native_tablet_item(u8 item,s32 flag){return item==0x6B&&flag==1?(s32)farore_owned:(item==song&&flag==1)?(s32)owned:0;}
s32 randomizer_tablet_query(u8 item,s32 flag){queries++;return randomizer_tablet_item(item,flag);}
u32 randomizer_tablet_play(void){nested_query();return play;}
void randomizer_tablet_init_matrix(u32 a){assert(a==actor);nested_query();M32(a,0x620)++;}
void randomizer_tablet_set_matrix(u32 a){assert(a==actor);nested_query();matrix_calls++;M32(a,0x624)++;}
void randomizer_tablet_cull_box(u32 a,f32 x,f32 y,f32 z,f32 x1,f32 y1,f32 z1){
 assert(a==actor&&x==-400&&y==-1&&z==-400&&x1==400&&y1==405&&z1==400);nested_query();}
s32 randomizer_tablet_event_index(u32 manager,u32 name,s32 map){assert(manager==play+0x52C4&&map==255);indices[num_indices++]=name; nested_query();return (s32)name;}
void randomizer_tablet_event_name(u32 info,u32 name){
 assert(info==actor+0xF8&&name==(song==0x71?0x1002CB04:0x1002CB14));nested_query();
 /* A callback can mutate the later query input; it must see the real result. */
 assert(M8(actor,0x5FE)==song);M32(actor,0x39C)=0x40;
}
void randomizer_tablet_private_cut(u32 a){assert(a==actor);cut_calls++;nested_query();M32(a,0x628)^=0x1234;
 if(nest&&!depth){depth=1;const unsigned saved=run_event;M8(play,0x5292)=1;assert(randomizer_tablet_execute(actor,0x800)==1);M8(play,0x5292)=saved;depth=0;}}
u16 randomizer_tablet_talk(u32 a,s32 mode){assert(a==actor&&mode==1);talk_calls++;nested_query();M32(a,0x62C)^=0x2345;return (u16)talk_result;}
void randomizer_tablet_bgm(u32 music){assert(music==(song==0x71?0x80000021:0x80000022));bgm_calls++;nested_query();}
void randomizer_tablet_friends(s32 visible){assert(visible==1);friend_calls++;nested_query();}
s32 randomizer_native_tablet_execute(u32 a,u32 matrix){assert(a==actor&&matrix==0x800);delegates++;return 37;}
s32 randomizer_native_leaf_return_create(u32 a){
 assert(a==77||a==0x700);
 assert(randomizer_leaf_return_actor==a);
 if(!(M32(a,0x2E4)&8)){
  M32(0x900,0x2E4)=8; /* A different constructed actor cannot arm this one. */
  assert(randomizer_tablet_item(0x6B,1)==(s32)farore_owned);
  assert(randomizer_native_tablet_item(0x6B,1)==(s32)farore_owned);
  assert(randomizer_leaf_return_scope==1);
  if(leaf_nest&&!leaf_depth){leaf_depth=1;M32(0x700,0x2E4)=0;
   assert(randomizer_leaf_return_create(0x700)==4);
   assert(randomizer_leaf_return_actor==a&&randomizer_leaf_return_scope==1);leaf_depth=0;}
  M32(a,0x2E4)|=8;
 }
 assert(randomizer_tablet_item(0x6A,1)==0&&randomizer_leaf_return_scope==1);
 assert(randomizer_tablet_item(0x6B,0)==0&&randomizer_leaf_return_scope==1);
 assert(randomizer_tablet_item(0x6B,1)==0&&!randomizer_leaf_return_scope);
 assert(randomizer_tablet_item(0x6B,1)==(s32)farore_owned);
 return 4;
}
int main(void){
 randomizer_zunari_query=1;assert(randomizer_tablet_item(0x2A,0)==0 && randomizer_zunari_query);assert(randomizer_tablet_item(0x2A,1)==1 && !randomizer_zunari_query);assert(randomizer_tablet_item(0x2A,1)==0);
 for(owned=0;owned<2;owned++)for(song=0x70;song<=0x71;song++){
  memset(memory,0x5A,sizeof memory);num_indices=0;
  M32(actor,0x540)=0xA00;M32(actor,0x544)=0xC00;M8(actor,0x556)=song==0x71;
  assert(randomizer_tablet_create(actor)==1&&num_indices==4);
  const unsigned expected[]={2,0,4,6};for(unsigned i=0;i<4;i++)assert(indices[i]==0x1000+expected[i]+(song==0x71));
  assert(M32(actor,0x348)==0xCC8&&M32(actor,0x620)==0x5A5A5A5C);
  assert(M8(actor,0x557)==0&&M8(actor,0x5FE)==song&&M32(actor,0x610)==0xFFFFFFFF);
  assert(M32(actor,0x39C)==0x20000048);
  for(unsigned o=0x3E0;o<0x538;o++)assert(M8(actor,o)==0);
  for(unsigned state=0;state<15;state++)for(run_event=0;run_event<2;run_event++)for(talk_result=17;talk_result<=19;talk_result++)for(unsigned timer=0;timer<4;timer++)for(nest=0;nest<2;nest++){
   memset(memory,0,sizeof memory);M32(play,0x5B2C)=player;M32(play,0x5B38)=0x1800;M8(play,0x5292)=run_event;
   M8(actor,0x557)=state;M8(actor,0x556)=song==0x71;M8(actor,0x5FE)=song;M16(actor,0x54A)=timer;M16(actor,0x54C)=timer;
   M32(player,0x3B8)=0xFFFFFFFF;M16(play,0x52B8)=0x10;
   const unsigned before=delegates;
   if(state!=10&&state!=12){assert(randomizer_tablet_execute(actor,0x800)==37&&delegates==before+1);continue;}
   assert(randomizer_tablet_execute(actor,0x800)==1);
   const unsigned target=(state==10?!run_event:talk_result==18);
   assert(M8(actor,0x557)==(target?0:state));
   const unsigned tail_twice=state==10&&nest;
   const unsigned timer_after=timer==3&&!tail_twice?2:0;
   assert(M16(actor,0x54A)==timer_after&&M16(actor,0x54C)==timer_after);
   assert(M32(0x800,0)==0x1046A618);
   if(state==12&&talk_result==18){assert(M32(player,0x3B8)==0xF7FFFFFF&&M16(play,0x52B8)==0x18);}
   else assert(M32(player,0x3B8)==0xFFFFFFFF&&M16(play,0x52B8)==0x10);
  }
  for(farore_owned=0;farore_owned<2;farore_owned++)for(unsigned constructed=0;constructed<2;constructed++)for(leaf_nest=0;leaf_nest<2;leaf_nest++){
   M32(77,0x2E4)=constructed?8:0;assert(randomizer_leaf_return_create(77)==4&&!randomizer_leaf_return_scope&&!randomizer_leaf_return_actor);
  }
 }
 assert(checks&&queries&&matrix_calls&&cut_calls&&talk_calls&&bgm_calls&&friend_calls&&delegates);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'test.c').write_text(source)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(Path(__file__).resolve().parent), str(root / 'test.c'),
                            '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
