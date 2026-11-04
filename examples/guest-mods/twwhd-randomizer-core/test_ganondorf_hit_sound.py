"""Complete collision-hit utility with Ganondorf's ordinary sound exception."""
import os
from pathlib import Path
import subprocess,tempfile,unittest
class GanondorfHitSound(unittest.TestCase):
 def test_collision_consumer(self):
  code=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int16_t s16;typedef uint16_t u16;typedef int8_t s8;typedef uint8_t u8;typedef float f32;typedef double f64;
static unsigned char arena[0x20000];static const u32 target=0x30000000,attacker=0x30002000,info=0x30003000,object=0x30004000,player=0x30008000,game=0x30010000;
static void* mem(u32 a){assert(a>=target&&a<target+sizeof arena);return arena+a-target;}
#define W(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)mem(a))
#define R_HIT(t,a) W(t,a)
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static f32 threshold=50,particle_scale=2;static s16 hit_adjust=2,critical_adjust=3,death_adjust=4;static u8 pause;
static void* data(u32 a){switch(a){case 0x1004B460:return &threshold;case 0x1004B468:return &particle_scale;case 0x1047B692:return &hit_adjust;case 0x1047B694:return &critical_adjust;case 0x1047B696:return &death_adjust;case 0x101EACB7:return &pause;}assert(0);return 0;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static u32 wwhd_dComIfG_getGameInfo(void);static f64 randomizer_hyrule_square_magnitude(const f32*);static f64 randomizer_hyrule_sqrt(f64);static s32 randomizer_arrow_atan(f64,f64);static s32 randomizer_boko_reverb(s32);static void randomizer_boko_sound(u32,u32,u32,s32);static u32 randomizer_arrow_particle(u32,u32,u32,u32,u32,u32,u32,u32,s32,u32,u32,u32);
#include "ganondorf_hit_sound.h"
static unsigned sounds,particles,steals,music,powers,enemy_calls,normal_calls,critical_calls,mutate,missing;static u32 sound_id,sound_arg;static f64 magnitude;static f64 angle_x,angle_z;
static u32 wwhd_dComIfG_getGameInfo(void){return game;}
static f64 randomizer_hyrule_square_magnitude(const f32* p){assert((u32)p==attacker+0x33C);return 25;}
static f64 randomizer_hyrule_sqrt(f64 d){assert(d==25);return magnitude;}
static s32 randomizer_arrow_atan(f64 x,f64 z){angle_x=x;angle_z=z;return 0x8123;}
u32 randomizer_hit_power(u32 i){assert(i==info&&W(u32,i+0x18)==0);powers++;return missing?0:attacker;}
void randomizer_hit_enemy_damage(u32 a){assert(a==attacker);enemy_calls++;}
void randomizer_hit_steal(u32 pos,u32 table,s32 room,u32 zero,u32 bit){assert(pos==target+0x314&&table==7&&room==-2&&!zero&&bit==4);steals++;}
u32 randomizer_hit_object_info(u32 o){assert(o==object);critical_calls++;return object;}
u32 randomizer_hit_sound_id(u32 o){assert(o==object);normal_calls++;return 0x1111;}
static s32 randomizer_boko_reverb(s32 room){assert(room==-2);return -7;}
static void randomizer_boko_sound(u32 id,u32 pos,u32 arg,s32 reverb){assert(pos==target+0x37C&&reverb==-7);sounds++;sound_id=id;sound_arg=arg;if(mutate==1)W(s8,target+0x3A1)=50;}
s32 randomizer_hit_player_angle(u32 a,u32 p){assert(a==target&&p==player);if(mutate==2)W(u32,info+0x14)=target+0x31C;return 0x9234;}
void randomizer_hit_music(u32 id){assert(id==0);music++;if(mutate==3)W(u32,info+4)=target;}
static u32 randomizer_arrow_particle(u32 c,u32 group,u32 id,u32 pos,u32 rotation,u32 scale,u32 alpha,u32 cb,s32 room,u32 primary,u32 env,u32 extra){assert(c==0xABCDEF&&!group&&alpha==255&&!cb&&room==-1&&!primary&&!env&&!extra);if(!particles){assert(id==0x10&&pos==target+0x314&&!rotation&&!scale);}else{assert(id==0xD&&pos==(mutate==2?target+0x31C:target+0x314));assert(((s16*)rotation)[0]==0&&((s16*)rotation)[1]==(s16)0x9234&&((s16*)rotation)[2]==0);assert(((f32*)scale)[0]==2&&((f32*)scale)[1]==2&&((f32*)scale)[2]==2);}particles++;return 0;}
static void reset(void){memset(arena,0,sizeof arena);W(u32,game+0x5B2C)=player;W(u32,game+0x5AB0)=0xABCDEF;W(u32,info)=object;W(s8,target+0x326)=-2;W(u32,target+0x3A4)=7;W(u8,target+0x3A8)=4;W(f32,target+0x314)=10;W(f32,target+0x31C)=20;W(f32,attacker+0x314)=3;W(f32,attacker+0x31C)=5;W(f32,attacker+0x33C)=6;W(f32,attacker+0x344)=8;sounds=particles=steals=music=powers=enemy_calls=normal_calls=critical_calls=0;pause=0xAA;magnitude=0;}
int main(void){unsigned cases=0;mutate=missing=0;const u16 names[]={0xF6,190,216,188};
for(unsigned h=0;h<256;h++)for(unsigned damage=0;damage<16;damage++)for(unsigned n=0;n<4;n++)for(unsigned dead=0;dead<2;dead++)for(unsigned type=0;type<3;type++)for(unsigned spin=0;spin<2;spin++){
 reset();W(u16,target+8)=names[n];W(s8,target+0x3A1)=(s8)h;W(u8,info+8)=damage;W(u8,info+9)=dead;W(u8,info+0xA)=type==0?1:type==1?9:12;W(u8,player+0x3AC)=spin?8:0;W(u8,player+0x69E8)=1;W(u32,info+0x14)=target+0x314;
 s8 health=(s8)h;if(damage)health=(s8)(health-damage*(spin?2:1));unsigned lethal=health<=0,forced=lethal&&n==0;assert(randomizer_ganondorf_hit_check(target,info)==attacker);assert(W(s8,target+0x3A1)==health&&W(u8,info+9)==(lethal?1:dead)&&W(u16,info+0xE)==0x8123);assert(sounds==1&&music==(damage!=0));assert(angle_x==-7&&angle_z==-15);assert(particles==((lethal&&!forced)?2u:0u));u32 expected=lethal&&!forced?0x2828:(!forced&&dead?0x2806:0x1111);assert(sound_id==expected&&sound_arg==(lethal&&!forced?0:(!forced&&n==1?0x33:0x20)));u8 expected_pause=0xAA;if(type==0)expected_pause=lethal&&!forced?10:!forced&&dead?7:3;assert(pause==expected_pause);cases++;}
/* Actual dead-branch decision is retained across sound callbacks changing health. */
reset();mutate=1;W(s8,target+0x3A1)=0;W(u32,info+0x14)=target+0x314;randomizer_ganondorf_hit_check(target,info);assert(sounds==1&&particles==2&&!normal_calls);cases++;
reset();mutate=2;W(s8,target+0x3A1)=0;W(u32,info+0x14)=target+0x314;randomizer_ganondorf_hit_check(target,info);assert(particles==2);cases++;
reset();mutate=3;W(s8,target+0x3A1)=10;W(u8,info+8)=1;assert(randomizer_ganondorf_hit_check(target,info)==target);cases++;
mutate=0;reset();missing=1;assert(randomizer_ganondorf_hit_check(target,info)==0&&!enemy_calls&&!sounds);missing=0;cases++;
for(unsigned flags=0;flags<2;flags++)for(unsigned stole=0;stole<2;stole++){
reset();W(s8,target+0x3A1)=20;W(u8,info+8)=5;W(u32,object+0x10)=flags?0x8000:0;W(u32,target+0x2E0)=0x380000;W(s16,attacker+8)=stole?446:188;W(u8,target+0x3A9)=2;randomizer_ganondorf_hit_check(target,info);assert(W(u8,info+8)==(flags||stole?0:5));assert(W(s8,target+0x3A1)==(flags||stole?20:15));assert(steals==stole);if(stole)assert(W(u8,target+0x3A9)==1&&W(u8,target+0x3A8)==5);cases++;}
reset();W(s8,target+0x3A1)=10;magnitude=50.00000000000001;randomizer_ganondorf_hit_check(target,info);assert(angle_x==-6&&angle_z==-8);cases++;
for(unsigned se=0;se<256;se++)for(unsigned hammer=0;hammer<2;hammer++){
reset();W(s8,target+0x3A1)=10;W(u8,info+9)=1;W(u8,object+0x6C)=se;W(u32,object+0x10)=hammer?0x10000:0;randomizer_ganondorf_hit_check(target,info);u32 id=hammer?0x2855:se==4?0x2835:se==5?0x2836:se==7?0x2879:se==8?0x286F:0x2806;assert(sound_id==id&&critical_calls==1);cases++;}
reset();W(s8,target+0x3A1)=10;W(s16,target+8)=216;W(u8,target+0x570)=2;randomizer_ganondorf_hit_check(target,info);assert(sound_id==0x693A&&sound_arg==0&&!normal_calls);cases++;
printf("Collision hit consumer: %u cases passed\n",cases);}
'''
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory);(root/'test.c').write_text(code)
   subprocess.run([os.environ.get("CC", "cc"),'-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(root/'test.c'),'-o',str(root/'test')],check=True)
   subprocess.run([str(root/'test')],check=True)
if __name__=='__main__':unittest.main()
