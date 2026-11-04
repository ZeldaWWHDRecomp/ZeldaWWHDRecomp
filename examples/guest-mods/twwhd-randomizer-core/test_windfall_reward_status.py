"""Dampa repeat reward timing in complete People message-status consumer."""
from pathlib import Path
import os,subprocess,tempfile,unittest
class Status(unittest.TestCase):
 def test_consumer(self):
  root=Path(__file__).parent
  header=(root/'people_sail_dialogue.h').read_text().replace('#define RPEOPLE(t,a,o) (*(volatile t*)((u32)(a)+(o)))','#define RPEOPLE(t,a,o) (*(volatile t*)address((u32)(a)+(o)))').replace('RPEOPLE(u32,','RPEOPLE(uint32_t,').replace('u32 difference[3]','uint32_t difference[3]').replace('*(u32*)&temporary','*(uint32_t*)&temporary')
  header=header.replace('RPEOPLE(uint32_t,randomizer_people_game(),0x5B3C)','RPEOPLE(u32,randomizer_people_game(),0x5B3C)')
  status=(root/'windfall_reward_status.h').read_text().replace('#define RWS8(a,o) (*(volatile u8*)((a)+(o)))','#define RWS8(a,o) (*(volatile u8*)address((a)+(o)))').replace('#define RWS16(a,o) (*(volatile u16*)((a)+(o)))','#define RWS16(a,o) (*(volatile u16*)address((a)+(o)))').replace('#define RWS32(a,o) (*(volatile unsigned int*)((a)+(o)))','#define RWS32(a,o) (*(volatile unsigned int*)address((a)+(o)))')
  code=r'''


#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef uint16_t u16;typedef int16_t s16;typedef float f32;typedef double f64;
static _Alignas(16) u8 globals[0x500000],actor[0xA00],save[0x1300],play[2][0x6000];
static u8 events[65536],tmp[65536],items[256],regs[65536],tmpregs[65536];
static unsigned game_calls,item_calls,sail_calls,event_writes,picture,color,daiza,wind,mutation,getter_mutation;static u32 selected_game;static f64 distance;
static void* address(u32 a){if(a>=0x10000000&&a<0x10500000)return globals+a-0x10000000;return(void*)a;}
static u32 savepointer=(u32)save;
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_GAME_DATA(a,t) (*(t*)((a)==1?(void*)&savepointer:address(a)))
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#include "people_sail_dialogue.h"
#define RWQ_PTR(a,o) (*(uint32_t*)address((a)+(o)))
#include "windfall_reward_queries.h"
static u32 init_reward_dampa;static u32 pointer_cells[2];
#define RWS_POINTER(a,o) pointer_cells[(o)==0x8A8?0:1]
#include "windfall_reward_status.h"
u32 randomizer_people_game(void){game_calls++;if(game_calls==1)assert(*(uint32_t*)(actor+0x8A8)==0);return (u32)play[getter_mutation&&game_calls>1?1:selected_game];}
s32 randomizer_people_item(u32 item){assert(item<256&&item!=0x77);item_calls++;if(item==0x78){sail_calls++;if(mutation){actor[0x905]=18;actor[0x90F]=1;*(uint32_t*)(actor+0x8A8)=0xDEADBEEF;}}return items[item];}
s32 randomizer_people_event_query(u32 self,u32 flag){assert(flag<65536);if(self==(u32)save+0x644)return events[flag];assert(self==(u32)save+0x1178);return tmp[flag];}
void randomizer_people_on(u32 self,u32 flag){if(flag==0x6A80){assert(*(uint32_t*)(actor+0x8D0)==5);assert((*(uint16_t*)(actor+0x8F2)&0x40)!=0);}
event_writes++;if(self==(u32)save+0x644)events[flag]=1;else{assert(self==(u32)save+0x1178);tmp[flag]=1;}}
void randomizer_people_off(u32 self,u32 flag){if(self==(u32)save+0x644)events[flag]=0;else{assert(self==(u32)save+0x1178);tmp[flag]=0;}}
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
int old_main(void){
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
return 0;
}

static u32 animation;static s32 animation_flag;static unsigned animations,photo_calls;
u32 rwq_animation(u32 self,u32 table,s32 flag){assert(self==(u32)actor);animation=table;animation_flag=flag;++animations;return 1;}
s32 rwq_photo(u32 self,u32 item){assert(self==(u32)actor&&item<256);++photo_calls;return item==0x23;}
void rwq_assert(u32 file,s32 line,u32 condition){assert((file==0x1002047C&&line==0x24CB&&condition==0x10020448)||(file==0x100207EC&&((line==0x1D43&&condition==0x10020800)||(line==0x1E1B&&condition==0x1002080C))));}
int queries_main(void){old_main();unsigned checks=0;
for(unsigned npc=0;npc<256;npc++)for(unsigned night=0;night<2;night++)for(unsigned done=0;done<2;done++){
 reset(npc);actor[0x90F]=night;events[0x6910]=done;actor[0x8FD]=1;actor[0x907]=1;animations=0;
 u32 generic=0x101C4044+4*(npc*2+night);*(uint32_t*)address(generic)=generic;
 randomizer_people_wait_animation((u32)actor);u32 expected=npc==5?0x101C31B4:npc==11&&!night&&done?0x101C31D8:npc==8?0x101C318C:generic;
 assert(animations==1&&animation==expected&&animation_flag==(npc!=5));++checks;
}
for(unsigned done=0;done<256;done++){reset(0);events[0x6904]=done;items[0xEB]=!done;assert(randomizer_people_pompie_reward_done((u32)actor)==(done!=0));++checks;}
for(unsigned npc=0;npc<256;npc++)for(unsigned night=0;night<2;night++)for(unsigned reward=0;reward<2;reward++)for(unsigned item=0;item<256;item++){
 reset(npc);actor[0x90F]=night;events[0x6910]=reward;events[0x2310]=events[0x2102]=events[0x2440]=1;play[0][0x5BBB]=item;photo_calls=0;
 int expected=0;int photo=item==0x23;
 switch(npc){case 2:case 10:expected=photo;break;case 3:case 4:case 6:expected=photo||item==0x1F;break;case 5:expected=item==0x1F;break;case 11:expected=!night&&photo&&!reward;break;case 16:expected=!photo&&!night;break;}
 assert(randomizer_people_xy_check((u32)actor,0)==expected);++checks;
}
for(unsigned done=0;done<2;done++)for(unsigned opposite=0;opposite<2;opposite++){
 reset(6);play[0][0x52B2]=1;play[0][0x5BE6]=5;color=1;events[0x6908]=done;items[0xEF]=opposite;randomizer_people_message((u32)actor);assert(*(uint32_t*)(actor+0x8A8)==(done?0x101C2F7C:0x101C36A8));++checks;
 reset(11);events[0x2340]=events[0x2310]=1;events[0x6910]=done;items[0xF0]=opposite;randomizer_people_message((u32)actor);assert(*(uint32_t*)(actor+0x8A8)==(done?0x101C2FFC:0x101C3B5C));++checks;
 reset(11);actor[0x90F]=1;events[0x2320]=events[0x2308]=events[0x2280]=1;events[0x6910]=done;items[0xF0]=opposite;randomizer_people_message((u32)actor);assert(*(uint32_t*)(actor+0x8A8)==(done?0x101C37B0:0x101C3004));++checks;
}
printf("Windfall queries: %u cases passed\n",checks);return 0;}


static u8 message_manager[0x1000],other_actor[0xA00];static unsigned search_missing,set_messages;static u32 item_count=3,symbol;
u32 rws_search(u32 name,u32 argument,u32 parameter){assert(name==0x1234&&!argument&&!parameter);return search_missing?0:(u32)other_actor;}
void rws_set_message(u32 self,u32 message){assert(self==(u32)actor&&message==0x101C5108);++set_messages;}
s32 rws_random(u32 self,s32 maximum){assert(self==(u32)actor&&maximum==3);return 1;}
u32 rws_item_count(u32 item){assert(item==0x45);return item_count;}
u32 rws_symbol(u32 inventory,u32 bit){assert(inventory==(u32)save+0xD4&&!bit);return symbol;}
static void prepare_status(unsigned code){reset(16);*(u32*)address(0x101F4B5C)=(u32)message_manager;*(u32*)address(0x101C3EC0)=0x1234;memset(message_manager,0,sizeof message_manager);pointer_cells[0]=0x101C5000;pointer_cells[1]=0;*(uint32_t*)address(0x101C5004)=code;*(uint32_t*)address(0x101C5008)=0;*(uint32_t*)address(0x101C500C)=0x9876;*(uint32_t*)address(0x101C5010)=0x7654;search_missing=set_messages=0;}
int main(void){queries_main();unsigned cases=0;unsigned int msg;
for(unsigned item=0;item<256;item++)for(unsigned done=0;done<256;done++){
 prepare_status(10);init_reward_dampa=item;events[0x6A80]=done;items[0xFD]=!done;*(uint16_t*)(actor+0x8F2)=0x8101;msg=0;
 assert(randomizer_people_next_status((u32)actor,(u32)&msg)==16);assert(*(uint32_t*)(actor+0x8D0)==(done?5:item));assert(events[0x6A80]&&event_writes==!done);assert(*(uint16_t*)(actor+0x8F2)==0x8141);assert(!item_calls);++cases;
}
for(unsigned command=0;command<24;command++)for(unsigned select=0;select<2;select++)for(unsigned done=0;done<2;done++){
 prepare_status(command);*(uint32_t*)(message_manager+0x948)=select;events[0x6A80]=done;tmpregs[0xFF03]=2;init_reward_dampa=7;msg=0;
 for(unsigned a=0x101C2F8C;a<0x101C4000;a+=4)if(a!=0x101C3EC0)*(uint32_t*)address(a)=a;
 /* Per-table flags/items and explain pointers retain real widths. */
 *(uint16_t*)address(0x101C305C)=0x4444;*(uint32_t*)address(0x101C3870)=12;
 *(uint32_t*)address(0x101C3CAC+8)=0x101C4000;*(uint32_t*)address(0x101C4000)=0x1111;
 u16 status=randomizer_people_next_status((u32)actor,(u32)&msg);assert(status==15||status==16);if(command==10)assert(status==16);++cases;
}
prepare_status(0);msg=0x3024;assert(randomizer_people_next_status((u32)actor,(u32)&msg)==16&&events[0x2220]);++cases;
for(unsigned missing=0;missing<2;missing++)for(unsigned select=0;select<2;select++)for(unsigned insufficient=0;insufficient<2;insufficient++){
 prepare_status(0);search_missing=missing;*(uint32_t*)(message_manager+0x948)=select;*(uint16_t*)(save+0x24)=insufficient?4:100;*(int16_t*)(play[0]+0x5BA4)=50;*(uint32_t*)(play[0]+0x5B48)=0x80000000;msg=0x358B;
 assert(randomizer_people_next_status((u32)actor,(u32)&msg)==16);if(!missing)assert(*(uint32_t*)(other_actor+0x7C0)==(!select?0x358C:insufficient?0x359B:0x358D));++cases;
}
prepare_status(0);pointer_cells[1]=0x101C5100;*(uint32_t*)address(0x101C5108)=1;msg=0;assert(randomizer_people_next_status((u32)actor,(u32)&msg)==15&&set_messages==1);++cases;
printf("Windfall status: %u cases passed\n",cases);return 0;}

'''
  with tempfile.TemporaryDirectory() as d:
   dest=Path(d);(dest/'people_sail_dialogue.h').write_text(header);(dest/'windfall_reward_status.h').write_text(status);(dest/'test.c').write_text(code)
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(root),str(dest/'test.c'),'-lm','-o',str(dest/'test')],check=True)
   subprocess.run([str(dest/'test')],check=True)
if __name__=='__main__':unittest.main()
