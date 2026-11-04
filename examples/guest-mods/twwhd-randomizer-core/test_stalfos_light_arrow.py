"""Complete Stalfos source consumer, light propagation and inline common paths."""
from pathlib import Path
import os,subprocess,tempfile,unittest
class Stalfos(unittest.TestCase):
 def test_consumer(self):
  code=r'''

#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef int8_t s8;typedef uint16_t u16;typedef int16_t s16;typedef float f32;typedef double f64;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 actor[0x2600],upper[0x2600],player[0x6A00],play[0x6100],morf1[0x100],morf2[0x100],model[0x100],node[0x100],matrices[26*48],matrix[48],regs[0x2000],emitter[0x300],hit_object[0x100],attacker[0x500];static unsigned int collision_joints[7];static u32 matrixptr=(u32)matrix;static u8 another_hit,damage_timer;
static void* data(u32 a){switch(a){case 0x1018C7B0:return&matrixptr;case 0x1047B610:return regs;case 0x1047B688:return regs+0x78;case 0x101D0E9C:return collision_joints;case 0x1046DFDC:return&another_hit;case 0x101EACB7:return&damage_timer;}assert(0);return 0;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static struct{u32 key,value;}cells[256];static unsigned nc;
static u32* cell(u32 key){for(unsigned i=0;i<nc;i++)if(cells[i].key==key)return&cells[i].value;assert(nc<256);cells[nc].key=key;cells[nc].value=0;return&cells[nc++].value;}
#define RSTPTR(a,o) (*cell((a)+(o)))
static u32 wwhd_dComIfG_getGameInfo(void);
#include "stalfos_light_arrow.h"
static unsigned emitter_return;static unsigned calls,deletes,particles,judges,registers,parts,chains,weapons,calc_calls,lighting_calls,hits,animations,steals;static unsigned upper_present,upper_dead,ice_result;static int selected_hit;static u8 attack_type,cutbit,damage;static unsigned line_hit,attribute_value;static unsigned desired_actor_type;static f64 random_result=99.999999999;
static u32 wwhd_dComIfG_getGameInfo(void){++calls;return(u32)play;}
static u32 actor_address;
s32 rst_line_cross_native(u32 background,u32 line){assert(background==(u32)play+0x12A0&&line);return line_hit;}
void rst_line_destroy(u32 line,s32 flag){(void)line;(void)flag;++calls;}
void rst_line_construct(u32 line){(void)line;++calls;}
void rst_collision_set(u32 collision,u32 sphere){assert(collision==(u32)play+0x26A4&&sphere>=(u32)actor+0x668&&sphere<=(u32)actor+0xE9C);++registers;}
void rst_zero(u32 value,f32 rate,f32 step){(void)value;(void)rate;(void)step;++calls;}
void rst_angle_calc(u32 value,s16 target,s16 rate,s16 step){(void)value;(void)target;(void)rate;(void)step;++calls;}
void rst_translate(s32 mode,f32 x,f32 y,f32 z){(void)mode;(void)x;(void)y;(void)z;++calls;}
void rst_scale(s32 mode,f32 x,f32 y,f32 z){(void)mode;(void)x;(void)y;(void)z;++calls;}
void rst_position(u32 source,u32 target){assert(source&&target);memcpy((void*)target,(void*)source,12);}
void rst_sphere_radius(u32 sphere,f32 radius){(void)sphere;(void)radius;++calls;}
void rst_sphere_center(u32 sphere,u32 center){(void)sphere;(void)center;++calls;}
f64 rst_random_signed(f64 range){assert(range==9000.0);return random_result;}
s32 rst_ice(u32 ice){assert(ice==(u32)actor+0x21F0);++calls;return ice_result;}
void rst_head_animation(u32 actor,s32 animation,f32 morph,u32 loop,f32 speed,s32 sound){assert(actor==actor_address&&(animation==15||animation==16)&&morph==1&&!loop&&speed==1&&sound==-1);++animations;}
void rst_chain(u32 actor){assert(actor==actor_address);++chains;}
void rst_weapon_chain(u32 actor){assert(actor==actor_address);++weapons;}
void rst_wait(u32 actor){(void)actor;++calls;}
void rst_part(u32 actor,u32 index){assert(actor==actor_address&&index<26);++parts;}
void rst_move(u32 actor){(void)actor;++calls;}
u32 rst_attribute(u32 background,u32 polygon){assert(background==(u32)play+0x12A0&&polygon);return attribute_value;}
void rst_line_set(u32 line,u32 start,u32 end,u32 actor){(void)line;(void)start;(void)end;(void)actor;++calls;}
void rst_status_move(u32 status){(void)status;++calls;}
s32 rst_hit(u32 sphere){++hits;return selected_hit>=0&&sphere==(u32)actor+0x668+0x12C*(unsigned)selected_hit;}
u32 rst_hit_object(u32 sphere){assert(sphere==(u32)actor+0x668+0x12C*(unsigned)selected_hit);return(u32)hit_object;}
void rst_start_attack(u32 sphere,u32 center){(void)sphere;(void)center;++calls;}
void rst_move_attack(u32 sphere,u32 center){(void)sphere;(void)center;++calls;}
void rst_defence_sound(u32 actor,u32 object,u32 sound){(void)actor;(void)object;(void)sound;++calls;}
void rst_attack_power(u32 info){RST8(info,8)=damage;RST8(info,0xA)=attack_type;RSTPTR(info,4)=(u32)attacker;RSTU16(info,0x12)=cutbit;}
u32 rst_attack_check(u32 actor,u32 info){assert(actor==actor_address);RST8(info,8)=damage;RST8(info,0xA)=attack_type;RSTPTR(info,4)=(u32)attacker;RSTU16(info,0x12)=cutbit;return 0;}
s32 rst_reverb(s32 room){return room;}
u32 rst_environment(void){return 77;}
void rst_lighting(u32 environment,s32 type,u32 position,u32 tev){assert(environment==77&&type==0&&position==(u32)actor+0x314&&tev==(u32)actor+0x10C);++lighting_calls;}
void rst_smoke_end(u32 callback){(void)callback;++calls;}
u32 rst_particle_native(u32 controller,u32 group,u32 id,u32 position,u32 angle,u32 scale,u32 alpha,u32 callback,s32 room,u32 color,u32 color2,u32 scale2){assert(controller==42&&!scale&&!color&&!color2&&!scale2);if(id==0x817E){assert(group==0&&!angle&&alpha==255&&!callback&&room==-1);assert(position>=(u32)actor+0xFF8&&position<(u32)actor+0x1AE8);}else if(id==0x817B){assert(group==0&&!angle&&alpha==255&&!callback&&room==-1&&position==(u32)actor+0x314);}else if(id==0x2022){assert(group==2&&angle==(u32)actor+0x21C0&&alpha==185&&callback==(u32)actor+0x2110&&position==(u32)actor+0x21B4);}else{assert(id==0x24&&group==0&&angle==(u32)actor+0x21C0&&alpha==255&&!callback&&room==-1);}++particles;return emitter_return?(u32)emitter:0;}
u32 rst_judge(u32 callback,u32 data){assert(callback==0x025E1234);++judges;assert(*(unsigned int*)data==123);return upper_present?(u32)upper:0;}
s32 rst_delete(u32 actor){assert(actor==actor_address);++deletes;return 1;}
u32 rst_create(u32 process,u32 parameter,u32 position,s32 room,u32 angle,u32 scale,s32 subtype,u32 callback){(void)process;(void)parameter;(void)position;(void)room;(void)angle;(void)scale;(void)subtype;(void)callback;++calls;return 0;}
void rst_steal(u32 position,u32 table,s32 room,u32 flag,u32 index){assert(position==(u32)actor+0x314&&!flag);(void)table;(void)room;(void)index;++steals;}
void rst_cancel_carry(u32 actor){(void)actor;++calls;}
void rst_sound_native(u32 id,u32 position,u32 parameter,s32 reverb){(void)id;(void)position;(void)parameter;(void)reverb;++calls;}
void rst_monster_native(u32 id,u32 position,u32 process,u32 parameter,s32 reverb){(void)id;(void)position;(void)process;(void)parameter;(void)reverb;++calls;}
void rst_bgm_hit(s32 sound){(void)sound;++calls;}
void rst_enemy_damage(void){++calls;}
s32 rst_morf_play(u32 animation,u32 position,u32 flags,s32 reverb){(void)animation;(void)position;(void)flags;(void)reverb;++calls;return 0;}
void rst_morf_calc(u32 animation){assert(animation==(u32)morf1||animation==(u32)morf2);++calc_calls;}
void rst_rotate_start(u32 matrix,s16 angle){(void)matrix;(void)angle;++calls;}
void rst_rotate(u32 matrix,s16 angle){(void)matrix;(void)angle;++calls;}
void rst_matrix_copy(u32 source,u32 target){assert(source&&target);memmove((void*)target,(void*)source,48);}

static void reset(void){memset(actor,0,sizeof actor);memset(upper,0,sizeof upper);memset(player,0,sizeof player);memset(regs,0,sizeof regs);memset(emitter,0,sizeof emitter);memset(hit_object,0,sizeof hit_object);memset(attacker,0,sizeof attacker);emitter_return=0;nc=0;actor_address=(u32)actor;matrixptr=(u32)matrix;calls=deletes=particles=judges=registers=parts=chains=weapons=calc_calls=lighting_calls=hits=animations=steals=0;upper_present=upper_dead=ice_result=line_hit=attribute_value=desired_actor_type=0;selected_hit=-1;attack_type=cutbit=damage=0;RSTPTR((u32)play,0x5B2C)=(u32)player;RSTPTR((u32)play,0x5AB0)=42;RSTPTR((u32)actor,0x3D8)=(u32)morf1;RSTPTR((u32)actor,0x3DC)=(u32)morf2;RSTPTR((u32)morf1,0x90)=RSTPTR((u32)morf2,0x90)=(u32)model;RSTPTR((u32)model,0x2C)=(u32)node;RSTPTR((u32)node,0x10)=(u32)matrices;RST32((u32)actor,0x201C)=123;RST32((u32)actor,0x2014)=0xFFFFFFFF;for(unsigned i=0;i<7;i++)collision_joints[i]=i;}
int main(void){unsigned cases=0;
for(unsigned upper_counter=0;upper_counter<256;upper_counter++)for(unsigned lower_counter=0;lower_counter<256;lower_counter++)for(unsigned found=0;found<2;found++)for(unsigned dead=0;dead<2;dead++){
 reset();upper_present=found;RST8((u32)upper,0x21F6)=upper_counter;RST8((u32)actor,0x21F6)=lower_counter;RSTS8((u32)upper,0x20CD)=dead;assert(randomizer_stalfos_execute((u32)actor)==1);unsigned expected=found&&upper_counter&&!lower_counter?1:lower_counter;assert(RST8((u32)actor,0x21F6)==expected&&judges==1);assert(deletes==(found&&dead));if(found&&dead)assert(particles==26&&!registers&&!lighting_calls);else assert(!particles&&registers==8&&lighting_calls==1);++cases;
}
for(unsigned n=1;n<256;n++){reset();RST8((u32)actor,0x20CD)=n;assert(randomizer_stalfos_execute((u32)actor)==1&&RST8((u32)actor,0x20CD)==(u8)(n-1)&&deletes==(n==1)&&judges==0&&calls==1);++cases;}
reset();ice_result=1;assert(randomizer_stalfos_execute((u32)actor)==1&&!judges&&calc_calls==2&&chains==1&&!registers);++cases;
for(unsigned mode=0;mode<4;mode++)for(unsigned action=0;action<3;action++)for(unsigned weapon=0;weapon<4;weapon++){
 reset();RSTS8((u32)actor,0xFEC)=mode;RST16((u32)actor,0x3E2)=action==0?0:action==1?32:33;RSTS8((u32)actor,0xFED)=weapon;RSTF((u32)actor,0x424)=0.6f;rst_collision((u32)actor);assert(registers==8&&!RSTS8((u32)actor,0xFEC)&&!RSTS8((u32)actor,0xFED));++cases;
}
for(unsigned index=0;index<7;index++)for(unsigned type=0;type<10;type++)for(unsigned form=0;form<3;form++)for(unsigned hp=0;hp<2;hp++){
 reset();selected_hit=index;attack_type=type;cutbit=0;damage=1;RST8((u32)actor,0x25AD)=form;RSTS8((u32)actor,0x3A1)=hp?4:0;RST16((u32)attacker,8)=0xA8;RSTS8((u32)actor,0x2020)=1;RST8((u32)player,0x3AC)=0;rst_damage((u32)actor);assert(RST16((u32)actor,0x412)==5&&hits==index+1);++cases;
}
/* Base scale is 330, not the neighboring shape-angle/padding word32C. */
reset();RST32((u32)actor,0x32C)=0xDEADBEEFu;RSTF((u32)actor,0x330)=1.25f;RSTF((u32)actor,0x334)=2.5f;RSTF((u32)actor,0x338)=3.75f;assert(randomizer_stalfos_execute((u32)actor)==1);assert(RSTF((u32)model,0xBC)==1.25f&&RSTF((u32)model,0xC0)==2.5f&&RSTF((u32)model,0xC4)==3.75f&&RST32((u32)actor,0x32C)==0xDEADBEEFu);++cases;
/* Emitter creation, failure, countdown and release retain callback-visible state. */
for(unsigned enabled=0;enabled<2;enabled++)for(unsigned timer=0;timer<3;timer++){
 reset();emitter_return=enabled;RSTS8((u32)actor,0x21B0)=1;RST16((u32)actor,0x21A8)=timer;rst_effects((u32)actor);assert(particles==(timer<2));if(enabled&&timer<2){assert(RSTPTR((u32)actor,0x2130)==(u32)emitter&&RST16((u32)actor,0x2180)==199&&RSTF((u32)emitter,0x238)==1&&(RST32((u32)emitter,0x254)&0x40));}++cases;
}
for(unsigned timer=0;timer<3;timer++){reset();RSTPTR((u32)actor,0x2130)=(u32)emitter;RST16((u32)actor,0x2180)=timer;RSTF((u32)actor,0x21AC)=0.75f;rst_effects((u32)actor);assert(RSTF((u32)emitter,0x238)==0.75f);assert((RSTPTR((u32)actor,0x2130)==0)==(timer==1));if(timer==1)assert(RST32((u32)emitter,0x5C)==0xFFFFFFFFu&&(RST32((u32)emitter,0x254)&1));++cases;}
/* All ground attributes, hit/no-hit and emitter failure/success paths. */
for(unsigned hit=0;hit<2;hit++)for(unsigned attr=0;attr<13;attr++)for(unsigned present=0;present<2;present++){
 reset();line_hit=hit;attribute_value=attr;emitter_return=present;rst_smoke((u32)actor,4);unsigned actual=hit?attr:0;assert(particles==(actual<5||actual==11));if(present&&particles){if(actual==4){assert(RST32((u32)emitter,0x5C)==3&&RSTF((u32)emitter,0x34)==2);}else{assert(RSTF((u32)emitter,0x34)==4&&RSTF((u32)emitter,0x238)==2&&RSTF((u32)emitter,0x220)==0.8f);}}++cases;
}
/* Explicit conversion boundaries retain f64 random results before fctiwz. */
assert(rst_truncate(99.999999999)==99&&rst_truncate(-99.999999999)==(unsigned)-99);
assert(rst_truncate(NAN)==0x80000000u&&rst_truncate(INFINITY)==0x7FFFFFFFu&&rst_truncate(-INFINITY)==0x80000000u);cases+=5;
/* Copying overlapping raw matrices must capture all words, including NaNs. */
reset();unsigned int raw[24];for(unsigned i=0;i<24;i++)raw[i]=0x7F800001u+i;rst_copy_model((u32)raw-0xC8+4,(u32)raw);for(unsigned i=0;i<12;i++)assert(raw[i+1]==0x7F800001u+i);++cases;
printf("Stalfos: %u complete-consumer/inline-helper cases passed\n",cases);return 0;}

'''
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'test.c').write_text(code)
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
