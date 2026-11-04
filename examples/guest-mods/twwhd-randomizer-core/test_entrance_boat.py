"""Missing-boat continuation must leave earlier ground failures and other spawns alone."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class EntranceBoat(unittest.TestCase):
    def test_failure_provenance_and_fallback_tail(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef uint16_t u16;typedef int16_t s16;typedef int32_t s32;typedef float f32;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_HOOK_RETURN(a,n,p) void n p
static unsigned char memory[0x30000] __attribute__((aligned(16)));
static unsigned char* ptr(u32 a){return memory+(a>=0x10460000?a-0x10460000+0x10000:a);}
/* Host field seam: model the packed BE mode byte independently of the LE flags word. */
static u8 saved_mode;
static u8* ptr8(u32 a){return a==0xB177?&saved_mode:ptr(a);}
#define M8(a,o) (*ptr8((a)+(o)))
#define M16(a,o) (*(uint16_t*)ptr((a)+(o)))
#define M32(a,o) (*(uint32_t*)ptr((a)+(o)))
#define F32(a,o) (*(float*)ptr((a)+(o)))
#define R_BOAT_U8 M8
#define R_BOAT_U16 M16
#define R_BOAT_U32 M32
#define R_BOAT_F32 F32
#define WWHD_ADDR_save_info_pointer 1
static const u32 save_address=0xA000,actor=0x1000;
#define WWHD_GAME_DATA(a,t) save_address
#include "entrance_boat.h"
static unsigned late,native_result,swim,callbacks,tail_calls,nesting,depth,native_calls;
static void cb(void){assert(!randomizer_bgwait_actor);callbacks++;}
s32 randomizer_native_bgwait(u32 a){
 assert(a==actor||a==actor+0x8000);native_calls++;
 assert(randomizer_bgwait_actor==a&&!randomizer_bgwait_late);
 if(nesting&&!depth){depth=1;const unsigned l=late,r=native_result;late=1;native_result=2;M32(a+0x8000,0xB0)=5<<12;
  assert(randomizer_bgwait(a+0x8000)==2);late=l;native_result=r;depth=0;assert(randomizer_bgwait_actor==a&&!randomizer_bgwait_late);}
 randomizer_bgwait_room_ready(a+4);assert(!randomizer_bgwait_late);
 if(late){randomizer_bgwait_room_ready(a);assert(!randomizer_bgwait_late);randomizer_bgwait_water_ready(a+4);assert(!randomizer_bgwait_late);randomizer_bgwait_water_ready(a);randomizer_bgwait_room_ready(a);assert(randomizer_bgwait_late==2);}
 return native_result;
}
s32 randomizer_boat_change_swim(u32 a){assert(a==actor);cb();return swim;}
s32 randomizer_boat_swim_wait(u32 a,s32 keep){assert(a==actor&&!keep);cb();return 0;}
s32 randomizer_boat_swim_move(u32 a,s32 keep){assert(a==actor&&!keep);cb();return 0;}
s32 randomizer_boat_wait(u32 a){assert(a==actor);cb();return 0;}
s32 randomizer_boat_move(u32 a){assert(a==actor);cb();return 0;}
s32 randomizer_boat_damage_up(u32 a,s32 type,s32 large,s16 x,s16 z){assert(a==actor&&type==-2&&large==1&&!x&&!z);cb();return 0;}
void randomizer_boat_sub_bgm_stop(void){cb();}
void randomizer_boat_old_frame(u32 data,f32 morph,u32 first,u32 last){assert(data==0xD00&&morph==0&&!first&&last==0x2A);tail_calls++;cb();}
#define ACTOR_VOID(name) void name(u32 a){assert(a==actor);cb();}
ACTOR_VOID(randomizer_boat_world_matrix) ACTOR_VOID(randomizer_boat_hat) ACTOR_VOID(randomizer_boat_anime) ACTOR_VOID(randomizer_boat_ratio) ACTOR_VOID(randomizer_boat_attention)
void randomizer_boat_model_calc(u32 model){assert(model==0x20000||model==0x22000||model==0x24000);cb();}
s32 randomizer_boat_item_model(u32 a){assert(a==actor);cb();return 0;}
void randomizer_boat_cylinder_center(u32 cyl,u32 pos){assert(cyl==actor+0x7774&&pos==actor+0x314);cb();}
u32 randomizer_boat_play(void){cb();return 0xC000;}
u32 randomizer_boat_ground_code(u32 bg,u32 poly){assert(bg==0xD2A0&&poly==actor+0x8F4);cb();return 0x12345678;}
s32 randomizer_boat_dragon_shield(u32 a,s32 enabled){assert(a==actor&&!enabled);cb();M32(save_address,0x1174)|=0x4000;return 0;}
int main(void){
 unsigned cases=0;
 for(unsigned spawn=0;spawn<16;spawn++)for(unsigned mode=0;mode<8;mode++)for(late=0;late<2;late++)for(native_result=0;native_result<=2;native_result++)for(swim=0;swim<2;swim++)for(unsigned event=0;event<2;event++)for(unsigned flags=0;flags<4;flags++){
  memset(memory,0,sizeof memory);callbacks=tail_calls=0;M32(actor,0xB0)=spawn<<12;M8(save_address,0x1177)=mode;
  M32(actor,0x69E0)=event?0xFF:3;M32(actor,0x65CC)=0xD00;M8(actor,0x68DE)=99;
  M32(actor,0x448)=0x20000;M32(actor,0x44C)=0x22000;M32(actor,0x450)=0x24000;
  M32(0x20000,0x2C)=0x20800;M32(0x20800,0x10)=0x21000;
  for(unsigned i=0;i<12;i++)F32(0x21000,15*0x30+4*i)=(float)i-4.0f;
  F32(save_address,0x1170)=23.0f;F32(actor,0x8A0)=17.0f;F32(actor,0x318)=90.0f;
  M16(actor,0x322)=0xDEAD;M32(save_address,0x1174)=0x12340000|(flags&1?0x8000:0)|(flags&2?0x4000:0);
  const unsigned expected=native_result==0&&late&&spawn==5&&(mode==1||mode==5||mode==6);
  assert(randomizer_bgwait(actor)==(expected?2:(s32)native_result));
  assert(!randomizer_bgwait_actor&&!randomizer_bgwait_late);
  assert(tail_calls==expected);if(!expected){assert(!callbacks);continue;}
  assert(M32(actor,0x69CC)==0x12345678&&M32(actor,0x69D0)==0x12345678);
  assert(M16(0x20800,4)&0x10);for(unsigned i=0;i<12;i++)assert(F32(0x22000,0xC8+4*i)==(float)i-4.0f&&F32(0x24000,0xC8+4*i)==(float)i-4.0f);
  if(mode==5){assert(F32(actor,0x318)==(swim?90.0f:17.0f));if(swim)assert(!M8(actor,0x68DE));}
  else if(event){assert(M16(actor,0x420)==4&&M16(actor,0x424)==0x23&&M16(actor,0x422)==0xDEAD&&M32(actor,0x430)==14);assert(F32(actor,0x370)==(swim?23.0f:17.0f));}
  else if(swim)assert(!M8(actor,0x68DE));
  assert(M16(actor,0x699E)==(flags&1?0:0x1234));
  assert(!!(M32(actor,0x3BC)&0x8000)==!!(flags&3));cases++;
 }
 assert(cases==48);
 const uint32_t bits[12]={0,0x80000000,0x7F800000,0xFF800000,0x7F800001,0xFF800001,0x7FC12345,0xFFC12345,1,0x007FFFFF,0x3F800000,0x80000001};
 for(unsigned alias=0;alias<2;alias++){
  const u32 root=0x20000,block=0x20800,matrix=0x21000+15*0x30;
  const u32 target=alias?matrix-0xC8+4:0x22000;
  M32(root,0x2C)=block;M32(block,0x10)=0x21000;M16(block,4)=0x22;
  for(unsigned i=0;i<12;i++)M32(matrix,4*i)=bits[i];
  randomizer_boat_copy_head_matrix(root,target);
  assert(M16(block,4)==0x32);for(unsigned i=0;i<12;i++)assert(M32(target,0xC8+4*i)==bits[i]);
 }

 /* A nested late/successful call cannot make the outer earlier failure late. */
 nesting=1;late=0;native_result=0;M32(actor,0xB0)=5<<12;M8(save_address,0x1177)=1;
 assert(randomizer_bgwait(actor)==0&&!randomizer_bgwait_actor&&!randomizer_bgwait_late);
 assert(native_calls>12288);
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
