"""Exercise magic grants and the fairy's independent effect/cut lifecycle."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class MagicUpgrade(unittest.TestCase):
    def test_grant_and_fairy_lifecycle(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32; typedef int32_t s32; typedef uint8_t u8;
typedef int8_t s8; typedef uint16_t u16; typedef int16_t s16; typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 actor[0x700],info[0x6000],emitter[0x300],player[0x400];
static u16 fixture_effects[]={33616,33618,33620,33622};
#define WWHD_GAME_DATA(a,t) (*(t*)fixture_effects)
static unsigned particles,lights,cuts,change_counter;
u32 wwhd_dComIfG_getGameInfo(void){return (u32)info;}
#include "magic_upgrade.h"
u32 randomizer_fairy_particle(u32 manager,u32 group,u32 effect,u32 position,
 u32 angle,u32 scale,u32 alpha,u32 callback,s32 room,u32 primary,u32 environment,u32 extra){
 assert(manager==123 && !group && effect==fixture_effects[actor[0x63D]]);
 assert(position==(u32)player+0x314 && !angle && !scale && alpha==255 && !callback);
 assert(room==-1 && !primary && !environment && !extra);++particles;
 if(change_counter)*(s16*)(actor+0x604)=0;
 return 0;
}
void randomizer_fairy_light_end(u32 p){assert(p==(u32)actor && !cuts);++lights;}
void randomizer_fairy_cut_end(u32 p,s32 staff){assert(p==(u32)info+0x52C4 && staff==-3 && lights==1);++cuts;}
static void reset(void){
 memset(actor,0,sizeof(actor));memset(info,0,sizeof(info));memset(emitter,0,sizeof(emitter));
 particles=lights=cuts=change_counter=0;
 *(u32*)(info+0x5B34)=(u32)player;*(u32*)(info+0x5AB0)=123;
 *(s32*)(actor+0x608)=-3;
}
int main(void){
 /* Fixed pending values, not old+32, regardless of previous pending work. */
 for(unsigned old=0;old<65536;old++){
  *(u16*)(info+0x5B60)=old;*(u16*)(info+0x5B64)=65535-old;
  randomizer_magic_upgrade();assert(*(u16*)(info+0x5B60)==32 && *(u16*)(info+0x5B64)==32);
 }
 for(unsigned color=0;color<4;color++)for(unsigned alpha=0;alpha<256;alpha++){
  reset();actor[0x63D]=color;actor[0x620]=alpha;
  *(s16*)(actor+0x604)=26;*(u32*)(actor+0x628)=(u32)emitter;
  *(u32*)(emitter+0x254)=0xA0;*(f32*)(actor+0x638)=9;
  assert(randomizer_fairy_delete((u32)actor)==1 && particles==1 && !lights && !cuts);
  assert(*(f32*)(actor+0x5E8)==1);
  if(alpha>10){assert(actor[0x620]==alpha-10 && emitter[0x247]==alpha-10);assert(*(u32*)(actor+0x628)==(u32)emitter);}
  else{assert(!actor[0x620] && !emitter[0x247]);assert(*(s32*)(emitter+0x5C)==-1 && *(u32*)(emitter+0x254)==0xA1);assert(!*(u32*)(actor+0x628) && *(f32*)(actor+0x638)==0);}
 }
 reset();*(s16*)(actor+0x604)=26;change_counter=1;
 randomizer_fairy_delete((u32)actor);assert(particles==1 && !cuts && *(s16*)(actor+0x604)==0);
 for(unsigned flags=0;flags<65536;flags++){
  reset();*(s16*)(actor+0x604)=27;*(u16*)(actor+0x57C)=flags;actor[0xB3]=6;
  *(u16*)(info+0x5B60)=7;*(u16*)(info+0x5B64)=11;
  randomizer_fairy_delete((u32)actor);assert(!particles && lights==1 && cuts==1);
  assert(*(u16*)(actor+0x57C)==(flags&~1u));
  assert(*(u16*)(info+0x5B60)==7 && *(u16*)(info+0x5B64)==11);
 }
 reset();*(s16*)(actor+0x604)=27;*(f32*)(actor+0x5A4)=0.1f;
 randomizer_fairy_delete((u32)actor);assert(!particles && !lights && !cuts);
 reset();*(s16*)(actor+0x604)=32767;
 randomizer_fairy_delete((u32)actor);assert(*(s16*)(actor+0x604)==-32768 && !particles && !lights && !cuts);
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
