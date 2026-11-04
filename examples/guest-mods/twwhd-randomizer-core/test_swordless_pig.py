"""Complete pig consumer: swordless damage, carry transitions and common paths.
Host endpoints control resources/physics; native Execute parity is still pending.
"""
from pathlib import Path
import os, subprocess, tempfile, unittest
class Pig(unittest.TestCase):
 def test_complete_consumer(self):
  code=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <math.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef int8_t s8;typedef uint16_t u16;typedef int16_t s16;typedef float f32;typedef double f64;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
#define RP_FMA fmaf
static u8 actor[0xE30],player[0x400],morf[0xC8],play[0x6000],regs[0x1000],trig[0x10000],save[0x1300],statics[4];
static u32 saveptr=(u32)save,matrixptr;static unsigned hit,sounds,particles,attacks,foods,animations,lines,ends;
static int damage,swim_result,food_result;static f64 random_value,sqrt_value;static unsigned sqrt_override,rotations;static unsigned mutate_reverb;static u32 sound_pid;static unsigned actual_sword,callback_sword,last_particle_id;static unsigned speed_move,frame_result,carry_result;
static void* memory(u32 p){if(p==1)return&saveptr;if(p==0x101F84DC)return&saveptr;if(p==0x101B7FA0)return statics;if(p==0x1018C7B0)return&matrixptr;if(p==0x1047B610)return regs;if(p==0x104A44F8)return trig;assert(0);return 0;}
#define WWHD_GAME_DATA(a,t) (*(t*)memory(a))
/* Guest pointer cells stay four bytes in production; virtualize only host-width pointers. */
static struct{u32 key,value;} ptrs[512];static unsigned n_ptrs;
static u32* pointer_cell(u32 key){for(unsigned i=0;i<n_ptrs;i++)if(ptrs[i].key==key)return&ptrs[i].value;assert(n_ptrs<512);ptrs[n_ptrs].key=key;ptrs[n_ptrs].value=0;return&ptrs[n_ptrs++].value;}
#define RP_PTR(a,o) (*pointer_cell((a)+(o)))
#include "swordless_pig.h"
void rp_calc_native(u32 a,f32 target,f32 rate,f32 step){(void)rate;(void)step;RP_F32(a,0)=target;}
void rp_zero_native(u32 a,f32 rate,f32 step){(void)rate;(void)step;RP_F32(a,0)=0;}
void rp_angle_native(u32 a,s16 target,s16 rate,s16 step){(void)a;(void)target;(void)rate;(void)step;}
s32 rp_distance_angle(s16 a,s16 b){(void)a;(void)b;return 0;}
s16 rp_atan(f32 y,f32 x){(void)y;(void)x;return 0;}
f64 rp_random(void){return random_value;}
f64 rp_random_f(f64 limit){(void)limit;return random_value;}
f64 rp_random_fx(f64 limit){(void)limit;return random_value;}
f64 rp_sqrt(f64 value){return sqrt_override?sqrt_value:sqrt(value);}
s32 rp_check_frame(u32 ctrl,f32 frame){(void)ctrl;(void)frame;return frame_result;}
void rp_animation(u32 actor,s32 animation,f32 morph,u8 loop,f32 rate,s32 sound){(void)actor;(void)animation;(void)morph;(void)loop;(void)rate;(void)sound;++animations;}
s32 rp_carry(u32 actor){(void)actor;return 0;}
void rp_splash(u32 actor){(void)actor;}
s32 rp_swim_change(u32 actor){(void)actor;return swim_result;}
void rp_gas(u32 actor){(void)actor;}
void rp_smoke(u32 actor){(void)actor;}
void rp_target(u32 actor,u8 mode){(void)actor;(void)mode;}
s32 rp_food_check(u32 actor){(void)actor;return food_result;}
void rp_background(u32 actor){(void)actor;}
s32 rp_eye(u32 actor){(void)actor;return 0;}
void rp_speed(u32 actor){if(speed_move)RP_F32(actor,0x314)+=1;}
void rp_draw(u32 actor){(void)actor;}
void rp_attack(u32 actor){(void)actor;++attacks;}
void rp_food(u32 actor){(void)actor;++foods;}
f32 rp_sine_native(u32 table,s16 angle){(void)table;(void)angle;return 0;}
s32 rp_hit(u32 sphere){(void)sphere;return hit;}
u32 rp_hit_object(u32 sphere){(void)sphere;return 0x1234;}
void rp_clear_hit(u32 sphere){(void)sphere;}
void rp_attack_check(u32 actor,u32 info){(void)info;callback_sword=save[0x2E];assert(callback_sword==actual_sword);RP_S8(actor,0x3A1)=damage;++attacks;}
void rp_follow_end(u32 callback){(void)callback;++ends;}
void rp_ripple_end(u32 callback){(void)callback;}
void rp_smoke_end(u32 callback){(void)callback;}
s32 rp_reverb(s32 room){(void)room;if(mutate_reverb)RP_U32((u32)actor,4)=0xFEDC;return 0;}
void rp_monster_sound(u32 id,u32 position,u32 pid,u32 argument,s32 reverb){(void)id;(void)position;sound_pid=pid;(void)argument;(void)reverb;++sounds;}
u8 rp_shock(u32 vibration,s32 strength,s32 flags,u32 direction){(void)vibration;(void)strength;(void)flags;(void)direction;return 0;}
u32 rp_particle(u32 controller,u32 group,u32 id,u32 pos,u32 rotation,u32 scale,u32 alpha,u32 callback,s32 room,u32 color,u32 color2,u32 scale2){(void)controller;(void)group;last_particle_id=id;(void)pos;(void)rotation;(void)scale;(void)alpha;(void)callback;(void)room;(void)color;(void)color2;(void)scale2;++particles;return 0;}
f32 rp_distance(u32 actor,u32 player){(void)actor;(void)player;return 1000;}
s16 rp_player_angle(u32 actor,u32 player){(void)actor;(void)player;return 0;}
s32 rp_ground_angle(u32 actor,u32 result){(void)actor;(void)result;return 0;}
void rp_lock_off(u32 camera,u32 id){(void)camera;(void)id;}
s32 rp_daynight(void){return 0;}
void rp_event_on(u32 save,u32 flag){(void)save;(void)flag;}
u32 rp_event_reg(u32 save,u32 flag){(void)save;(void)flag;return 0;}
void rp_event_set_reg(u32 save,u32 flag,u32 value){(void)save;(void)flag;(void)value;}
s32 rp_special(u32 bg,u32 poly){(void)bg;(void)poly;return 0;}
s32 rp_attribute(u32 bg,u32 poly){(void)bg;(void)poly;return 0;}
u32 rp_material(u32 bg,u32 poly){(void)bg;(void)poly;return 0;}
void rp_mass(u32 manager,u32 object,u8 priority){(void)manager;(void)object;(void)priority;}
void rp_triangle(u32 bg,u32 poly,u32 a,u32 b,u32 c){(void)bg;(void)poly;(void)a;(void)b;(void)c;}
void rp_line_ctor(u32 line){(void)line;++lines;}
void rp_rotate_y(u32 matrix,s16 angle){(void)matrix;(void)angle;++rotations;}
void rp_position(u32 source,u32 result){(void)source;(void)result;}
void rp_vector_add(u32 a,u32 b,u32 out){(void)a;(void)b;(void)out;}
void rp_vector_subtract(u32 a,u32 out,u32 b){(void)a;(void)out;(void)b;}
f32 rp_square_mag(u32 vec){(void)vec;return 0;}
void rp_sphere_center(u32 sphere,u32 center){(void)sphere;(void)center;}
void rp_sphere_radius(u32 sphere,f32 radius){(void)sphere;(void)radius;}
s32 rp_morf_play(u32 morf,u32 position,u32 material,s8 reverb){(void)morf;(void)position;(void)material;(void)reverb;return 0;}
u32 rp_play(void){return (u32)play;}
void rp_line_poly_dtor(u32 line,s32 flags){(void)line;(void)flags;++lines;}
void rp_collision_set(u32 scene,u32 object){(void)scene;(void)object;}
void rp_off_bit(u32 value,u32 bit){(void)value;(void)bit;}
static unsigned assure_calls,assure_mode;static rp_string* first_string;static char long_a[0x40002],long_b[0x40002];
static void assure_test(rp_string* text){if(text->text==0x100127DC||text->text==0x100129C4)text->text=(u32)"sea";++assure_calls;if(assure_calls==1)first_string=text;if(assure_calls==3){if(assure_mode==1)text->text=first_string->text;if(assure_mode==2){text->text=first_string->text;first_string->text=(u32)"changed";}if(assure_mode>=3)text->text=(u32)long_b;}}
static void reset(void){memset(actor,0,sizeof actor);memset(player,0,sizeof player);memset(morf,0,sizeof morf);memset(play,0,sizeof play);memset(statics,0,sizeof statics);n_ptrs=0;hit=sounds=particles=attacks=foods=animations=lines=ends=speed_move=frame_result=carry_result=0;random_value=sqrt_value=0;sqrt_override=rotations=0;mutate_reverb=0;swim_result=food_result=0;assure_calls=assure_mode=0;RP_PTR(0x100127E0,0x14)=(u32)assure_test;RP_PTR((u32)actor,0x744)=(u32)morf;RP_PTR((u32)play,0x5B2C)=(u32)player;RP_PTR((u32)play,0x5AB0)=0x12345678;RP_F32((u32)morf,0x98)=1;RP_F32((u32)actor,0xA68)=-1000000000.0f;RP_S8((u32)actor,0x326)=-2;}
int main(void){unsigned cases=0;
 /* All signed health bytes, every actual sword byte, and both pig sizes.
  * Attack callback observes untouched equipment and supplies actual damage. */
 for(unsigned sword=0;sword<256;sword++)for(unsigned hp=0;hp<256;hp++)for(unsigned large=0;large<2;large++){
  reset();actual_sword=save[0x2E]=sword;damage=(s8)hp;hit=1;RP_U8((u32)actor,0x748)=large?8:0;RP_S16((u32)actor,0x768)=5;RP_S8((u32)actor,0x3A1)=77;
  rp_pl_attack_hit_check((u32)actor);assert(callback_sword==sword&&save[0x2E]==sword);assert(attacks==1&&sounds==1&&particles==1&&ends==2);assert(RP_S8((u32)actor,0x3A1)==((s8)hp<=0?10:(s8)hp));assert(RP_S16((u32)actor,0x766)==((s8)hp<=0?3:0));assert(RP_S16((u32)actor,0x768)==((s8)hp<=0?0x21:5));assert(statics[0]==((s8)hp<=0));++cases;
  reset();actual_sword=save[0x2E]=sword;RP_U8((u32)actor,0x748)=large?8:0;RP_S16((u32)actor,0x766)=1;RP_S16((u32)actor,0x768)=12;RP_S16((u32)actor,0x77E)=1;RP_U32((u32)actor,0x9FC)=0x20;RP_U8((u32)actor,0x3A1)=hp;RP_F32((u32)actor,0xA68)=-1000000000.0f;
  rp_carry_move((u32)actor);assert(RP_U8((u32)actor,0x3A1)==(u8)(hp-1));assert(save[0x2E]==sword);assert(RP_S16((u32)actor,0x768)==13&&RP_S16((u32)actor,0x76E)==16);assert((RP_U32((u32)actor,0x39C)&1)&&RP_F32((u32)actor,0x370)==0);++cases;
 }
 assert(cases==262144);
 /* Pig particle IDs are zero-extended u16 in native r5, including 0x80B6. */
 reset();rp_particle_set(0,0,0x80B6,0,0,0,255,0,-1,0,0,0);assert(last_particle_id==0x80B6);
 /* Native captures process ID before the reverb callback mutates actor fields. */
 reset();RP_U32((u32)actor,4)=0x1234;mutate_reverb=1;rp_sound((u32)actor,0x4822);assert(sound_pid==0x1234&&RP_U32((u32)actor,4)==0xFEDC);
 /* Unhit angry pig still enters the native run animation and attack mode. */
 reset();statics[0]=1;rp_pl_attack_hit_check((u32)actor);assert(animations==1&&RP_S16((u32)actor,0x766)==3&&RP_S16((u32)actor,0x768)==0x23);
 /* Already processed hits must not generate repeated damage/effects. */
 reset();hit=1;RP_U8((u32)actor,0x74B)=1;rp_pl_attack_hit_check((u32)actor);assert(!attacks&&!particles&&!sounds);
 /* Carry release retains the unordered-NaN <= branch and airborne branch. */
 reset();RP_S16((u32)actor,0x768)=10;RP_F32((u32)actor,0x370)=NAN;rp_carry_move((u32)actor);assert(RP_S16((u32)actor,0x768)==13&&RP_F32((u32)actor,0x374)==-3);
 reset();RP_S16((u32)actor,0x768)=11;RP_S16((u32)actor,0x792)=1;RP_F32((u32)actor,0x370)=1;rp_carry_move((u32)actor);assert(RP_S16((u32)actor,0x768)==12&&RP_F32((u32)actor,0x340)==20);
 /* Native swim change early return still destroys the local line checker. */
 reset();RP_S16((u32)actor,0x768)=23;swim_result=1;rp_swim_move((u32)actor);assert(lines==2);
 /* Untargeted normal swim/food early exits preserve downstream logic. */
 reset();swim_result=1;rp_normal_move((u32)actor);assert(!animations);
 reset();RP_S16((u32)actor,0x768)=1;food_result=1;RP_S16((u32)actor,0x76E)=9;rp_normal_move((u32)actor);assert(RP_S16((u32)actor,0x768)==1);
 /* Complete common tail: timer wrap, event cooldown pause, movement callbacks,
  * carry position restoration and both external attack/food dispatch paths. */
 for(unsigned mode=3;mode<=4;mode++)for(unsigned carried=0;carried<2;carried++){
  reset();RP_S16((u32)actor,0x766)=mode;RP_S16((u32)actor,0x768)=0;RP_S16((u32)actor,0x78E)=32767;RP_S16((u32)actor,0x776)=32767;RP_S16((u32)actor,0x76E)=-32768;play[0x5292]=1;RP_U32((u32)actor,0x2E0)=carried?0x2000:0;RP_VEC((u32)actor,0x314).x=100;RP_VEC((u32)actor,0x314).y=20;RP_VEC((u32)actor,0x314).z=30;RP_VEC((u32)actor,0xB98).x=99;RP_VEC((u32)actor,0xB98).z=-99;speed_move=1;for(unsigned j=0;j<8;j++)actor[0x388+j]=(u8)(0xA0+j);
  assert(randomizer_pig_execute((u32)actor)==1);assert(RP_S16((u32)actor,0x78E)==-32768&&RP_S16((u32)actor,0x776)==32767&&RP_S16((u32)actor,0x76E)==32767);assert(attacks==(mode==3)&&foods==(mode==4));assert(RP_VEC((u32)actor,0x314).x==(WWHD_RANDOMIZER_CLASSIC_MODE?(carried?199:200):(carried?110:111)));assert(RP_VEC((u32)actor,0x314).z==(WWHD_RANDOMIZER_CLASSIC_MODE?-69:20));for(unsigned j=0;j<8;j++)assert(actor[0x388+j]==(u8)(0xA0+j));assert(RP_F32((u32)actor,0x390)==(WWHD_RANDOMIZER_CLASSIC_MODE?199:110)&&RP_F32((u32)actor,0x394)==70&&RP_F32((u32)actor,0x398)==(WWHD_RANDOMIZER_CLASSIC_MODE?-69:20));
 }
 /* NaN operand priority and bit-preserving position copies. */
 reset();unsigned int a=0x7FC01234,b=0x7FC05678;f32 fa,fb;memcpy(&fa,&a,4);memcpy(&fb,&b,4);f32 value=rp_kb_fadds_ppc(fa,fb);unsigned int raw;memcpy(&raw,&value,4);assert(raw==a);RP_U32((u32)actor,0x314)=0x80000000u;RP_U32((u32)actor,0x318)=a;RP_U32((u32)actor,0x31C)=b;rp_copy_vector((u32)actor+0x798,(u32)actor+0x314);assert(RP_U32((u32)actor,0x798)==0x80000000u&&RP_U32((u32)actor,0x79C)==a&&RP_U32((u32)actor,0x7A0)==b);
 /* Native f64 random results round only after adding the f32 offset.
  * Premature f32 conversion makes the timer 100 instead of 99. */
 reset();random_value=49.99999599;rp_normal_move((u32)actor);assert(RP_S16((u32)actor,0x76E)==99);assert((int)(f32)((f64)(f32)random_value+50.0)==100);
 reset();random_value=299.99996947;RP_S16((u32)actor,0x768)=11;RP_S16((u32)actor,0x792)=1;RP_U8((u32)morf,0xA7)=1;RP_U32((u32)actor,0x2E0)=0x2000;rp_carry_move((u32)actor);assert(RP_S16((u32)actor,0x772)==899);assert((int)(f32)((f64)(f32)random_value+600.0)==900);
 reset();random_value=0.99999951;RP_S16((u32)actor,0x768)=13;RP_U8((u32)actor,0x3A1)=0;rp_carry_move((u32)actor);assert(RP_F32((u32)actor,0x370)==(f32)(random_value+15.0));assert(RP_F32((u32)actor,0x370)!=(f32)((f64)(f32)random_value+15.0));
 /* Both sqrt comparison consumers retain f64 precision through fcmp. */
 reset();sqrt_override=1;sqrt_value=1.0+0x1p-30;RP_S16((u32)actor,0x768)=3;RP_S16((u32)actor,0x76E)=10;RP_F32((u32)actor,0x814)=2;RP_F32((u32)actor,0x798)=1;rp_normal_move((u32)actor);assert(rotations==1);assert((f32)sqrt_value==1.0f);
 reset();sqrt_override=1;sqrt_value=1.0-0x1p-30;RP_S16((u32)actor,0x766)=3;RP_S16((u32)actor,0x768)=36;randomizer_pig_execute((u32)actor);assert(RP_S16((u32)actor,0x790)==1);assert((f32)sqrt_value==1.0f);
 /* cM_rnd likewise reaches its half-probability comparison unrounded. */
 reset();random_value=0.5-0x1p-30;RP_U32((u32)actor,0x9FC)=0x10;rp_swim_move((u32)actor);assert(RP_S16((u32)actor,0x76A)==0x4000);assert((f32)(0.5-0x1p-30)==0.5f);
 /* Real SafeString callback order: literal twice, stage once; captured pointer
  * equality wins before the post-callback literal reload, then bounded compare. */
 reset();assure_mode=0;assure_calls=0;RP_PTR(0x100127E0,0x14)=(u32)assure_test;memcpy(play+0x5134,"sea",4);assert(rp_sea((u32)"sea")&&assure_calls==3);
 assure_mode=1;assure_calls=0;assert(rp_sea((u32)"sea")&&assure_calls==3);
 assure_mode=2;assure_calls=0;assert(rp_sea((u32)"sea")&&assure_calls==3);
 memset(long_a,'s',sizeof long_a);memset(long_b,'s',sizeof long_b);assure_mode=3;assure_calls=0;assert(!rp_sea((u32)long_a)&&assure_calls==3);long_a[0x40000]=long_b[0x40000]=0;assure_calls=0;assert(rp_sea((u32)long_a));long_b[27]='a';assure_calls=0;assert(!rp_sea((u32)long_a));
 assert(rp_truncate(NAN)==0x80000000u&&rp_truncate(INFINITY)==0x7FFFFFFFu&&rp_truncate(-INFINITY)==0x80000000u);
 return 0;
}
'''
  for classic in (0,1):
   with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.c').write_text(code)
    subprocess.run([os.environ.get('CC','clang'),f'-DWWHD_RANDOMIZER_CLASSIC_MODE={classic}','-std=c11','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror','-I',str(Path(__file__).resolve().parent),str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
