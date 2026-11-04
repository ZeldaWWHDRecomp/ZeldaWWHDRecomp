"""Complete Puppet Ganon Execute: state transitions, camera paths and ABI endpoints."""
from pathlib import Path
import os, subprocess, tempfile, unittest
class Puppet(unittest.TestCase):
 def test_consumer(self):
  code = r'''

#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
typedef uintptr_t u32; typedef int32_t s32; typedef uint8_t u8;typedef int8_t s8;typedef uint16_t u16;typedef int16_t s16;typedef float f32;typedef double f64;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static u8 actor[0x15800],player[0x500],play[0x6000],phase2[0x4000],phase3[0x4000],save[0x1300],hio[0x200],regs[0x2000],sins[0x10000],thunder[16],light[16],tev[0x500];
static u32 saveptr=(u32)save,phase2ptr=(u32)phase2,phase3ptr=(u32)phase3,matrix=0x7770;static unsigned thunder_init;
static void* data(u32 a){switch(a){case 1:return&saveptr;case 0x10461A38:return&phase2ptr;case 0x10461A3C:return&phase3ptr;case 0x1018C7B0:return&matrix;case 0x10461CDC:return hio;case 0x1047B610:return regs;case 0x1047B688:return regs+0x78;case 0x104A44F8:return sins;case 0x10461A54:return&thunder_init;case 0x10461CC4:return thunder;case 0x10461CD0:return light;case 0x10461E58:return tev;}assert(0);return 0;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static struct{u32 key,value;} cells[128];static unsigned nc;
static u32* cell(u32 a){for(unsigned i=0;i<nc;i++)if(cells[i].key==a)return&cells[i].value;assert(nc<128);cells[nc].key=a;cells[nc].value=0;return&cells[nc++].value;}
#define RB_PTR(a,o) (*cell((a)+(o)))
static u32 wwhd_dComIfG_getGameInfo(void);
#include "post_puppet_layer.h"
static unsigned calls,next_calls,create_calls,order_calls,cam_calls,rotations,positions,random_calls,checks;
static int move_state=-1,light_state=-1,camera_state=-1,report_state=-1;static int keese;
static u32 matrix_seen;static int mutate_matrix;
static u32 wwhd_dComIfG_getGameInfo(void){++calls;return(u32)play;}
static void setpos(u32 p,u32 pos,s16 a){(void)p;(void)pos;(void)a;++calls;}
static void voice(u32 p,u32 v){assert(p==(u32)player&&v==29);++calls;}
static u32 actor_address;
u32 rb_env(void){++calls;return 42;}
void rb_lighting(u32 environment,u32 type,u32 position,u32 tev){assert(environment==42&&type==3&&position==(u32)light&&tev==(u32)data(0x10461E58));++calls;if(light_state>=0)RB_U8((u32)actor,0x15764)=light_state;}
u32 rb_search(u32 callback,u32 actor){(void)actor;++calls;return callback==0x0207B22C?(u32)phase2:callback==0x0207B27C?(u32)phase3:0;}
void rb_move(u32 actor){assert(actor==actor_address);++calls;if(move_state>=0)RB_U8(actor,0x15764)=move_state;}
void rb_camera_stop(u32 camera){(void)camera;++calls;}
void rb_camera_start(u32 camera){(void)camera;++calls;}
void rb_camera_trim(u32 camera,s32 trim){(void)camera;(void)trim;++calls;}
void rb_camera_set(u32 camera,u32 center,u32 eye,s16 bank,f32 fov){(void)camera;(void)center;(void)eye;(void)bank;(void)fov;++cam_calls;if(camera_state>=0)RB_U8((u32)actor,0x15764)=camera_state;}
void rb_meter_show(void){++calls;}
void rb_calc_native(u32 value,f32 target,f32 rate,f32 step){(void)rate;(void)step;RB_F32(value,0)=target;++calls;}
void rb_zero_native(u32 value,f32 rate,f32 step){(void)rate;(void)step;RB_F32(value,0)=0;++calls;}
void rb_angle_native(u32 value,s16 target,s16 rate,s16 step){(void)rate;(void)step;RB_S16(value,0)=target;++calls;}
void rb_rotate_y_start(u32 matrix,s16 angle){(void)matrix;(void)angle;++calls;}
void rb_rotate_x_start(u32 matrix,s16 angle){(void)angle;assert(!mutate_matrix||matrix==matrix_seen);++rotations;}
void rb_rotate_y(u32 matrix,s16 angle){(void)angle;assert(!mutate_matrix||matrix==matrix_seen);++rotations;}
void rb_position_native(u32 source,u32 out){memcpy((void*)out,(void*)source,12);++positions;}
s32 rb_reverb(s32 room){(void)room;++calls;return 0;}
void rb_sound(u32 id,u32 position,u32 parameter,s32 reverb){(void)id;(void)position;(void)parameter;(void)reverb;++calls;}
void rb_monster_sound(u32 id,u32 position,u32 parameter,s32 reverb){(void)id;(void)position;(void)parameter;(void)reverb;++calls;}
void rb_bgm(u32 id){(void)id;++calls;}
s32 rb_order(u32 actor,u32 type,u32 flag,u32 argument){assert(actor==actor_address&&type==2&&flag==0xFFFF&&argument==0);++order_calls;return 1;}
u32 rb_particle_native(u32 controller,u32 group,u32 effect,u32 position,u32 angle,u32 scale,u32 alpha,u32 callback,s32 room,u32 color,u32 color2,u32 scale2){(void)controller;(void)group;(void)effect;(void)position;(void)angle;(void)scale;(void)alpha;(void)callback;(void)room;(void)color;(void)color2;(void)scale2;++calls;return 0;}
u8 rb_shock_native(u32 vibration,s32 strength,s32 flags,u32 vector){(void)vibration;(void)strength;(void)flags;(void)vector;++calls;return 0;}
void rb_event_on(u32 save,u32 flag){(void)save;(void)flag;++calls;}
s32 rb_keese_count(u32 actor){(void)actor;return keese;}
f64 rb_random_f(f64 limit){assert(limit==500.0);++random_calls;return 10.25;}
f64 rb_random_fx(f64 limit){assert(limit==2500.0);++random_calls;return random_calls==1?12.25:99.5;}
u32 rb_create(u32 process,u32 parameters,u32 position,s32 room,u32 angle,u32 scale,s32 subtype,u32 callback){assert(process==0xD7&&parameters==0xFFFF0003u&&room==-3&&!angle&&!scale&&subtype==-1&&!callback);assert(RB_F32(position,0)==99.5f&&RB_F32(position,4)==3510.25f);/* z is intentionally not read. */++create_calls;return 1;}
void rb_next_stage(u32 stage,s16 point,s8 room,s8 layer,f32 speed,u32 mode,s32 setpoint,s8 wipe){assert(stage==0x10008C40&&point==4&&room==0&&layer==-1&&speed==0&&mode==0&&setpoint==1&&wipe==0);assert(RB_S8((u32)actor,0x15764)==0);++next_calls;}
s16 rb_atan(f64 y,f64 x){(void)y;(void)x;matrix_seen=WWHD_GAME_DATA(0x1018C7B0,u32);if(mutate_matrix)WWHD_GAME_DATA(0x1018C7B0,u32)^=0x1110;return 0;}
f64 rb_sqrt(f64 value){return sqrt(value);}
void rb_report(s32 x,s32 y,u32 format,...){(void)x;(void)y;(void)format;if(report_state>=0)RB_U8((u32)actor,0x15764)=report_state;}

static void init(int mode){memset(actor,0,sizeof actor);memset(player,0,sizeof player);memset(play,0,sizeof play);memset(hio,0,sizeof hio);memset(regs,0,sizeof regs);nc=0;actor_address=(u32)actor;phase2ptr=(u32)phase2;phase3ptr=(u32)phase3;saveptr=(u32)save;matrix=0x7770;RB_PTR((u32)play,0x5B2C)=(u32)player;RB_PTR((u32)play,0x5AF8)=0x10000;RB_PTR((u32)player,0xB4)=0x20000;RB_PTR(0x20000,0x114)=(u32)setpos;RB_PTR(0x20000,0xE4)=(u32)voice;RB_S8((u32)actor,0x151CE)=mode;RB_S8((u32)actor,0x326)=-3;RB_U16((u32)actor,0xF8)=2;move_state=light_state=camera_state=report_state=-1;calls=next_calls=create_calls=order_calls=cam_calls=positions=rotations=random_calls=0;keese=100;mutate_matrix=0;}
int main(void){
 for(unsigned state=0;state<256;state++)for(int output=-1;output<3;output++){
  init(0);RB_U8((u32)actor,0x15764)=state;move_state=output;
  assert(randomizer_puppet_execute((u32)actor)==1);
  unsigned expected=output>=0?(unsigned)output:(state==1?0:state);
  assert(next_calls==(expected==2));assert(RB_U8((u32)actor,0x15764)==(expected==2?0:expected));assert(RB_S8((u32)actor,0x3D5)==(state==1?2:0));++checks;
 }
 for(unsigned state=0;state<256;state++){init(22);RB_U8((u32)actor,0x3D4)=0xFF;RB_U8((u32)actor,0x15764)=state;assert(randomizer_puppet_execute((u32)actor)==1&&!calls&&!next_calls);++checks;}
 const int modes[]={0,1,2,3,10,11,12,20,21,22,150,-1};const int frames[]={0,90,100,130,152,180,201,220,235,250,265,281,297,313,369,370,389,390,499,500};
 for(unsigned m=0;m<sizeof(modes)/sizeof(*modes);m++)for(unsigned f=0;f<sizeof(frames)/sizeof(*frames);f++)for(int accept=0;accept<2;accept++){
  init(modes[m]);RB_U16((u32)actor,0xF8)=accept?2:0;RB_S16((u32)actor,0x151D0)=frames[f];
  if(modes[m]==22&&frames[f]>=390)continue; /* Rope uses its own endpoint fixture below. */
  assert(randomizer_puppet_execute((u32)actor)==1);if(!accept&&(modes[m]==1||modes[m]==10||modes[m]==20))assert(order_calls==1&&!cam_calls);if(modes[m]==150)assert(!order_calls&&RB_S8((u32)actor,0x151CE)==-106);++checks;
 }
 for(int source=0;source<4;source++){init(2);if(source==0)move_state=2;if(source==1)light_state=2;if(source==2)camera_state=2;if(source==3)report_state=2;assert(randomizer_puppet_execute((u32)actor)==1&&next_calls==1);++checks;}
 init(0);RB_S16((u32)actor,0x14EB2)=32767;RB_S16((u32)actor,0x14F18)=-32768;RB_S16((u32)actor,0x4BE)=100;assert(randomizer_puppet_execute((u32)actor)==1);assert(RB_S16((u32)actor,0x14EB2)==-32768&&RB_S16((u32)actor,0x14F18)==32767&&RB_S16((u32)actor,0x4BE)==0);++checks;
 init(0);keese=0;RB_S16((u32)hio,0x176)=1;RB_S8((u32)actor,0x15750)=1;assert(randomizer_puppet_execute((u32)actor)==1&&create_calls==1&&random_calls==3);++checks;
 static unsigned char lines[16],out[60*12],sizes[60];
 for(int mode=0;mode<2;mode++){init(22);mutate_matrix=1;RB_PTR((u32)actor,0x149A8)=(u32)lines;RB_PTR((u32)lines,0)=(u32)out;RB_PTR((u32)lines,4)=(u32)sizes;RB_S16((u32)actor,0x151D0)=500;RB_S8((u32)actor,0x14C7C)=mode;memset(sizes,0,sizeof sizes);rb_last_himo_control((u32)actor);assert(positions==59&&rotations==118);for(int j=0;j<60;j++)assert(sizes[j]==10);assert(RB_F32((u32)out,0)==-15);++checks;}
 assert(rb_truncate(NAN)==0x80000000u&&rb_truncate(INFINITY)==0x7FFFFFFFu&&rb_truncate(-INFINITY)==0x80000000u);++checks;
 printf("puppet: %u independent state/camera/rope/ABI cases passed\n",checks);return 0;
}
'''
  with tempfile.TemporaryDirectory() as d:
   src=Path(d)/"test.c";src.write_text(code);exe=Path(d)/"test"
   subprocess.run([os.environ.get("CC","clang"),"-std=c11","-O2","-Wall","-Wextra","-Werror","-I",str(Path(__file__).parent),str(src),"-lm","-o",str(exe)],check=True)
   subprocess.run([str(exe)],check=True)
if __name__=="__main__":unittest.main()
