import os
from pathlib import Path
import subprocess,tempfile,unittest
class ReplacementCutsceneGates(unittest.TestCase):
 def test_marie_eligibility_and_tingle_exact_store_omission(self):
  header=Path(__file__).with_name('npc_replacement_cutscene_gates.h').read_text().replace('volatile u32*','volatile uint32_t*')
  for offset in ['actor+0x3B4','randomizer_gate_info()+0x5B34']:
   header=header.replace('*(volatile uint32_t*)('+offset+')','*(volatile u32*)('+offset+')')
  source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <math.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef int8_t s8;typedef uint16_t u16;typedef int16_t s16;typedef float f32;
static _Alignas(16) u8 actor[0x900],save[2][0x1300],play[2][0x6000],morf[2][0xC0],environment[2][0x1100],gfx[2][0x300];
static u32 saveptr,gfxptr;static unsigned family,night,new_flag,old_flag,wrong_flag,registered,heart,pendant,tmp,first,ended,animation_calls,night_calls,event_calls,end_calls,delete_calls,env_calls,on_calls,off_calls,game,mutation,trace[128],n;
static void note(unsigned code){assert(n<128);trace[n++]=code;}
static void* data(u32 address){if(address==1)return &saveptr;assert(address==0x101F8344);note(14);return &gfxptr;}
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#include "npc_replacement_cutscene_gates.h"
u32 randomizer_gate_info(void){note(1);return(u32)play[game];}
s32 randomizer_gate_event_query(u32 self,u32 flag){event_calls++;note(2);assert(self==saveptr+(flag==0x104||flag==0x580?0x1178u:0x644u));assert(flag!=0x1E04&&flag!=0x3F80);if(flag==0x1F80){if(mutation==1)*(u8*)(actor+0x81A)=0x80;return new_flag;}if(flag==0x1E01)return first;if(flag==0x1C08)return heart;if(flag==0x1C04)return pendant;if(flag==0x104||flag==0x580)return tmp;assert(0);return 0;}
void randomizer_gate_event_on(u32 self,u32 flag){note(3);on_calls++;assert(self==saveptr+0x644);if(family==0){assert(flag==0x1E01);first=1;}else{assert(family==2&&flag==0xB80);if(mutation==2)game=1;}}
void randomizer_gate_event_off(u32 self,u32 flag){note(4);off_calls++;assert(self==saveptr+0x1178&&flag==0x580);}
u8 randomizer_gate_event_reg(u32 self,u32 flag){note(5);assert(self==saveptr+0x644&&flag==0xC0FF);return registered;}
u8 randomizer_gate_ivan_talk(u32 self){(void)self;assert(0);return 0;}
void randomizer_gate_ivan_animation(u32 self){(void)self;assert(0);}
s32 randomizer_gate_ivan_demo_check(u32 self){(void)self;assert(0);return 0;}
u8 randomizer_gate_ivan_demo(u32 self){(void)self;assert(0);return 0;}
u8 randomizer_gate_ivan_next_visit(u32 self){(void)self;assert(0);return 0;}
void randomizer_gate_ivan_visit_init(u32 self,u8 mode){(void)self;(void)mode;assert(0);}
void randomizer_gate_ivan_visit(u32 self){(void)self;assert(0);}
void randomizer_gate_ivan_event(u32 self){(void)self;assert(0);}
s32 randomizer_gate_chase(u32 speed,f32 target,f32 step){(void)speed;(void)target;(void)step;assert(0);return 0;}
s16 randomizer_gate_actor_angle(u32 a,u32 b){(void)a;(void)b;assert(0);return 0;}
void randomizer_gate_marie_animation(u32 self){assert(self==(u32)actor);note(6);animation_calls++;*(u32*)(actor+0x3B4)=(u32)morf[1];actor[0x806]=77;}
s32 randomizer_gate_night_stop(void){note(7);night_calls++;if(mutation==1)actor[0x81A]=0x40;return night;}
s32 randomizer_gate_tingle_end(u32 manager,u32 name){note(8);end_calls++;assert(manager==(u32)play[0]+0x52C4&&name==0x10022C5C);if(mutation==2)saveptr=(u32)save[1];return ended;}
s32 randomizer_gate_actor_delete(u32 self){note(9);delete_calls++;assert(self==(u32)actor);assert((*(u16*)(play[game]+0x52B8)&8));if(mutation==2)gfxptr=(u32)gfx[1];return -7;}
u32 randomizer_gate_environment(void){note(10);const unsigned index=env_calls++;if(mutation==2&&index==0){*(uint32_t*)(actor+0x7F4)=204;return(u32)environment[0];}return(u32)environment[mutation==2?1:0];}
static void reset(unsigned f){memset(actor,0,sizeof(actor));memset(save,0,sizeof(save));memset(play,0,sizeof(play));memset(morf,0,sizeof(morf));memset(environment,0,sizeof(environment));memset(gfx,0,sizeof(gfx));family=f;animation_calls=night_calls=event_calls=end_calls=delete_calls=env_calls=on_calls=off_calls=game=mutation=n=0;saveptr=(u32)save[0];gfxptr=(u32)gfx[0];*(u32*)(actor+0x3B4)=(u32)morf[0];gfx[0][0x243]=91;gfx[1][0x243]=92;}
int main(void){
 for(unsigned item=0;item<3;item++)for(new_flag=0;new_flag<2;new_flag++)for(old_flag=0;old_flag<2;old_flag++)for(wrong_flag=0;wrong_flag<2;wrong_flag++)for(registered=0;registered<256;registered++)for(heart=0;heart<2;heart++)for(pendant=0;pendant<2;pendant++)for(tmp=0;tmp<2;tmp++){
  reset(0);actor[0x81B]=3;play[0][0x52B1]=item==0?0x1F:item==1?0:255;
  u32 expected=item||!new_flag?0x2739:!registered?0x273A:!heart?0x2749:!pendant?(tmp?0x274A:0x274C):registered>=99?0x275E:0x2754;
  assert(randomizer_marie_get_message((u32)actor)==expected&&event_calls==(item?0u:!new_flag?1u:1u+!!registered+!!registered*!!heart+!!registered*!!heart*!pendant));
 }
 for(unsigned state=0;state<256;state++){
  if(state==1||state==3){continue;}
  reset(0);actor[0x81B]=state;*(uint32_t*)(actor+0x808)=0xF1234567;assert(randomizer_marie_get_message((u32)actor)==(state==4?0xF1234567u:0)&&n==0);
 }
 for(night=0;night<2;night++)for(first=0;first<2;first++)for(registered=0;registered<256;registered++){
  reset(0);actor[0x81B]=1;u32 expected=night?0x2722:!first?0x2711:registered?0x2713:0x2712;const unsigned was_first=first;assert(randomizer_marie_get_message((u32)actor)==expected&&night_calls==1&&on_calls==(!night&&!was_first));
 }
 for(unsigned flags=0;flags<4;flags++)for(unsigned phase=0;phase<4;phase++){
  reset(1);actor[0x81B]=9;actor[0x806]=12;*(u16*)(actor+0x800)=(flags&1?1:0)|(flags&2?16:0);*(f32*)(morf[0]+0xB0)=0.0f;const f32 values[4]={-1,0.999f,1,NAN};*(f32*)(morf[1]+0xB0)=values[phase];new_flag=0;
  s32 result=randomizer_marie_wait((u32)actor);assert(result==(flags?(phase<2):1));
  if(flags){assert(animation_calls==1&&actor[0x81C]==9&&actor[0x81B]==(flags&2?2:1)&&actor[0x806]==(flags&2?255:77));}else assert(!animation_calls&&actor[0x81A]==1);
 }
 reset(1);mutation=1;new_flag=1;night=0;assert(randomizer_marie_wait((u32)actor)==1&&actor[0x81A]==0x44&&event_calls==1&&night_calls==1);
 const uint32_t counters[8]={0x80000000,0xFFFFFFFF,0,202,203,204,0x7FFFFFFF,0xFFFFFFFE};
 for(ended=0;ended<2;ended++)for(unsigned c=0;c<8;c++)for(unsigned colour=0;colour<3;colour++){
  reset(2);const unsigned col=colour==0?0:colour==1?254:255;environment[0][0x10A4]=col;*(uint32_t*)(actor+0x7F4)=counters[c];*(u16*)(play[0]+0x52B8)=0x20;
  randomizer_tingle_rescue_complete((u32)actor);uint32_t next=counters[c]+1u;assert(*(uint32_t*)(actor+0x7F4)==(col==255?counters[c]:next));assert(environment[0][0x10A4]==(col==255?255:(int32_t)next>203?255:col));assert(gfx[0][0x243]==91&&gfx[1][0x243]==92&&on_calls==ended&&delete_calls==ended&&end_calls==1&&*(u16*)(play[0]+0x52B8)==(ended?0x28:0x20));
 }
 reset(2);ended=1;mutation=2;randomizer_tingle_rescue_complete((u32)actor);assert(saveptr==(u32)save[1]&&game==1&&gfxptr==(u32)gfx[1]&&gfx[1][0x243]==92&&*(uint32_t*)(actor+0x7F4)==205&&environment[0][0x10A4]==0&&environment[1][0x10A4]==255);const unsigned expected_trace[8]={1,8,3,1,9,14,10,10};assert(n==8&&!memcmp(trace,expected_trace,sizeof(expected_trace)));
}
'''
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);(root/'npc_replacement_cutscene_gates.h').write_text(header);(root/'test.c').write_text(source)
   subprocess.run([os.environ.get("CC", "cc"),'-std=c11','-O2','-Wall','-Wextra','-Werror',str(root/'test.c'),'-lm','-o',str(root/'test')],check=True);subprocess.run([str(root/'test')],check=True)
if __name__=='__main__':unittest.main()
