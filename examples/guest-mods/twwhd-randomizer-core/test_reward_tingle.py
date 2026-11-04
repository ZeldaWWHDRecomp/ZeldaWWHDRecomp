"""Control-flow tests for the actual gift replacement; live PPC tests cover its ABI."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class TingleRewards(unittest.TestCase):
    def test_reward_selection_fallback_and_creation_failure(self):
        source = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
/* Host pointer width for this control-flow fixture; the module builds separately as PPC32. */
typedef uintptr_t u32;
typedef int32_t s32;
typedef uint8_t u8;
typedef int8_t s8;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 info[0x6000],actor[0xB00];
static u32 parameter,fee,missing,created,received_item,received_pos;
static u32 received_room,received_flags,received_fourth,received_angle,received_scale;
static unsigned substance_calls,fee_calls,original_calls,create_calls,cut_calls;
static const u32 init_reward_tingle_first=0xAD,init_reward_tingle_second=0x31,init_reward_ankle_statues=0x0F;
u32 wwhd_dComIfG_getGameInfo(void){return (u32)info;}
u8 wwhd_daNpc_Tc_c__checkAllMonumentFee(void* self){assert(self==actor);++fee_calls;return (u8)fee;}
u32 wwhd_fopAcM_createItemForPresentDemo(u32 pos,u32 item,u32 flags,u32 fourth,u32 room,u32 angle,u32 scale){
 ++create_calls;received_item=item;received_pos=pos;received_room=room;
 received_flags=flags;received_fourth=fourth;received_angle=angle;received_scale=scale;
 return created;
}
#include "reward_tingle.h"
void randomizer_native_tingle_present(u32 self,s32 staff){assert(self==(u32)actor && staff==17);++original_calls;}
u32 randomizer_event_substance(u32 manager,s32 staff,const char* name,s32 type){
 assert(manager==(u32)info+0x52C4 && staff==17 && !strcmp(name,"ItemType") && type==3);
 ++substance_calls;return missing?0:(u32)&parameter;
}
void randomizer_event_cut_end(u32 manager,s32 staff){assert(manager==(u32)info+0x52C4 && staff==17);++cut_calls;}
static void reset(u32 type,u8 actor_type,u32 all_fees,u32 result){
 memset(info,0,sizeof(info));memset(actor,0,sizeof(actor));
 parameter=type;fee=all_fees;missing=0;created=result;
 actor[0x970]=actor_type;actor[0x326]=(u8)-3;
 *(u32*)(info+0x52A0)=0x1234;
 substance_calls=fee_calls=original_calls=create_calls=cut_calls=0;
}
static void invoke(void){randomizer_tingle_present((u32)actor,17);assert(substance_calls==1);}
int main(void){
 const u32 rewards[]={0xAD,0x31,0x0F};
 for(u32 type=0;type<3;type++){
  reset(type,1,1,0xFFFFFFFFu);invoke();
  assert(received_item==rewards[type] && create_calls==1 && !original_calls && !cut_calls);
  assert(*(u32*)(info+0x52A0)==0x1234);
  reset(type,1,1,0x5678);invoke();
  assert(received_item==rewards[type] && create_calls==1 && !original_calls && cut_calls==1);
  assert(*(u32*)(info+0x52A0)==0x5678);
  assert(received_pos==(u32)actor+0x314 && received_room==(u32)(s8)-3);
  assert(!received_flags && received_fourth==0xFFFFFFFFu && !received_angle && !received_scale);
 }
 /* Missing ItemType means the first gift, as in the original function. */
 reset(99,0,0,0xFFFFFFFFu);missing=1;invoke();assert(received_item==rewards[0] && !original_calls);
 /* Other Tingle roles and unpaid statues retain the original native behavior. */
 for(u8 actor_type=0;actor_type<=4;actor_type++){
  reset(2,actor_type,0,0xFFFFFFFFu);invoke();assert(original_calls==1 && !create_calls && !cut_calls);
  assert(fee_calls==(actor_type==1));
 }
 reset(3,1,1,0xFFFFFFFFu);invoke();assert(original_calls==1 && !create_calls);
 reset(0xFFFFFFFFu,1,1,0xFFFFFFFFu);invoke();assert(original_calls==1 && !create_calls);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / "test.c").write_text(source)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(Path(__file__).resolve().parent), str(path / "test.c"),
                            "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
