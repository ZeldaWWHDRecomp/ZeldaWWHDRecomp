from pathlib import Path
import re,subprocess,tempfile,unittest

class PeopleSailDialogue(unittest.TestCase):
 def test_shared_prefix_live_tables_and_preserved_ownership_queries(self):
  header=Path(__file__).with_name('people_sail_dialogue.h').read_text()
  header=header.replace('#define RPEOPLE(t,a,o) (*(volatile t*)((u32)(a)+(o)))','#define RPEOPLE(t,a,o) (*(volatile t*)address((u32)(a)+(o)))').replace('RPEOPLE(u32,','RPEOPLE(uint32_t,')
  header=header.replace('RPEOPLE(uint32_t,randomizer_people_game(),0x5B3C)','RPEOPLE(u32,randomizer_people_game(),0x5B3C)')
  source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <math.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef uint16_t u16;typedef int16_t s16;typedef float f32;typedef double f64;
static _Alignas(16) u8 globals[0x500000],actor[0xA00],save[0x1300],play[2][0x6000];
static u8 events[65536],tmp[65536],items[256],regs[65536],tmpregs[65536];
static unsigned game_calls,item_calls,sail_calls,event_writes,picture,color,daiza,wind,mutation,getter_mutation;static u32 selected_game;static f64 distance;
static void* address(u32 a){if(a>=0x10000000&&a<0x10500000)return globals+a-0x10000000;return(void*)a;}
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_GAME_DATA(a,t) ((u32)save)
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#include "people_sail_dialogue.h"
u32 randomizer_people_game(void){game_calls++;if(game_calls==1)assert(*(uint32_t*)(actor+0x8A8)==0);return (u32)play[getter_mutation&&game_calls>1?1:selected_game];}
s32 randomizer_people_item(u32 item){assert(item<256&&item!=0x77);item_calls++;if(item==0x78){sail_calls++;if(mutation){actor[0x905]=18;actor[0x90F]=1;*(uint32_t*)(actor+0x8A8)=0xDEADBEEF;}}return items[item];}
s32 randomizer_people_event_query(u32 self,u32 flag){assert(flag<65536);if(self==(u32)save+0x644)return events[flag];assert(self==(u32)save+0x1178);return tmp[flag];}
void randomizer_people_on(u32 self,u32 flag){event_writes++;if(self==(u32)save+0x644)events[flag]=1;else{assert(self==(u32)save+0x1178);tmp[flag]=1;}}
void randomizer_people_off(u32 self,u32 flag){assert(self==(u32)save+0x644);events[flag]=0;}
u8 randomizer_people_reg_query(u32 self,u32 reg){return self==(u32)save+0x644?regs[reg]:tmpregs[reg];}
void randomizer_people_reg_write(u32 self,u32 reg,u8 value){if(self==(u32)save+0x644)regs[reg]=value;else{assert(self==(u32)save+0x1178);tmpregs[reg]=value;}}
s32 randomizer_people_color(u32 self){assert(self==(u32)actor);return color;}
s32 randomizer_people_all(u32 self){assert(self==(u32)actor);return 0;}
s32 randomizer_people_one(u32 self){assert(self==(u32)actor);return 0;}
s32 randomizer_people_wind(u32 self){assert(self==(u32)actor);return wind;}
s32 randomizer_people_daiza(u32 self){assert(self==(u32)actor);return daiza;}
s16 randomizer_people_pig_timer(u32 self){assert(self==(u32)actor);return 0;}
s32 randomizer_people_pig_ok(u32 self){assert(self==(u32)actor);return 0;}
void randomizer_people_distance_angle(u32 a,u32 b,u32 d,u32 angle){(void)a;(void)b;assert(!angle);*(f32*)d=10000;}
void randomizer_people_subtract(u32 a,u32 out,u32 b){for(unsigned i=0;i<3;i++)((f32*)out)[i]=((f32*)a)[i]-((f32*)b)[i];}
f64 randomizer_people_square(u32 vector){const f32*v=(f32*)vector;assert(v[1]==0);return (f64)v[0]*v[0]+(f64)v[2]*v[2];}
f64 randomizer_people_sqrt(f64 square){(void)square;return distance;}
void randomizer_people_assert(u32 file,s32 line,u32 text){assert(file==0x10020874&&(line==0x2113||line==0x2151)&&text);}
static void reset(unsigned npc){memset(actor,0,sizeof(actor));memset(save,0,sizeof(save));memset(play,0,sizeof(play));memset(events,0,sizeof(events));memset(tmp,0,sizeof(tmp));memset(items,0,sizeof(items));memset(regs,0,sizeof(regs));memset(tmpregs,0,sizeof(tmpregs));game_calls=item_calls=sail_calls=event_writes=selected_game=mutation=getter_mutation=picture=color=daiza=wind=0;actor[0x905]=npc;distance=0;}
static u32 table_word(u32 a){return *(uint32_t*)address(a);}
int main(void){
 /* A synthetic live table returns its address; no copied game message strings/data. */
 for(u32 a=0x101C2000;a<0x101C4000;a+=4)*(uint32_t*)address(a)=(uint32_t)a;
 const u32 daiza_tables[8]={0x101C3054,0x101C37D0,0x101C37F0,0x101C3BF8,0x101C3810,0x101C3830,0x101C3C04,0x101C3C10};
 const u32 wind_tables[8]={0x101C3880,0x101C38A0,0x101C38B0,0x101C38D0,0x101C38E0,0x101C38F0,0x101C3900,0x101C3910};
 for(unsigned i=0;i<8;i++){*(uint32_t*)address(0x101C3850+i*4)=daiza_tables[i];*(uint32_t*)address(0x101C3920+i*4)=wind_tables[i];}
 const unsigned targets[3]={11,14,15};const unsigned first_flags[3]={0x2340,0x2301,0x2480};
 const u32 first_tables[3]={0x101C3790,0x101C3BEC,0x101C3C1C};
 /* First normal-sail call retained for every ownership byte, Swift-Sail call absent.
    Actual expected table addresses are verified from public static glue below. */
 for(unsigned t=0;t<3;t++)for(unsigned sail=0;sail<256;sail++)for(unsigned first=0;first<2;first++)for(unsigned night=0;night<2;night++){
  reset(targets[t]);items[0x78]=sail;items[0x77]=!sail;events[first_flags[t]]=first;actor[0x90F]=night;daiza=3;wind=5;
  u32 message=randomizer_people_message((u32)actor);assert(message!=0);assert(sail_calls==!night);
  if(!night&&!first){assert(events[first_flags[t]]==1&&event_writes==1);assert(*(uint32_t*)(actor+0x8A8)==first_tables[t]);}
  if(!night&&first&&t==1)assert(*(uint32_t*)(actor+0x8A8)==daiza_tables[3]);
  if(!night&&first&&t==2)assert(*(uint32_t*)(actor+0x8A8)==wind_tables[5]);
 }
 for(unsigned t=0;t<3;t++){
  reset(targets[t]);mutation=1;assert(randomizer_people_message((u32)actor)==table_word(first_tables[t]));assert(actor[0x905]==18&&actor[0x90F]==1&&sail_calls==1);
  reset(targets[t]);events[0xA02]=1;assert(randomizer_people_message((u32)actor)==(t==0?0x347B:t==1?0x30E4:0x321E));assert(sail_calls==0&&*(uint32_t*)(actor+0x8A8)==0);
  reset(targets[t]);events[0xA02]=1;items[0x69]=1;assert(randomizer_people_message((u32)actor)==table_word(first_tables[t])&&sail_calls==1);
 }
 /* Other NPCs and unsupported IDs still execute the complete shared prefix. */
 for(unsigned npc=0;npc<256;npc++){reset(npc);(void)randomizer_people_message((u32)actor);assert(game_calls>=2);if(npc>18)assert(*(uint32_t*)(actor+0x8A8)==0&&item_calls==0);}
 /* Native f1 remains double: the distance just below200 must stay in range. */
 for(unsigned npc=17;npc<19;npc++){
  reset(npc);distance=nextafter(200.0,-INFINITY);u32 near=randomizer_people_message((u32)actor);assert(events[npc==17?0x2880:0x2B08]);
  reset(npc);distance=200.0;u32 far=randomizer_people_message((u32)actor);assert(!events[npc==17?0x2880:0x2B08]&&near!=far);
 }
 /* First getter's photo capture and second getter's talk state retain their order. */
 reset(11);play[0][0x52B2]=1;play[0][0x5BE6]=0;assert(randomizer_people_message((u32)actor)!=0&&sail_calls==0);
 reset(11);getter_mutation=1;play[0][0x52B2]=1;play[1][0x52B0]=1;assert(randomizer_people_message((u32)actor)!=0&&sail_calls==0);
 reset(11);getter_mutation=1;play[1][0x52B0]=1;assert(randomizer_people_message((u32)actor)==0&&sail_calls==0);
}
'''
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory);(root/'people_sail_dialogue.h').write_text(header);(root/'test.c').write_text(source)
   subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',str(root/'test.c'),'-lm','-o',str(root/'test')],check=True)
   subprocess.run([str(root/'test')],check=True)
if __name__=='__main__':unittest.main()
