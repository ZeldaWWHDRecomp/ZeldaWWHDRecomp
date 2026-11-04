"""Complete Magtail source consumer and light-arrow respawn bookkeeping."""
from pathlib import Path
import os,subprocess,tempfile,unittest
class Magtail(unittest.TestCase):
 def test_consumer(self):
  code=r'''

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef int8_t s8;typedef uint16_t u16;typedef int16_t s16;typedef float f32;typedef double f64;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) unsigned char actor[0x1E00],player[0x400],play[0x6100],hio[0x60],regs[0x2000],trig[0x10000],morf[8][0x100],models[8][0x100],matrix[48],stack[48],statics[0x200],table[0x200],hit_object[0x100],angle_result[8];static u32 matrixptr=(u32)matrix;static unsigned char color[4];
static void* data(u32 a){switch(a){case 0x1018C7B0:return&matrixptr;case 0x10465870:return hio;case 0x1048D0CC:return stack;case 0x1047B610:return regs;case 0x1047B688:return regs+0x78;case 0x104A44F8:return trig;case 0x10465848:return statics;case 0x1046584C:return statics+4;case 0x101BADD0:return statics+8;case 0x101EACB7:return statics+9;case 0x101BAE74:return table;case 0x101BAE8C:return table+24;case 0x101BAEA4:return table+48;case 0x101BADC8:return table+72;case 0x101BAE14:return table+80;case 0x101BAE34:return table+112;case 0x101D5E98:return color;}assert(0);return 0;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static struct{u32 key,value;}cells[256];static unsigned nc;
static u32* cell(u32 key){for(unsigned i=0;i<nc;i++)if(cells[i].key==key)return&cells[i].value;if(nc>=256){fprintf(stderr,"overflow key=%lx\n",(unsigned long)key);abort();}cells[nc].key=key;cells[nc].value=0;return&cells[nc++].value;}
#define RMTPTR(a,o) (*cell((a)+(o)))
static u32 wwhd_dComIfG_getGameInfo(void);
#include "magtail_light_arrow.h"
static unsigned calls,hit_calls,power_calls,check_calls,ice_result,appear_result,deletes,onactors,onswitches,body3_calls,particle_calls;static int hit_index;static unsigned attack_type;static unsigned constructor_mode;static f64 random_result;static unsigned fixture_damage;static f64 distance_result;static f32 last_y_target;
static u32 wwhd_dComIfG_getGameInfo(void){++calls;return(u32)play;}
static u32 actor_address;
f64 rmt_ground_cross(u32 background,u32 check){(void)background;(void)check;assert(background==(u32)play+0x12A0&&check);return-1000000000.0;}
s32 rmt_line_cross(u32 background,u32 line){(void)background;(void)line;assert(background==(u32)play+0x12A0&&line);return 0;}
u32 rmt_plane(u32 background,s32 index,u32 polygon){(void)background;(void)index;(void)polygon;return 0;}
void rmt_line_destroy(u32 line,s32 flag){(void)line;(void)flag;assert(flag==0&&RMT32(line,0x20)==0x100154BC&&RMT32(line,0x58)==0x100155FC&&RMT32(line,0x64)==0x100154CC);}
void rmt_ground_destroy(u32 check,s32 flag){(void)check;(void)flag;assert(flag==0&&RMT32(check,0x20)==0x100154EC&&RMT32(check,0x40)==0x1001550C&&RMT32(check,0x4C)==0x100154CC);}
void rmt_ground_construct(u32 check){(void)check;memset((void*)check,0xA5,0x54);}
void rmt_line_construct(u32 line){(void)line;memset((void*)line,0xA5,0x6C);}
void rmt_collision(u32 collision,u32 sphere){(void)collision;(void)sphere;++calls;}
void rmt_calc(u32 value,f32 target,f32 rate,f32 step){(void)rate;(void)step;++calls;if(value==(u32)actor+0x318)last_y_target=target;}
void rmt_zero(u32 value,f32 rate,f32 step){(void)value;(void)rate;(void)step;++calls;}
void rmt_angle_calc(u32 value,s16 target,s16 rate,s16 step){(void)value;(void)target;(void)rate;(void)step;++calls;}
void rmt_position(u32 source,u32 target){(void)source;(void)target;memcpy((void*)target,(void*)source,12);}
void rmt_sphere_radius(u32 sphere,f32 radius){(void)sphere;(void)radius;assert(sphere&&radius==radius);}
void rmt_sphere_center(u32 sphere,u32 center){(void)sphere;(void)center;++calls;}
s16 rmt_atan(f64 y,f64 x){(void)y;(void)x;++calls;return 0;}
f64 rmt_random(f64 range){(void)range;assert(range>0);return random_result;}
u32 rmt_angle_constructor(u32 angle,s16 x,s16 y,s16 z){(void)angle;(void)x;(void)y;(void)z;assert(angle);if(constructor_mode==1){RMT16((u32)angle_result,0)=101;RMT16((u32)angle_result,2)=202;RMT16((u32)angle_result,4)=303;return(u32)angle_result;}RMT16(angle,2)=y;RMT16(angle,0)=x;RMT16(angle,4)=z;return angle;}
void rmt_vector_add(u32 a,u32 target,u32 b){(void)a;(void)target;(void)b;for(unsigned i=0;i<3;i++)RMTF(target,4*i)=RMTF(a,4*i)+RMTF(b,4*i);}
void rmt_vector_subtract(u32 a,u32 target,u32 b){(void)a;(void)target;(void)b;for(unsigned i=0;i<3;i++)RMTF(target,4*i)=RMTF(a,4*i)-RMTF(b,4*i);}
s32 rmt_ice(u32 ice){(void)ice;assert(ice==(u32)actor+0x1A1C);return ice_result;}
void rmt_animation(u32 actor,s32 animation,f32 morph,u32 loop,f32 speed,s32 sound){(void)actor;(void)animation;(void)morph;(void)loop;(void)speed;(void)sound;++calls;}
void rmt_bg_check(u32 actor){(void)actor;++calls;}
void rmt_texture(u32 actor,u16 index){(void)actor;(void)index;++calls;}
void rmt_wall_check(u32 actor){(void)actor;++calls;}
void rmt_explode(u32 actor){(void)actor;++calls;}
void rmt_water_sound(u32 actor){(void)actor;++calls;}
void rmt_sound(u32 actor,u32 id,u32 parameter){(void)actor;(void)id;(void)parameter;++calls;}
void rmt_monster_out(u32 actor,u32 id,u32 parameter){(void)actor;(void)id;(void)parameter;++calls;}
f64 rmt_sin_out(u32 table,s16 angle){(void)table;(void)angle;++calls;return 0;}
void rmt_body2(u32 actor){(void)actor;++calls;}
void rmt_body3(u32 actor){(void)actor;assert(actor==(u32)actor_address);++body3_calls;if(fixture_damage)assert(RMTS8(actor,0x1DD4)==1&&RMT8(actor,0x1A22)==1);}
void rmt_body4(u32 actor){(void)actor;++calls;}
void rmt_body5(u32 actor){(void)actor;++calls;}
void rmt_roll(u32 actor){(void)actor;++calls;}
f64 rmt_cos_out(u32 table,s16 angle){(void)table;(void)angle;++calls;return 0;}
u32 rmt_action_target(u32 attention,s32 index){(void)attention;(void)index;++calls;return 0;}
s32 rmt_lockon(u32 attention){(void)attention;++calls;return 0;}
void rmt_line_set(u32 line,u32 start,u32 end,u32 actor){(void)line;(void)start;(void)end;(void)actor;++calls;}
void rmt_status_move(u32 status){(void)status;++calls;}
s32 rmt_attack_hit(u32 sphere){(void)sphere;++calls;return 0;}
s32 rmt_hit(u32 sphere){(void)sphere;++hit_calls;return hit_index==8?sphere==(u32)actor+0x18C4:hit_index>=0&&sphere==(u32)actor+0xF64+0x12C*(unsigned)hit_index;}
u32 rmt_hit_object(u32 sphere){(void)sphere;assert(sphere==(u32)actor+0x18C4||sphere>=(u32)actor+0xF64);return(u32)hit_object;}
void rmt_defence(u32 actor,u32 object,u32 sound){(void)actor;(void)object;(void)sound;++calls;}
void rmt_attack_power(u32 info){(void)info;++power_calls;RMT8(info,8)=1;RMT8(info,0xA)=attack_type;RMTA(info,0xC).y=123;RMTS32(info,0x18)=0;assert(RMTPTR(info,0x14)==0||RMTPTR(info,0x14)==(u32)actor+0x1990);}
u32 rmt_attack_check(u32 actor,u32 info){(void)actor;(void)info;++check_calls;RMT8(info,8)=1;RMT8(info,0xA)=attack_type;RMTA(info,0xC).y=123;return 0;}
s32 rmt_reverb(s32 room){(void)room;++calls;return 0;}
void rmt_flush(u32 position,s32 mode){(void)position;(void)mode;++calls;}
u32 rmt_particle_native(u32 controller,u32 group,u32 id,u32 position,u32 angle,u32 scale,u32 alpha,u32 callback,s32 room,u32 color,u32 color2,u32 scale2){(void)controller;(void)group;(void)id;(void)position;(void)angle;(void)scale;(void)alpha;(void)callback;(void)room;(void)color;(void)color2;(void)scale2;assert(controller==42&&alpha==255&&!color&&!color2&&!scale2);++particle_calls;return 0;}
s32 rmt_simple_native(u32 controller,u32 id,u32 position,u32 alpha,u32 color,u32 color2,s32 draw){(void)controller;(void)id;(void)position;(void)alpha;(void)color;(void)color2;(void)draw;assert(controller==42&&(id==0x8058||id==0x8059)&&position&&alpha==255&&color==(u32)data(0x101D5E98)&&color2==color&&!draw);++particle_calls;return 1;}
void rmt_on_actor(u32 save,u32 id,s32 room){(void)save;(void)id;(void)room;assert(save==(u32)play+0x20&&id==0x1234&&room==-6);++onactors;}
s32 rmt_switch(u32 save,u32 flag,s32 room){(void)save;(void)flag;(void)room;assert(save==(u32)play+0x20&&flag==6&&room==-4);return appear_result;}
void rmt_on_switch(u32 save,u32 flag,s32 room){(void)save;(void)flag;(void)room;assert(save==(u32)play+0x20&&flag==7&&room==-4);++onswitches;}
s32 rmt_delete(u32 actor){(void)actor;assert(actor==(u32)actor_address);++deletes;return 1;}
void rmt_min(u32 actor,f32 x,f32 y,f32 z){(void)actor;(void)x;(void)y;(void)z;++calls;}
void rmt_max(u32 actor,f32 x,f32 y,f32 z){(void)actor;(void)x;(void)y;(void)z;++calls;}
void rmt_pos_move(u32 actor,u32 collision){(void)actor;(void)collision;++calls;}
f64 rmt_distance(u32 actor,u32 target){(void)actor;(void)target;++calls;return distance_result;}
s16 rmt_actor_angle(u32 actor,u32 target){(void)actor;(void)target;++calls;return 0;}
void rmt_pillar(u32 position,f32 scale){(void)position;(void)scale;++calls;}
u32 rmt_search(u32 callback,u32 data){(void)callback;(void)data;++calls;return 0;}
void rmt_monster_native(u32 id,u32 position,u32 process,u32 parameter,s32 reverb){(void)id;(void)position;(void)process;(void)parameter;(void)reverb;++calls;}
void rmt_bgm(s32 sound){(void)sound;++calls;}
void rmt_enemy_damage(void){++calls;}
s32 rmt_morf_play(u32 animation,u32 position,u32 flags,s32 reverb){(void)animation;(void)position;(void)flags;(void)reverb;++calls;return 0;}
void rmt_rotate_y_start(u32 matrix,s16 angle){(void)matrix;(void)angle;++calls;}
void rmt_rotate_x_start(u32 matrix,s16 angle){(void)matrix;(void)angle;++calls;}
void rmt_rotate_x(u32 matrix,s16 angle){(void)matrix;(void)angle;++calls;}
void rmt_rotate_y(u32 matrix,s16 angle){(void)matrix;(void)angle;++calls;}
void rmt_rotate_z(u32 matrix,s16 angle){(void)matrix;(void)angle;++calls;}
void rmt_stack_translate(f32 x,f32 y,f32 z){(void)x;(void)y;(void)z;++calls;}
void rmt_stack_scale(f32 x,f32 y,f32 z){(void)x;(void)y;(void)z;++calls;}
void rmt_assert(u32 file,s32 line,u32 message){(void)file;(void)line;(void)message;++calls;}
void rmt_vector_add_native(u32 a,u32 b,u32 destination){(void)a;(void)b;(void)destination;for(unsigned i=0;i<3;i++)RMTF(destination,4*i)=RMTF(a,4*i)+RMTF(b,4*i);}
void rmt_matrix_vector(u32 matrix,u32 source,u32 destination){(void)matrix;(void)source;(void)destination;memcpy((void*)destination,(void*)source,12);}
void rmt_translate(u32 matrix,f32 x,f32 y,f32 z){(void)matrix;(void)x;(void)y;(void)z;++calls;}
f64 rmt_sqrt(f64 value){(void)value;return sqrt(value);}

static s32 guard(u32 self){assert(self==(u32)player);return 0;}
static void reset(void){memset(actor,0,sizeof actor);memset(player,0,sizeof player);memset(play,0,sizeof play);memset(hio,0,sizeof hio);memset(regs,0,sizeof regs);memset(models,0,sizeof models);memset(hit_object,0,sizeof hit_object);nc=0;actor_address=(u32)actor;calls=hit_calls=power_calls=check_calls=deletes=onactors=onswitches=body3_calls=particle_calls=0;ice_result=appear_result=0;hit_index=-1;attack_type=0;fixture_damage=0;random_result=0;distance_result=0;last_y_target=0;constructor_mode=0;RMTPTR((u32)play,0x5B2C)=(u32)player;RMTPTR((u32)play,0x5AB0)=42;RMTPTR((u32)player,0xB4)=(u32)angle_result;RMTPTR((u32)angle_result,0x3C)=(u32)guard;for(unsigned i=0;i<8;i++){RMTPTR((u32)actor,0x3E0+4*i)=(u32)morf[i];RMTPTR((u32)morf[i],0x90)=(u32)models[i];}RMT8((u32)actor,0x3D0)=1;RMT16((u32)actor,0x580)=2;RMT16((u32)actor,0x582)=200;RMT8((u32)actor,0x570)=2;RMT16((u32)actor,0x572)=2;RMT16((u32)actor,0x578)=2;RMT16((u32)actor,0x57A)=2;}
int main(void){unsigned cases=0;
/* Head light-arrow bookkeeping must be present before the following native action. */
for(unsigned counter=0;counter<256;counter++)for(unsigned pending=0;pending<256;pending++){
 reset();hit_index=8;fixture_damage=1;RMT8((u32)actor,0x1A22)=counter;RMT8((u32)actor,0x1DD4)=pending;RMT32((u32)hit_object,0x10)=0x100000;assert(randomizer_magtail_execute((u32)actor)==1);assert(RMT8((u32)actor,0x1DD4)==1&&RMT8((u32)actor,0x1A22)==1&&RMT16((u32)actor,0x57C)==5&&!power_calls&&!check_calls&&body3_calls==1);++cases;
}
/* Original body hits still set the extra Y offset and zero health. */
for(unsigned part=0;part<8;part++)for(unsigned mode=0;mode<3;mode++){
 reset();hit_index=part;RMT8((u32)actor,0xD20)=mode;RMT32((u32)hit_object,0x10)=0x100000;RMTF((u32)regs,0)=5;RMTS8((u32)actor,0x3A1)=8;rmt_damage_check((u32)actor);if(mode!=1||part>=2)assert(RMT8((u32)actor,0x1DD4)==1&&RMT8((u32)actor,0x1A22)==1&&RMTF((u32)actor,0x1A24)==-15&&RMTS8((u32)actor,0x3A1)==0);else assert(!RMT8((u32)actor,0x1DD4));++cases;
}
for(unsigned part=0;part<9;part++)for(unsigned type=0;type<8;type++)for(unsigned action=0;action<3;action++){
 reset();hit_index=part;attack_type=type;RMT8((u32)actor,0x570)=action;RMT8((u32)actor,0x1A17)=3;RMTS8((u32)actor,0x3A1)=8;rmt_damage_check((u32)actor);assert(!RMT8((u32)actor,0x1DD4));++cases;
}
/* Appearance gates and early death preserve exact room/id/save contracts. */
for(unsigned gate=0;gate<2;gate++){reset();RMT8((u32)actor,0x3D7)=7;RMTS8((u32)actor,0x326)=-4;appear_result=gate;assert(randomizer_magtail_execute((u32)actor)==1);assert(RMT8((u32)actor,0x3D7)==(gate?0:7));++cases;}
reset();RMT8((u32)actor,0x3D5)=7;RMTF((u32)actor,0x318)=-6000;RMTU16((u32)actor,0x2D8)=0x1234;RMTS8((u32)actor,0x326)=-4;RMTS8((u32)actor,0x2FE)=-6;assert(randomizer_magtail_execute((u32)actor)==1&&onswitches==1&&onactors==1&&deletes==1);++cases;
reset();ice_result=1;assert(randomizer_magtail_execute((u32)actor)==1&&calls==1&&!body3_calls);++cases;
/* Full common paths: material wrapping, live joint updates and particle contracts. */
for(unsigned material=0;material<3;material++)for(unsigned frame=0;frame<140;frame++){
 reset();RMTS8((u32)hio,4)=1;RMT8((u32)actor,0x400)=material;RMTS32((u32)actor,0x404)=frame;RMTS32((u32)actor,0x408)=frame;RMT8((u32)actor,0x3D0)=0;assert(randomizer_magtail_execute((u32)actor)==1);unsigned step=material==1?2:1,hi=material==0?40:material==1?100:130,bhi=material==0?30:material==1?90:120;assert(RMTS32((u32)actor,0x404)==(s32)(frame+step>hi?(material?hi:0):frame+step));assert(RMTS32((u32)actor,0x408)==(s32)(frame+step>bhi?(material?bhi:0):frame+step));assert(particle_calls==16&&RMT8((u32)actor,0x468)==1);++cases;
}
/* Expanded aggregate add retains constructor effects/returned pointer and aliasing. */
for(unsigned returned=0;returned<2;returned++){
 reset();s16 values[6]={1,2,3,10,20,30};constructor_mode=returned;rmt_angles_add((u32)values,(u32)values,(u32)(values+3));assert(values[0]==(returned?101:11)&&values[1]==(returned?202:22)&&values[2]==(returned?303:33));++cases;
}
/* Complete trail/collider helper over every ring index and pause/fast state. */
for(unsigned trail=0;trail<64;trail++)for(unsigned fast=0;fast<2;fast++)for(unsigned paused=0;paused<2;paused++)for(unsigned invulnerable=0;invulnerable<2;invulnerable++){
 reset();RMTS32((u32)actor,0xD10)=trail;RMT8((u32)actor,0xD1C)=fast;RMT16((u32)actor,0x5AA)=paused;RMT16((u32)actor,0x57C)=invulnerable;RMT8((u32)actor,0xD1D)=1;RMT32((u32)actor,0x32C)=0xDEADBEEF;RMTF((u32)actor,0x330)=1.25f;RMTF((u32)actor,0x334)=2.5f;RMTF((u32)actor,0x338)=3.75f;RMTV((u32)actor,0x314).x=5;RMTV((u32)actor,0x314).y=6;RMTV((u32)actor,0x314).z=7;rmt_body_control1((u32)actor);assert(RMTS32((u32)actor,0xD10)==(s32)(paused?trail:(trail+1)&63));for(unsigned i=0;i<8;i++)assert(RMTF((u32)models[i],0xBC)==1.25f&&RMTF((u32)models[i],0xC0)==2.5f&&RMTF((u32)models[i],0xC4)==3.75f&&RMTF((u32)actor,0x5BC+12*i)==5);assert(RMT32((u32)actor,0x32C)==0xDEADBEEF);++cases;
}
/* Native randomF is double until fadds, not rounded before adding50. */
reset();random_result=2.999998;RMT16((u32)actor,0x580)=0;rmt_eye_tex_anm((u32)actor);assert(RMT16((u32)actor,0x580)==52);++cases;
/* Representative non-target complete movement/fight/roll/destroyed paths. */
for(unsigned action=0;action<4;action++)for(unsigned mode=0;mode<24;mode++){
 reset();RMT8((u32)actor,0x570)=action;RMT8((u32)actor,0x571)=mode;RMT16((u32)actor,0x582)=0;RMTF((u32)hio,0x24)=100;assert(randomizer_magtail_execute((u32)actor)==1);assert(!RMT8((u32)actor,0x1DD4));++cases;
}
/* Nested Acch uses flags+28 and ground+94, not unrelated neighbor fields. */
reset();RMT8((u32)actor,0x570)=1;RMT8((u32)actor,0x571)=10;RMT32((u32)actor,0xD8C)=0x20;RMT32((u32)actor,0xD80)=0;rmt_fight((u32)actor);assert(RMT8((u32)actor,0x571)==0);++cases;
reset();RMT8((u32)actor,0x570)=1;RMT8((u32)actor,0x571)=0;distance_result=1000;RMTF((u32)hio,0x24)=100;RMTF((u32)actor,0xDF8)=123;RMTF((u32)actor,0xE88)=999;RMT16((u32)actor,0x582)=0;rmt_fight((u32)actor);assert(last_y_target==163);++cases;
printf("Magtail: %u complete-consumer/helper cases passed\n",cases);return 0;}

'''
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'test.c').write_text(code)
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
