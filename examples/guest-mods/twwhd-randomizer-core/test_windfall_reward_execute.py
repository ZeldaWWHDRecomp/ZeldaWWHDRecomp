"""Complete People execution and independent Kamo reward gate."""
from pathlib import Path
import os,subprocess,tempfile,unittest
class Execute(unittest.TestCase):
 def test_consumer(self):
  root=Path(__file__).parent
  header=(root/'people_sail_dialogue.h').read_text().replace('#define RPEOPLE(t,a,o) (*(volatile t*)((u32)(a)+(o)))','#define RPEOPLE(t,a,o) (*(volatile t*)address((u32)(a)+(o)))').replace('RPEOPLE(u32,','RPEOPLE(uint32_t,').replace('u32 difference[3]','uint32_t difference[3]').replace('*(u32*)&temporary','*(uint32_t*)&temporary')
  header=header.replace('RPEOPLE(uint32_t,randomizer_people_game(),0x5B3C)','RPEOPLE(u32,randomizer_people_game(),0x5B3C)')
  code=r'''



#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef uint16_t u16;typedef int16_t s16;typedef float f32;typedef double f64;
static _Alignas(16) u8 globals[0x500000],actor[0xC00],save[0x1300],play[2][0x6000];
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
#define RWP_LITERAL(a) ((u32)"Ocmera")
static void phase_terminate(u32);
#define RWP_PTR(a,o) ((u32)phase_terminate)
#include "windfall_reward_phases.h"
#define RWS16(a,o) (*(volatile u16*)address((a)+(o)))
static u32 exec_pointer(u32);
#define RWE_POINTER(a,o) exec_pointer((a)+(o))
static void dispatch(u32,u32);
#define RWE_CALL_MOVE(t,a) dispatch(t,a)
s32 rws_random(u32,s32);
#include "windfall_reward_execute.h"
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

static u32 animation;static s32 animation_flag;static unsigned animations;
u32 rwq_animation(u32 self,u32 table,s32 flag){assert(self==(u32)actor);animation=table;animation_flag=flag;++animations;return 1;}
s32 rwq_photo(u32 self,u32 item){assert(self==(u32)actor&&item<256);return item==0x23;}
void rwq_assert(u32 file,s32 line,u32 condition){assert(file==0x1002047C&&line==0x24CB&&condition==0x10020448);}
int queries_main(void){old_main();unsigned checks=0;
for(unsigned npc=0;npc<256;npc++)for(unsigned night=0;night<2;night++)for(unsigned done=0;done<2;done++){
 reset(npc);actor[0x90F]=night;events[0x6910]=done;actor[0x8FD]=1;actor[0x907]=1;animations=0;
 u32 generic=0x101C4044+4*(npc*2+night);*(uint32_t*)address(generic)=generic;
 randomizer_people_wait_animation((u32)actor);u32 expected=npc==5?0x101C31B4:npc==11&&!night&&done?0x101C31D8:npc==8?0x101C318C:generic;
 assert(animations==1&&animation==expected&&animation_flag==(npc!=5));++checks;
}
for(unsigned done=0;done<256;done++){reset(0);events[0x6904]=done;items[0xEB]=!done;assert(randomizer_people_pompie_reward_done((u32)actor)==(done!=0));++checks;}
for(unsigned npc=0;npc<256;npc++)for(unsigned night=0;night<2;night++)for(unsigned reward=0;reward<2;reward++)for(unsigned item=0;item<256;item++){
 reset(npc);actor[0x90F]=night;events[0x6910]=reward;events[0x2310]=events[0x2102]=events[0x2440]=1;play[0][0x5BBB]=item;
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


static u32 argument;static s32 day;static unsigned constructor_calls,argument_calls,terminate_calls,constructor_mutation;
u32 rwp_people_ctor(u32 self){assert(self==(u32)actor);++constructor_calls;if(constructor_mutation)*(uint32_t*)(actor+0x2E4)=0x100;return self;}
u32 rwp_photo_ctor(u32 self){return rwp_people_ctor(self);}
u32 rwp_people_argument(u32 self){assert(self==(u32)actor);++argument_calls;return argument;}
u32 rwp_photo_argument(u32 self){return rwp_people_argument(self);}
s32 rwp_day(void){return day;}
static void phase_terminate(u32 value){rwp_string* s=(rwp_string*)value;assert(s->vtable==0x10020114);++terminate_calls;}
int phases_main(void){queries_main();unsigned cases=0;const s16 points[]={-32768,-1,0,1,2,254,255,32767};
for(unsigned photo=0;photo<2;photo++)for(argument=0;argument<256;argument++)for(unsigned done=0;done<2;done++)for(unsigned point=0;point<8;point++)for(unsigned initialized=0;initialized<2;initialized++){
 reset(6);constructor_calls=argument_calls=terminate_calls=0;constructor_mutation=!initialized;*(uint32_t*)(actor+0x2E4)=initialized?8:1;strcpy((char*)play[0]+0x5134,"Ocmera");*(s16*)(play[0]+0x513C)=points[point];events[0x6904]=done;items[0xEB]=!done;
 s32 actual=photo?randomizer_photo_first_phase((u32)actor):randomizer_people_first_phase((u32)actor);s32 expected=2;
 if(photo){if(argument!=255){if(argument==0){if(!done&&points[point]!=0)expected=3;}else if(done||points[point]!=(s32)argument)expected=3;}}
 else if(done||points[point]!=(s32)argument)expected=3;
 assert(actual==expected&&constructor_calls==!initialized&&argument_calls==1);assert(*(uint32_t*)(actor+0x2E4)==(initialized?8:0x108));assert(!item_calls);
 if(photo){assert(actor[0xB36]==(expected==2));assert(actor[0xB35]==(expected==2&&argument!=0&&argument!=255?4:0));}
 else{assert(terminate_calls==3&&actor[0x904]==(expected==2));assert((*(uint32_t*)(actor+0x8CC)&0x40)==(expected==2?0x40:0));}++cases;
}
for(unsigned npc=0;npc<256;npc++)for(argument=0;argument<256;argument++)for(day=-2;day<9;day++){
 reset(npc);constructor_calls=argument_calls=terminate_calls=0;constructor_mutation=0;strcpy((char*)play[0]+0x5134,"other");regs[0xB907]=6;s32 expected=2;
 if((npc==8||npc==10)&&argument!=255&&((argument<<1)&0xFE)!=6)expected=3;
 if(npc==17||npc==18){if(argument==0&&(day==5||day==6))expected=3;if(argument==1&&day!=5&&day!=6)expected=3;}
 assert(randomizer_people_first_phase((u32)actor)==expected);assert(actor[0x904]==(expected==2));assert(terminate_calls==(npc==6?3:0));++cases;
}
printf("Windfall phases: %u cases passed\n",cases);return 0;}


static u8 npc_data[0x100],morf[0x100];static u32 last_target,last_object;static unsigned move_calls,event_calls,orders,collision_calls,matrix_calls,look_calls;static u8 random_value;static unsigned event_mutation;
static u32 exec_pointer(u32 key){if(key==(u32)actor+0x8A0)return(u32)npc_data;if(key==(u32)actor+0x44C)return(u32)morf;if(key==(u32)actor+0xB4)return 0x101C6000;return*(uint32_t*)address(key);}
static void dispatch(u32 target,u32 self){last_target=target;last_object=self;++move_calls;}
s32 rws_random(u32 self,s32 maximum){assert(self==(u32)actor&&(maximum==4||maximum==8));return random_value;}
void rwe_warp(u32 self){assert(self==(u32)actor&&actor[0x91A]==255);}
void rwe_attention(u32 self){assert(self==(u32)actor);}
void rwe_check_order(u32 self){assert(self==(u32)actor);}
void rwe_event_move(u32 self){assert(self==(u32)actor);++event_calls;}
void rwe_event_order(u32 self){assert(self==(u32)actor);++orders;if(event_mutation){actor[0x905]=11;actor[0x909]=3;}}
void rwe_texture(u32 self){assert(self==(u32)actor);}
void rwe_animation(u32 self){assert(self==(u32)actor);}
void rwe_look(u32 self){assert(self==(u32)actor);++look_calls;}
void rwe_matrix(u32 self){assert(self==(u32)actor);++matrix_calls;}
s32 rwe_chase(u32 value,f32 target,f32 step){assert(value==(u32)actor+0x370&&step==0.1f);*(f32*)value=target;return 1;}
void rwe_move(u32 self,u32 vector){assert(self==(u32)actor&&vector==(u32)actor+0x654);}
void rwe_correct(u32 acch,u32 background){assert(acch==(u32)actor+0x450&&background==(u32)play[0]+0x12A0);}
void rwe_collision(u32 self,u32 cylinder,u32 center,f32 radius,f32 height){assert(self==(u32)actor&&cylinder==(u32)actor+0x690&&center&&radius==2&&height==3);++collision_calls;}
void rwe_rotate(u32 matrix,s32 angle){assert(matrix==(u32)address(0x1048D0CC));(void)angle;}
void rwe_transform(u32 matrix,u32 source,u32 target){assert(matrix==(u32)address(0x1048D0CC)&&source==target);}
void rwe_add(u32 a,u32 b,u32 target){assert(a==target&&b==(u32)actor+0x314);for(unsigned i=0;i<3;i++)((f32*)target)[i]+=((f32*)b)[i];}
static void exec_reset(unsigned npc){reset(npc);move_calls=event_calls=orders=collision_calls=matrix_calls=look_calls=animations=0;event_mutation=0;random_value=1;argument=1;*(f32*)(actor+0x8C0)=2;*(f32*)(npc_data+0x40)=3;*(f32*)(npc_data+0x44)=2;*(f32*)(npc_data+0x24)=8;*(s16*)address(0x101C4BE4)=0;*(s16*)address(0x101C4BE6)=-1;*(uint32_t*)address(0x101C4BE8)=0x3000;}
int main(void){phases_main();unsigned cases=0;
for(unsigned npc=0;npc<256;npc++)for(unsigned night=0;night<2;night++)for(unsigned done=0;done<2;done++)for(unsigned anim=0;anim<4;anim++){
 exec_reset(npc);actor[0x90F]=night;events[0x6910]=done;items[0xF0]=!done;actor[0x909]=anim;*(uint16_t*)(actor+0xF8)=0;
 for(unsigned base=0x101C3460;base<=0x101C34D0;base+=4)*(uint32_t*)address(base)=base;
 assert(randomizer_people_execute((u32)actor)==0);assert(move_calls==1&&!event_calls&&orders==1&&collision_calls==1&&matrix_calls==1&&look_calls==1);
 unsigned expected_anim=anim&&((!night&&(npc==3||npc==4||(npc==11&&!done)))||(night&&(npc==14||npc==16)));
 assert(animations==expected_anim);if(npc==11&&!night)assert(item_calls==0);++cases;
}
for(unsigned running=0;running<2;running++)for(unsigned command=0;command<3;command++)for(unsigned requested=0;requested<2;requested++)for(unsigned virt=0;virt<2;virt++){
 exec_reset(0);play[0][0x5292]=running;*(uint16_t*)(actor+0xF8)=command;actor[0x910]=requested;
 *(s16*)address(0x101C4BE4)=0;*(s16*)address(0x101C4BE6)=virt?2:-1;if(virt)*(s16*)address(0x101C4BEA)=0xB4;*(uint32_t*)address(0x101C6014)=0x4000;
 assert(randomizer_people_execute((u32)actor)==0);unsigned dispatched=!running||(command==1&&requested);assert(move_calls==dispatched&&event_calls==!dispatched);if(dispatched)assert(last_object==(u32)actor&&last_target==(virt?0x4000:0x3000));++cases;
}
const f64 distances[]={180.4,nextafter(180.0,-INFINITY),NAN,INFINITY,-INFINITY};const unsigned weights[]={81,80,0,130,80};for(unsigned mode=0;mode<5;mode++){exec_reset(17);argument=0;distance=distances[mode];/* 180.4 rejects premature f32 rounding of the wide sqrt return. */assert(randomizer_people_execute((u32)actor)==0&&actor[0x668]==weights[mode]);++cases;}
exec_reset(0);event_mutation=1;events[0x6910]=0;assert(randomizer_people_execute((u32)actor)==0&&animations==1);++cases;
printf("Windfall execute: %u cases passed\n",cases);return 0;}

'''
  with tempfile.TemporaryDirectory() as d:
   dest=Path(d);(dest/'people_sail_dialogue.h').write_text(header);(dest/'test.c').write_text(code)
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(root),str(dest/'test.c'),'-lm','-o',str(dest/'test')],check=True)
   subprocess.run([str(dest/'test')],check=True)
if __name__=='__main__':unittest.main()
