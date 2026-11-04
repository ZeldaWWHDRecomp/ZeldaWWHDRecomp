"""Input priority, actual guard eligibility, blend/collider behavior and save isolation."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

class ShieldlessHammer(unittest.TestCase):
    def test_consumers(self):
        header=Path(__file__).with_name("shieldless_hammer.h")
        imports=re.findall(r"WWHD_GAME_FUNC\([^,]+,([^,]+),(\w+),\((.*?)\)\);",header.read_text())
        enum="enum {"+",".join("C_"+name for _,name,_ in imports)+",C_virtual_guard,C_native_buttons,C_native_guard,C_count};"
        overrides={
            "shield_lockon_truth":"return lock_truth;",
            "shield_getReadyItem":"if(published_item) R_SW(u16,self+0x69B0)=published_item;return answers[C_shield_getReadyItem];",
            "shield_free_grab":"return;",
            "shield_set_upper":"assert(animation==0x1B&&part==2&&rate==1.25f&&start==0.0f&&end==-1&&morph==2.4f);R_SW(u16,self+0x5888)=animation;return 1;",
            "shield_chase":"if(*value<target){*value+=step;if(*value>target)*value=target;}else{*value-=step;if(*value<target)*value=target;}return *value==target;",
            "shield_joint_tree":"assert(model_data==model_data_token);return tree_token;",
            "shield_joint_count":"assert(resource==tree_token&&argument==0);return joints;",
        }
        mocks=[]
        for ret,name,args in imports:
            argnames=[a.strip().split()[-1].lstrip("*") for a in args.split(",") if a.strip()!="void"]
            unused="".join("(void)"+a+";" for a in argnames)
            body=overrides.get(name,"return;" if ret=="void" else "return answers[C_"+name+"];")
            mocks.append(f"{ret} {name}({args}){{{unused}record(C_{name});{body}}}")
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <math.h>
typedef uint32_t u32;typedef uint16_t u16;typedef uint8_t u8;typedef int32_t s32;typedef int16_t s16;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 memory[0x80000],save_expected[0x1300];
#define R_SW(type,address) (*(volatile type*)(memory+(address)-0x10000))
static const u32 self=0x10000,play=0x22000,save=0x32000,attention=0x39000;
static u32 save_pointer=0x32000;static u16 curse;
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_GAME_DATA(a,t) (*(t*)((a)==1?(void*)&save_pointer:(void*)&curse))
static s32 guard_virtual(u32 actor);
#define R_SHIELD_GUARD(actor) guard_virtual(actor)
static s32 trigger_override=-1;static unsigned bridge_calls,dispatch_calls,dispatch_item,published_item;
void wwhd_item_action_dispatch(u32 actor,u32 item);
u32 wwhd_item_ready_trigger(u32 actor,u32 native){assert(actor==self);++bridge_calls;return trigger_override<0?native:(u32)trigger_override;}
u32 wwhd_dComIfG_getGameInfo(void){return play;}
ENUM
static s32 answers[C_count];static unsigned counts[C_count],trace[512],trace_size;static u8 lock_truth,guard_truth;static s32 joints;
static const u32 blend=0x40000,data=0x41000,weights=0x42000,model=0x43000,frame=0x44000,model_data_token=0x5500,tree_token=0x6600;
static void record(unsigned id){assert(id<C_count&&trace_size<512);trace[trace_size++]=id;++counts[id];}
void wwhd_item_action_dispatch(u32 actor,u32 item){
 assert(actor==self&&R_SW(u16,self+0x69B0)==item);
 assert(trace_size&&trace[trace_size-1]==(item==0x25?C_shield_checkRopeAnime:C_shield_getReadyItem));
 assert(!counts[C_shield_checkNextHookshotMode]&&!counts[C_shield_checkNextRopeMode]&&!counts[C_shield_checkNextBoomerangMode]);
 ++dispatch_calls;dispatch_item=item;
}
#include "shieldless_hammer.h"
MOCKS
static s32 guard_virtual(u32 actor){assert(actor==self);record(C_virtual_guard);return guard_truth;}
s32 shield_native_buttons(u32 actor){assert(actor==self);record(C_native_buttons);return 666;}
void shield_native_guard(u32 actor){assert(actor==self);record(C_native_guard);}
static void reset(void){
 memset(memory,0,sizeof memory);memset(answers,0,sizeof answers);memset(counts,0,sizeof counts);trace_size=0;lock_truth=guard_truth=curse=0;joints=2;trigger_override=-1;bridge_calls=0;dispatch_calls=dispatch_item=published_item=0;
 R_SW(u16,self+0x69B0)=0x33;R_SW(u8,save+0x2F)=255;R_SW(u16,self+0x5888)=0xFFFF;R_SW(u32,self+0x6894)=attention;R_SW(u32,self+0x6590)=0xFFFFFFFF;
 R_SW(u32,self+0x57F4)=blend;R_SW(u32,blend+0x7C)=data;R_SW(u32,blend+0x80)=model;R_SW(u32,model+0xAC)=model_data_token;R_SW(u32,data+0x2C)=weights;R_SW(f32,weights)=0.2f;R_SW(f32,weights+4)=0.3f;R_SW(f32,weights+8)=99.0f;
 R_SW(u32,self+0x583C)=frame;R_SW(f32,self+0x589C)=3.5f;
 answers[C_shield_checkGuardAccept]=1;answers[C_shield_checkItemChangeFromButton]=99;
 // Proc imports are not contiguous: explicitly assign every side-effect action a unique return.
 PROC_ANSWERS
 memcpy(save_expected,memory+save-0x10000,sizeof save_expected);
}
static void isolated(void){assert(!memcmp(save_expected,memory+save-0x10000,sizeof save_expected));}
static void near(f32 a,f32 b){assert(fabsf(a-b)<0.000001f);}
int main(void){
 assert(shield_abs(-2147483647-1)==(-2147483647-1));assert(shield_abs(-31)==31&&shield_abs(31)==31);
 // All non-target input/guard invocations delegate directly and touch no save state.
 for(unsigned item=0;item<0x106;item++)for(unsigned shield=0;shield<256;shield++){
  if(item==0x33&&shield==255)continue;reset();R_SW(u16,self+0x69B0)=item;R_SW(u8,save+0x2F)=shield;memcpy(save_expected,memory+save-0x10000,sizeof save_expected);
  assert(shieldless_hammer_buttons(self)==666);shieldless_hammer_guard(self);assert(counts[C_native_buttons]==1&&counts[C_native_guard]==1&&trace_size==2);isolated();
 }
 // Guard request/acceptance/R-button combinations: only real accepted defense may start its proc.
 for(unsigned accept=0;accept<2;accept++)for(unsigned held=0;held<2;held++)for(unsigned blocked=0;blocked<3;blocked++){
  reset();answers[C_shield_checkGuardAccept]=accept;R_SW(u8,self+0x690E)=held?0x40:0;
  if(blocked==1)answers[C_shield_checkEquipAnime]=1;if(blocked==2)answers[C_shield_checkUpperReadyThrowAnime]=1;
  const s32 result=shieldless_hammer_buttons(self);const unsigned allow=accept&&!blocked;
  assert(result==(allow&&held?answers[C_shield_procCrouchDefense_init]:99));assert(R_SW(u8,play+0x5BB5)==(allow?0x36:0));assert(counts[C_shield_procCrouch_init]==0);
  if(allow&&held){const unsigned expected[]={C_shield_checkBowAnime,C_shield_checkRopeAnime,C_shield_setDoStatus,C_shield_orderTalk,C_shield_checkEquipAnime,C_virtual_guard,C_shield_itemTrigger,C_shield_changeSpecialBattle,C_shield_checkUpperReadyThrowAnime,C_shield_checkEquipAnime,C_shield_checkGuardAccept,C_shield_procCrouchDefense_init};assert(trace_size==sizeof expected/sizeof *expected&&!memcmp(trace,expected,sizeof expected));}
  isolated();
 }
 // Existing call/NPC priority, nonzero R status and early returns remain ahead of guard handling.
 reset();R_SW(u32,self+0x3BC)=2;R_SW(u8,self+0x690D)=0x40;assert(shieldless_hammer_buttons(self)==answers[C_shield_procCall_init]);assert(R_SW(u8,play+0x5BB5)==0x24&&!counts[C_shield_procCrouchDefense_init]);isolated();
 reset();R_SW(u8,play+0x5BB5)=0x19;R_SW(u8,self+0x690E)=0x40;assert(shieldless_hammer_buttons(self)==99&&R_SW(u8,play+0x5BB5)==0x19&&!counts[C_shield_checkGuardAccept]);isolated();
 reset();answers[C_shield_orderTalk]=1;assert(shieldless_hammer_buttons(self)==1&&!counts[C_shield_checkGuardAccept]&&R_SW(u8,play+0x5BB5)==0);isolated();
 reset();answers[C_shield_changeSpecialBattle]=1;assert(shieldless_hammer_buttons(self)==1&&!counts[C_shield_checkGuardAccept]);isolated();
 reset();R_SW(u16,self+0x5888)=0x34;assert(shieldless_hammer_buttons(self)==1&&!counts[C_shield_checkGuardAccept]);isolated();
 for(unsigned upper=0x95;upper<=0x96;upper++){reset();R_SW(u16,self+0x5888)=upper;answers[C_shield_checkNextActionGrab]=1;assert(shieldless_hammer_buttons(self)==1&&!counts[C_shield_checkGuardAccept]);isolated();}
 // The actual hammer attack still outranks defense with the same directional threshold.
 for(unsigned direction=0;direction<4;direction++)for(unsigned moving=0;moving<2;moving++){
  reset();answers[C_shield_itemTrigger]=1;answers[C_shield_getReadyItem]=0x33;answers[C_shield_getDirectionFromShapeAngle]=direction;R_SW(f32,self+0x6A08)=moving?0.051f:0.05f;
  const unsigned expected=moving&&(direction==2||direction==3)?C_shield_procHammerSideSwing_init:C_shield_procHammerFrontSwingReady_init;
  assert(shieldless_hammer_buttons(self)==answers[expected]&&counts[expected]==1&&!counts[C_shield_checkGuardAccept]);isolated();
 }
 // An accepted quick-use intent may replace only the local zero predicate.
 for(unsigned slot=0;slot<3;slot++){
  reset();trigger_override=4u<<slot;answers[C_shield_getReadyItem]=0x33;
  const u8 before_input=R_SW(u8,self+0x690D);
  assert(shieldless_hammer_buttons(self)==answers[C_shield_procHammerFrontSwingReady_init]);
  assert(bridge_calls==1&&counts[C_shield_itemTrigger]==1&&counts[C_shield_procHammerFrontSwingReady_init]==1);
  assert(R_SW(u8,self+0x690D)==before_input&&R_SW(u16,self+0x69B0)==0x33);isolated();
 }
 // getReadyItem observers can change equipment after the initial hammer-only guard.
 // Each accepted live dispatch composes the port immediately before its native call.
 for(unsigned which=0;which<3;which++){
  const unsigned items[]={0x2F,0x25,0x2D};
  const unsigned actions[]={C_shield_checkNextHookshotMode,C_shield_checkNextRopeMode,C_shield_checkNextBoomerangMode};
  reset();published_item=items[which];answers[C_shield_getReadyItem]=published_item;answers[C_shield_itemTrigger]=1;
  assert(shieldless_hammer_buttons(self)==answers[actions[which]]);
  assert(dispatch_calls==1&&dispatch_item==items[which]&&counts[actions[which]]==1);
  assert(trace[trace_size-1]==actions[which]&&R_SW(u16,self+0x69B0)==published_item);isolated();
  reset();published_item=items[which];answers[C_shield_getReadyItem]=0x33;answers[C_shield_itemTrigger]=1;
  assert(shieldless_hammer_buttons(self)==99&&dispatch_calls==0&&counts[actions[which]]==0);isolated();
 }
 // The original selected-item equality still rejects a mismatched ready item.
 reset();trigger_override=4;answers[C_shield_getReadyItem]=0x2F;
 assert(shieldless_hammer_buttons(self)==99&&!counts[C_shield_procHammerFrontSwingReady_init]);isolated();
 // Guard eligibility and held-object lifetime, including the attention fallback flag.
 for(unsigned lock=0;lock<3;lock++)for(unsigned accept=0;accept<2;accept++)for(unsigned keep=0;keep<3;keep++){
  reset();lock_truth=lock==1;if(lock==2)R_SW(u32,attention+0x20)=0x20000000;answers[C_shield_checkGuardAccept]=accept;R_SW(u32,self+0x6590)=keep==0?0xFFFFFFFF:10;R_SW(u32,self+0x6594)=keep==2?0x19999:0;
  shieldless_hammer_guard(self);assert(R_SW(u8,play+0x5BB5)==(lock&&accept&&keep!=1?0x36:0));isolated();
 }
 // A fresh guard selects the real hammer animation; native upper/under frame coupling is retained.
 reset();lock_truth=1;R_SW(u8,self+0x690E)=0x40;R_SW(u16,self+0x5848)=0x22;guard_truth=1;R_SW(u32,self+0x76F0)=0xA0;
 shieldless_hammer_guard(self);assert(R_SW(u8,play+0x5BB5)==0x36&&counts[C_shield_free_grab]==1&&counts[C_shield_resetActAnimeUpper]==1&&counts[C_shield_set_upper]==1);near(R_SW(f32,frame),3.5f);assert(R_SW(u32,self+0x76F0)==0xA1);{const unsigned expected[]={C_shield_checkEquipAnime,C_shield_lockon_truth,C_shield_checkGuardAccept,C_shield_free_grab,C_shield_resetActAnimeUpper,C_shield_set_upper,C_virtual_guard};assert(trace_size==sizeof expected/sizeof *expected&&!memcmp(trace,expected,sizeof expected));}isolated();
 // Live blend growth and release keep every joint's ratio and spare entries correct.
 reset();lock_truth=1;R_SW(u8,self+0x690E)=0x40;R_SW(u16,self+0x5888)=0x1B;shieldless_hammer_guard(self);near(R_SW(f32,weights),0.2f+0.41666666f);near(R_SW(f32,weights+4),R_SW(f32,weights));assert(R_SW(f32,weights+8)==99.0f&&counts[C_shield_chase]==1&&!counts[C_shield_set_upper]);isolated();
 reset();R_SW(u16,self+0x5888)=0x1B;R_SW(f32,weights)=0.8f;shieldless_hammer_guard(self);near(R_SW(f32,weights),0.8f-0.41666666f);assert(!counts[C_shield_resetActAnimeUpper]);isolated();
 reset();R_SW(u16,self+0x5888)=0x1B;shieldless_hammer_guard(self);assert(counts[C_shield_resetActAnimeUpper]==1&&!counts[C_shield_joint_count]);isolated();
 for(unsigned slip=0;slip<2;slip++){reset();R_SW(s32,self+0x65F0)=slip?0x6D:0xD;R_SW(u8,self+0x690E)=0x40;shieldless_hammer_guard(self);assert(R_SW(u8,play+0x5BB5)==0x36&&!counts[C_shield_checkEquipAnime]&&!counts[C_shield_free_grab]);isolated();}
 // A weight store can alias the blend-data pointer. The second joint must reload it.
 reset();R_SW(u32,data+0x2C)=blend+0x7C;const u32 data2=0x45000,weights2=0x46000;R_SW(u32,data2+0x2C)=weights2;f32 bits;memcpy(&bits,&data2,4);shield_set_joint_ratio(blend,2,bits);assert(R_SW(u32,blend+0x7C)==data2&&R_SW(u32,weights2+4)==data2);isolated();
 return 0;
}
'''
        procs="".join(f"answers[C_{name}]=1000+C_{name};" for _,name,_ in imports if name.startswith("shield_proc"))
        source=source.replace("ENUM",enum).replace("MOCKS","\n".join(mocks)).replace("PROC_ANSWERS",procs)
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/"test.c";exe=Path(tmp)/"test"
            src.write_text(source)
            subprocess.run([os.environ.get("CC","cc"),"-std=c11","-O2","-fno-strict-aliasing","-Wno-int-to-pointer-cast","-Wno-pointer-to-int-cast","-I",str(header.parent),str(src),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

if __name__ == "__main__":
    unittest.main()
