"""Complete ChuChu hit consumer and retained electric collision after hookshot."""
from pathlib import Path
import os,subprocess,tempfile,unittest
class BlueChuChu(unittest.TestCase):
 def test_consumer(self):
  code=r'''

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef int8_t s8;typedef int16_t s16;typedef uint16_t u16;typedef float f32;typedef double f64;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 actor[0x1000],player[0x400],other[0x400],play[0x6100],regs[0x2000],object[0x100];
static void* data(u32 a){if(a==0x1047B610)return regs;if(a==0x1047B688)return regs+0x78;assert(0);return 0;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static struct{u32 key,value;}cells[32];static unsigned nc;
static u32* cell(u32 key){for(unsigned i=0;i<nc;i++)if(cells[i].key==key)return&cells[i].value;assert(nc<32);cells[nc].key=key;cells[nc].value=0;return&cells[nc++].value;}
#define RBCPTR(a,o) (*cell((a)+(o)))
static u32 wwhd_dComIfG_getGameInfo(void);
#include "bluechu_hookshot_shock.h"
static unsigned hit_present,object_present,sound_mutation;static u32 attacker;static unsigned calls,status_calls,hit_calls,objects,actors,attacks,particles,sounds,monsters,ended,fires;static unsigned char trace[256];static unsigned nt;
static void log_call(unsigned c){assert(nt<256);trace[nt++]=c;}
static u32 wwhd_dComIfG_getGameInfo(void){++calls;log_call(1);return(u32)play;}
s16 rbc_atan(f64 y,f64 x){(void)y;(void)x;log_call(2);return 123;}
void rbc_fire_remove(u32 fire){assert(fire==(u32)actor+0xCDC);++fires;log_call(3);}
void rbc_electric_end(u32 a){assert(a==(u32)actor);++ended;log_call(4);}
u32 rbc_hit_actor(u32 target_info){assert(target_info==(u32)actor+0x858);++actors;log_call(5);return attacker;}
void rbc_status_move(u32 status){assert(status==(u32)actor+0x7A4);++status_calls;log_call(6);}
s32 rbc_hit(u32 cylinder){assert(cylinder==(u32)actor+0x7C4);++hit_calls;log_call(7);return hit_present;}
u32 rbc_hit_object(u32 cylinder){assert(cylinder==(u32)actor+0x7C4);++objects;log_call(8);return object_present?(u32)object:0;}
u32 rbc_attack_check(u32 a,u32 info){assert(a==(u32)actor&&RBCPTR(info,0)==(u32)object&&!RBCPTR(info,0x14));++attacks;log_call(9);RBCS8(a,0x3A1)=-5;RBC8(a,0x3A8)=77;return attacker;}
s32 rbc_reverb(s32 room){assert(room==-4);log_call(10);return 9;}
u32 rbc_particle_native(u32 controller,u32 group,u32 id,u32 position,u32 angle,u32 scale,u32 alpha,u32 callback,s32 room,u32 color,u32 color2,u32 scale2){assert(controller==42&&!group&&alpha==255&&!callback&&room==-1&&!color&&!color2&&!scale2);assert(RBC32(position,0)==0x7FC00123u&&RBC32(position,4)==0x80000000u&&RBC32(position,8)==0x40400000u);if(id==0x10)assert(!angle&&!scale);else{assert(angle==(u32)player+0x328);if(id==0xF)assert(RBCF(scale,0)==2&&RBCF(scale,4)==2&&RBCF(scale,8)==2);else assert(id==0xD&&!scale);}++particles;log_call(11);return 0;}
void rbc_sound_native(u32 id,u32 position,u32 parameter,s32 reverb){(void)id;(void)parameter;assert(position==(u32)actor+0x37C&&reverb==9);if(sound_mutation)RBC32((u32)actor,0x7C4)=0x13579BDFu;++sounds;log_call(12);}
void rbc_monster_native(u32 id,u32 position,u32 process,u32 parameter,s32 reverb){assert(id==0x4863&&position==(u32)actor+0x37C&&process==7&&!parameter&&reverb==9);++monsters;log_call(13);}
static void reset(void){memset(actor,0,sizeof actor);memset(player,0,sizeof player);memset(other,0,sizeof other);memset(play,0,sizeof play);memset(regs,0,sizeof regs);memset(object,0,sizeof object);nc=0;nt=0;calls=status_calls=hit_calls=objects=actors=attacks=particles=sounds=monsters=ended=fires=0;hit_present=object_present=1;sound_mutation=0;attacker=(u32)player;RBCPTR((u32)play,0x5B2C)=(u32)player;RBCPTR((u32)play,0x5AB0)=42;RBCS8((u32)actor,0x326)=-4;RBCS8((u32)actor,0x3A1)=4;RBC32((u32)actor,4)=7;RBC32((u32)actor,0x890)=0x7FC00123u;RBC32((u32)actor,0x894)=0x80000000u;RBC32((u32)actor,0x898)=0x40400000u;}
int main(void){unsigned cases=0;
/* Full hookshot consumer matrix: AtSet is unchanged, even after a sound callback. */
for(unsigned color=0;color<5;color++)for(unsigned action=0;action<8;action++)for(unsigned state=0;state<256;state++)for(unsigned bit=0;bit<2;bit++){
 reset();RBC32((u32)object,0x10)=0x8000;RBC8((u32)actor,0x3F9)=color;RBC8((u32)actor,0x3FC)=action;RBC8((u32)actor,0x3FD)=state;RBC32((u32)actor,0x7C4)=0xA5A5A5A4u|bit;RBCF((u32)actor,0x434)=2;RBCF((u32)actor,0x438)=3;RBC16((u32)actor,0x450)=4;RBCA((u32)actor,0x418).x=5;RBCA((u32)actor,0x418).y=6;RBCA((u32)actor,0x418).z=7;RBCF((u32)actor,0x33C)=8;RBCF((u32)actor,0x340)=9;RBCF((u32)actor,0x344)=10;
 s32 result=randomizer_bluechu_body_hit((u32)actor);assert(RBC32((u32)actor,0x7C4)==(0xA5A5A5A4u|bit)&&status_calls==1&&objects==1&&actors==1&&!attacks);if(state==0x15){assert(result==0&&RBC8((u32)actor,0x3FC)==action&&RBCF((u32)actor,0x33C)==0&&RBCF((u32)actor,0x340)==0&&RBCF((u32)actor,0x344)==0&&RBCF((u32)actor,0x374)==-3);}else if(action!=3){assert(result==1&&RBC8((u32)actor,0x3FC)==0&&RBC8((u32)actor,0x3FD)==10&&!RBCF((u32)actor,0x434)&&!RBCF((u32)actor,0x438)&&!RBC16((u32)actor,0x450)&&!RBCA((u32)actor,0x418).x&&!RBCA((u32)actor,0x418).y&&!RBCA((u32)actor,0x418).z);}else assert(result==1&&RBC8((u32)actor,0x3FC)==3&&RBC8((u32)actor,0x3FD)==0x28);++cases;
}
reset();RBC32((u32)object,0x10)=0x8000;RBC8((u32)actor,0x3F9)=3;sound_mutation=1;assert(randomizer_bluechu_body_hit((u32)actor)==1&&RBC32((u32)actor,0x7C4)==0x13579BDFu&&sounds==1);++cases;
/* Missing hit/object still performs target-actor lookup in native order. */
reset();hit_present=0;assert(!randomizer_bluechu_body_hit((u32)actor)&&status_calls==1&&!objects&&!actors&&nt==3&&trace[0]==1&&trace[1]==6&&trace[2]==7);++cases;
reset();object_present=0;assert(!randomizer_bluechu_body_hit((u32)actor)&&objects==1&&actors==1&&nt==5&&trace[3]==8&&trace[4]==5);++cases;
/* Every attack family and player cut value, with ordinary and electric colors. */
const unsigned types[]={0,2,0x40,0x80,0x100,0x200,0x400,0x800,0x1000,0x2000,0x4000,0x8000,0x10000,0x20000,0x40000,0x80000,0x100000,0x200000,0x800000,0x1000000,0x4000000,0x8000000,0x10000000};
for(unsigned type=0;type<sizeof(types)/sizeof(types[0]);type++)for(unsigned color=0;color<5;color++)for(unsigned cut=0;cut<256;cut++){
 reset();RBC32((u32)object,0x10)=types[type];RBC8((u32)actor,0x3F9)=color;RBC8((u32)player,0x3AC)=cut;RBCS8((u32)actor,0x3A9)=1;RBC8((u32)actor,0x3A8)=5;RBC32((u32)actor,0x7C4)=0xABCDEF01u;(void)randomizer_bluechu_body_hit((u32)actor);assert(RBC32((u32)actor,0x7C4)==0xABCDEF01u);if(types[type]==0x8000000){assert(attacks==1&&RBCS8((u32)actor,0x3A1)==4&&RBC8((u32)actor,0x3A8)==(color==2?5:77));}if(types[type]==0x100000)assert(RBC8((u32)actor,0x92A)==1&&RBCF((u32)actor,0xAD0)==0.5f&&RBCF((u32)actor,0x92C)==80&&ended==1);++cases;
}
reset();RBC8((u32)actor,0x40C)=1;attacker=(u32)other;assert(randomizer_bluechu_body_hit((u32)actor)==1&&RBC8((u32)actor,0x40C)==2&&RBCS8((u32)actor,0x3A1)==0&&particles==2&&!attacks);++cases;
printf("Blue ChuChu: %u complete hit-consumer cases passed\n",cases);return 0;}

'''
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'test.c').write_text(code)
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
