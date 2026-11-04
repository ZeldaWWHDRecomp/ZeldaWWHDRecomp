"""Exact late-equipment observer selection, native callback order, and nested scopes."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

class SwordlessLifecycle(unittest.TestCase):
    def test_phase_observers(self):
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uint32_t u32;typedef int32_t s32;typedef uint8_t u8;
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_HOOK_RETURN(a,n,p) void n p
static _Alignas(16) u8 memory[0x20000];
#define R_SW(type,address) (*(type*)(memory+(address)-0x10000))
#define WWHD_ADDR_save_info_pointer 0x101F84DC
static u32 save_pointer=0x10000;
#define WWHD_GAME_DATA(a,t) (*(t*)((a)==WWHD_ADDR_save_info_pointer?(void*)&save_pointer:(void*)(uintptr_t)(a)))
static u32 init_swordless,depth,nest,ff1,callbacks;
static u8 entry_sword,late_before,late_after;
static const u32 stage=0x18134,literal=0x1F000,stage_object=0x1F020,wrong=0x1F040;
u32 wwhd_dComIfG_getGameInfo(void){return stage-0x5134;}
#include "swordless_lifecycle.h"
static void text(const char*s){strcpy((char*)&R_SW(u8,stage),s);}
static void object(u32 p,u32 t,u32 v){R_SW(u32,p)=t;R_SW(u32,p+4)=v;}
static u8 expected_late(u8 equipped,u8 owned){
 if(randomizer_swordless_stage_is(stage,"GTower"))return equipped;
 if(randomizer_swordless_stage_is(stage,"Ojhous")&&equipped==255)return 0x38;
 return owned?equipped:255;
}
s32 randomizer_native_swordless_phase(u32 scene){
 assert(scene==91||scene==92);
 if(depth){randomizer_swordless_phase_actors_ready();randomizer_swordless_phase_string_ready(literal);randomizer_swordless_phase_string_ready(literal);randomizer_swordless_phase_string_ready(stage_object);return 97;}
 entry_sword=R_SW(u8,save_pointer+0x2E);++callbacks;
 // A matching sequence before the actor-init anchor must do nothing.
 randomizer_swordless_phase_string_ready(literal);randomizer_swordless_phase_string_ready(literal);randomizer_swordless_phase_string_ready(stage_object);
 assert(R_SW(u8,save_pointer+0x2E)==entry_sword);
 randomizer_swordless_phase_actors_ready();
 // FF1's real native equipment/ownership clearing remains before the late site.
 if(ff1){R_SW(u8,save_pointer+0x2E)=255;R_SW(u8,save_pointer+0xD4)=0;}
 late_before=R_SW(u8,save_pointer+0x2E);++callbacks;
 // Wrong vtable, wrong literal text, stage without pair and split literal objects.
 object(wrong,RANDOMIZER_PHASE_LITERAL,0);randomizer_swordless_phase_string_ready(wrong);
 randomizer_swordless_phase_string_ready(stage_object);
 object(wrong,stage+1,RANDOMIZER_PHASE_STRING_VTABLE);randomizer_swordless_phase_string_ready(wrong);
 randomizer_swordless_phase_string_ready(literal);
 object(wrong,RANDOMIZER_PHASE_LITERAL,RANDOMIZER_PHASE_STRING_VTABLE);randomizer_swordless_phase_string_ready(wrong);
 randomizer_swordless_phase_string_ready(stage_object);assert(R_SW(u8,save_pointer+0x2E)==late_before);
 // Nested phase saves and restores the outer marker and all caller state.
 randomizer_swordless_phase_string_ready(literal);
 if(nest){
  const randomizer_swordless_phase_scope previous=randomizer_swordless_phase;
  u8 name[9];memcpy(name,&R_SW(u8,stage),9);u8 equip=R_SW(u8,save_pointer+0x2E),owned=R_SW(u8,save_pointer+0xD4);
  depth=1;text("sea");R_SW(u8,save_pointer+0xD4)=0;R_SW(u8,save_pointer+0x2E)=0x3E;
  assert(randomizer_swordless_phase_load(92)==97);
  memcpy(&R_SW(u8,stage),name,9);R_SW(u8,save_pointer+0x2E)=equip;R_SW(u8,save_pointer+0xD4)=owned;depth=0;
  assert(!memcmp(&previous,&randomizer_swordless_phase,sizeof previous));
 }
 randomizer_swordless_phase_string_ready(literal);
 randomizer_swordless_phase_string_ready(stage_object);
 late_after=R_SW(u8,save_pointer+0x2E);++callbacks;
 assert(late_after==(init_swordless?expected_late(late_before,R_SW(u8,save_pointer+0xD4)):late_before));
 // One semantic site per phase. Repeated assurances must not remove an Orca grant.
 randomizer_swordless_phase_string_ready(literal);randomizer_swordless_phase_string_ready(literal);randomizer_swordless_phase_string_ready(stage_object);
 assert(R_SW(u8,save_pointer+0x2E)==late_after);
 return 97;
}
int main(void){
 const char*stages[]={"GTower","Ojhous","sea","Xboss0"};
 for(unsigned option=0;option<2;option++)for(unsigned st=0;st<4;st++)for(unsigned owned=0;owned<256;owned++)for(unsigned equipped=0;equipped<256;equipped++){
  memset(memory,0xA5,sizeof memory);randomizer_swordless_phase=(randomizer_swordless_phase_scope){0};init_swordless=option;callbacks=0;depth=0;nest=(owned==0&&equipped==255);ff1=0;
  text(stages[st]);object(literal,RANDOMIZER_PHASE_LITERAL,RANDOMIZER_PHASE_STRING_VTABLE);object(stage_object,stage,RANDOMIZER_PHASE_STRING_VTABLE);
  R_SW(u8,save_pointer+0xD4)=owned;R_SW(u8,save_pointer+0x2E)=equipped;
  u8 expected_save[0x1300];memcpy(expected_save,memory,sizeof expected_save);
  assert(randomizer_swordless_phase_load(91)==97);
  expected_save[0x2E]=late_after;assert(!memcmp(expected_save,memory,sizeof expected_save));
  assert(entry_sword==(option&&st==0&&equipped==255?0x3E:equipped));assert(callbacks==3);
  assert(R_SW(u8,save_pointer+0xD4)==owned&&!randomizer_swordless_phase.enabled&&!randomizer_swordless_phase.ready);
 }
 // Real FF1 clearing is retained and precedes the cleanup observer.
 memset(memory,0,sizeof memory);text("sea");object(literal,RANDOMIZER_PHASE_LITERAL,RANDOMIZER_PHASE_STRING_VTABLE);object(stage_object,stage,RANDOMIZER_PHASE_STRING_VTABLE);init_swordless=1;ff1=1;nest=0;R_SW(u8,save_pointer+0xD4)=7;R_SW(u8,save_pointer+0x2E)=0x3E;assert(randomizer_swordless_phase_load(91)==97);assert(late_before==255&&late_after==255&&R_SW(u8,save_pointer+0xD4)==0);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/"test.c";exe=Path(tmp)/"test"
            src.write_text(source)
            subprocess.run([os.environ.get("CC","cc"),"-std=c11","-O2","-Wno-int-to-pointer-cast","-Wno-pointer-to-int-cast","-I",str(Path(__file__).parent),str(src),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

if __name__ == "__main__":
    unittest.main()
