"""Complete flower consumer: rope transitions, callback isolation and GHS Add expansion."""
from pathlib import Path
import os, subprocess, tempfile, unittest
class Flower(unittest.TestCase):
 def test_consumer(self):
  code=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <math.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef int8_t s8;typedef uint16_t u16;typedef int16_t s16;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static u8 actor[0x600],model[0x200],play[0x6000],camera_process[0x300],regs[0x1000],trig[0x10000],savebytes[64];
static unsigned int matrix_words[12];static u32 matrixptr=(u32)matrix_words,saveptr=(u32)savebytes;
static u8 check_table[5]={1,2,4,8,16};static s16 yaw_table[32],pitch_table[32],offset_table[32];
static void* memory(u32 p){if(p==1)return&saveptr;if(p==0x1018C7B0)return&matrixptr;if(p==0x1047B610)return regs;if(p==0x1047B688)return regs+0x78;if(p==0x104A44F8)return trig;if(p==0x101B868C)return check_table;if(p==0x101B86B4)return yaw_table;if(p==0x101B86F4)return pitch_table;if(p==0x101B8734)return offset_table;assert(p==0x100134D4);return(void*)"K SUB COUNT %d";}
#define WWHD_GAME_DATA(a,t) (*(t*)memory(a))
static u32 wwhd_dComIfG_getGameInfo(void);
/* Host pointers are 64-bit; model/camera guest pointer cells remain 32-bit in production. */
static u32 pointer_cells[4];
static u32* pointer_cell(u32 a,u32 o){if(a==(u32)actor&&o==0x3B8)return pointer_cells;if(a==(u32)actor&&o==0x48C)return pointer_cells+1;if(a==(u32)play&&o==0x5AF8)return pointer_cells+2;if(a==(u32)play&&o==0x5AB0)return pointer_cells+3;assert(0);return 0;}
#define RFP(a,o) (*pointer_cell((a),(o)))
#include "forbidden_woods_flower.h"
static unsigned inventory_calls,orders,pushes,pulls,positions,stops,starts,trims,sets,reports,resets,particles,switches,sounds,shocks,ctor_calls;
static s32 ownership;static unsigned query_mutation,ctor_redirect,ctor_mutation,particle_mutation;static unsigned expected_mask;
static s16 expected_pitch,expected_yaw;static u16 redirect_angles[8];static u32 first_input,second_input;
static u32 wwhd_dComIfG_getGameInfo(void){return(u32)play;}
void rf_calc(u32 value,f32 target,f32 rate,f32 step){(void)target;(void)rate;(void)step;if(value==(u32)actor+0x3EC){assert(expected_pitch==pitch_table[expected_mask]);pitch_table[expected_mask]=9999;yaw_table[expected_mask]=9998;}}
void rf_zero(u32 value,f32 rate,f32 step){(void)value;(void)rate;(void)step;}
void rf_angle(u32 value,s16 target,s16 rate,s16 step){(void)rate;if(value==(u32)actor+0x450&&RFS16((u32)actor,0x3B6)==1&&inventory_calls){assert(target==expected_pitch);if(query_mutation)assert(step==(s16)(u16)35000);}if(value==(u32)actor+0x44E&&inventory_calls)assert(target==expected_yaw);}
s32 rf_inventory(u8 item){assert(item==0x2D&&expected_mask==31);++inventory_calls;assert(RF8((u32)actor,0x3A1)==77&&RF16((u32)actor,0x3B6)==0&&RF16((u32)actor,0x494)==0);if(query_mutation)RFF((u32)actor,0x454)=3.5f;return ownership;}
u32 rf_angles_ctor(u32 out,s16 x,s16 y,s16 z){++ctor_calls;if(ctor_mutation){assert(x==-32768&&y==32767&&z==-1);for(unsigned i=0;i<3;i++){RF16(first_input,2*i)=0xFFFF;RF16(second_input,2*i)=0;}}if(ctor_redirect)return(u32)redirect_angles;RF16(out,2)=(u16)y;RF16(out,0)=(u16)x;RF16(out,4)=(u16)z;return out;}
void rf_vector_add(u32 first,randomizer_flower_vector* out,u32 second){out->x=RFF(first,0)+RFF(second,0);out->y=RFF(first,4)+RFF(second,4);out->z=RFF(first,8)+RFF(second,8);}
void rf_switch_on(u32 save,s32 bit,s32 room){assert(save==saveptr+0x20&&bit==7&&room==-2);++switches;}
s32 rf_reverb(s32 room){assert(room==-2);return 17;}
void rf_sound(u32 sound,u32 position,u32 parameter,s32 reverb){assert((sound==0x6A0E||sound==0x6951)&&position==(u32)actor+0x314&&!parameter&&reverb==17);++sounds;}
u8 rf_shock_native(u32 vibration,s32 strength,s32 flags,randomizer_flower_vector* direction){assert(vibration==(u32)play+0x599C&&(strength==4||strength==7)&&flags==-0x21);assert(direction->x==0&&direction->y==1&&direction->z==0);++shocks;return 1;}
u32 rf_particle_native(u32 control,u32 group,u32 id,u32 position,u32 angle,u32 scale,u32 alpha,u32 callback,s32 setup,u32 primary,u32 environment,u32 scale2d){assert(control==0x1234&&!group&&id==(u32)(s32)(s16)(u16)(0x82A4+particles)&&!angle&&!scale&&alpha==255&&!callback&&setup==-1&&!primary&&!environment&&!scale2d);if(particles!=2)assert(RFF(position,4)==1170);else assert(position==(u32)actor+0x314);++particles;if(particle_mutation&&particles==1)RFF((u32)actor,0x318)=9000;return 1;}
void rf_translate(f32 x,f32 y,f32 z,u32 concat){(void)x;(void)y;(void)z;assert(!concat);}
void rf_y_start(u32 matrix,s16 angle){(void)angle;assert(matrix==matrixptr);}
void rf_y_rotate(u32 matrix,s16 angle){(void)angle;assert(matrix==matrixptr);}
void rf_x_rotate(u32 matrix,s16 angle){(void)angle;assert(matrix==matrixptr);}
void rf_z_rotate(u32 matrix,s16 angle){(void)angle;assert(matrix==matrixptr);}
void rf_position(randomizer_flower_vector* source,u32 result){++positions;RFF(result,0)=source->x;RFF(result,4)=source->y;RFF(result,8)=source->z;}
void rf_push(void){++pushes;}void rf_pull(void){++pulls;}
void rf_matrix_copy(u32 source,u32 result){assert(source==matrixptr&&result==(u32)actor+0x45C);memmove((void*)result,(void*)source,48);}
void rf_background_move(u32 background){assert(background==0x12345678);}
s32 rf_order_event(u32 a,u32 type,u32 id,u32 argument){assert(a==(u32)actor&&type==2&&id==0xFFFFFFFFu&&!argument);++orders;return 0;}
void rf_camera_stop(u32 camera){assert(camera==(u32)camera_process+0x248);++stops;}
void rf_camera_start(u32 camera){assert(camera==(u32)camera_process+0x248);++starts;}
void rf_camera_trim(u32 camera,s32 trim){assert(camera==(u32)camera_process+0x248&&(trim==0||trim==2));++trims;}
void rf_camera_reset(u32 camera,randomizer_flower_vector* center,randomizer_flower_vector* eye){assert(camera==(u32)camera_process+0x248&&center&&eye&&RF16((u32)actor,0x494)==0&&(RF16((u32)play,0x52B8)&8));++resets;}
void rf_camera_set(u32 camera,randomizer_flower_vector* center,randomizer_flower_vector* eye,s16 bank,f32 fov){(void)bank;(void)fov;assert(camera==(u32)camera_process+0x248&&center&&eye);++sets;}
void rf_sound_global(u32 sound){assert(sound==0x806);++sounds;}
void rf_report(s32 x,s32 y,u32 format,s32 count){assert(x==0x19A&&y==0x1AE&&format&&count==RFS16((u32)actor,0x496));++reports;}
static void reset(void){memset(actor,0,sizeof actor);memset(play,0,sizeof play);memset(regs,0,sizeof regs);inventory_calls=orders=pushes=pulls=positions=stops=starts=trims=sets=reports=resets=particles=switches=sounds=shocks=ctor_calls=0;ctor_redirect=ctor_mutation=query_mutation=particle_mutation=0;RFP((u32)actor,0x3B8)=(u32)model;RFP((u32)actor,0x48C)=0x12345678;RFP((u32)play,0x5AF8)=(u32)camera_process;RFP((u32)play,0x5AB0)=0x1234;RFS8((u32)actor,0x326)=-2;RF8((u32)actor,0x3A1)=77;RF8((u32)actor,0x3BE)=7;RFF((u32)actor,0x318)=5000;for(unsigned i=0;i<32;i++){pitch_table[i]=(s16)(i*100);yaw_table[i]=(s16)(i*300);offset_table[i]=(s16)i;}for(unsigned i=0;i<12;i++)matrix_words[i]=0x7F800001+i;}
int main(void){
 for(unsigned i=0;i<8192;i++){*(f32*)(trig+i*8)=0;*(f32*)(trig+i*8+4)=1;}
 unsigned scenarios=0;const u8 states[]={0,1,2,3,5,255};const s32 ownerships[]={-3,0,1,2};
 for(unsigned pattern=0;pattern<7776;pattern++)for(unsigned inv=0;inv<4;inv++){
  reset();ownership=ownerships[inv];unsigned p=pattern;expected_mask=0;
  for(unsigned i=0;i<5;i++){const u8 state=states[p%6];p/=6;RF8((u32)actor,0x448+i)=state;if(state!=0&&state!=2)expected_mask|=1<<i;}
  expected_pitch=pitch_table[expected_mask];expected_yaw=yaw_table[expected_mask];query_mutation=pattern&1;
  assert(randomizer_flower_execute((u32)actor)==1);assert(inventory_calls==(expected_mask==31));
  assert(RF8((u32)actor,0x3A1)==(expected_mask==31?0:77));assert(RF16((u32)actor,0x3B6)==(expected_mask==31?1:0));assert(RF16((u32)actor,0x494)==(expected_mask==31?1:0));assert(orders==(expected_mask==31));assert(pushes==5&&pulls==5&&positions==5);assert(!memcmp(model+0xC8,matrix_words,48));++scenarios;
 }
 assert(scenarios==31104);
 /* Native constructor callbacks may mutate inputs, redirect output and alias caller destination. */
 reset();u16 first[3]={32767,0x8000,0},second[3]={1,0xFFFF,0xFFFF};first_input=(u32)first;second_input=(u32)second;ctor_mutation=ctor_redirect=1;redirect_angles[0]=0x1234;redirect_angles[1]=0xFEDC;redirect_angles[2]=0x8000;
 rf_add_angles((u32)first,(u32)second,(u32)(redirect_angles+1));assert(ctor_calls==1&&redirect_angles[1]==0x1234&&redirect_angles[2]==0xFEDC&&redirect_angles[3]==0x8000);
 assert(rf_truncate(NAN)==0x80000000&&rf_truncate(INFINITY)==0x7FFFFFFF&&rf_truncate(-INFINITY)==0x80000000&&rf_truncate(-1.9f)==0xFFFFFFFF);
 reset();RF16((u32)actor,0x3B6)=1;RF16((u32)actor,0x496)=27;assert(randomizer_flower_execute((u32)actor)==1);assert(switches==1&&sounds==1&&shocks==1);
 reset();RF16((u32)actor,0x3B6)=1;RF16((u32)actor,0x496)=64;particle_mutation=1;assert(randomizer_flower_execute((u32)actor)==1);assert(particles==4);
 reset();RF16((u32)actor,0x3B6)=1;RFF((u32)actor,0x318)=1000;RFF((u32)actor,0x340)=-30;assert(randomizer_flower_execute((u32)actor)==1);assert(shocks==1&&sounds==1&&RFF((u32)actor,0x340)==0&&RFF((u32)actor,0x4B8)==90);
 reset();RF16((u32)actor,0x3B6)=2;RF16((u32)actor,0x494)=2;RF16((u32)actor,0x496)=150;assert(randomizer_flower_execute((u32)actor)==1);assert(resets==1&&starts==1&&trims==1&&!sets&&!reports);
 reset();RF16((u32)actor,0x3B6)=2;RF16((u32)actor,0x494)=1;RF16((u32)actor,0xF8)=2;assert(randomizer_flower_execute((u32)actor)==1);assert(stops==1&&trims==1&&sets==1&&reports==1&&positions==6);
 reset();RF16((u32)actor,0x3B6)=0xFFFF;RF16((u32)actor,0x3B4)=0x7FFF;assert(randomizer_flower_execute((u32)actor)==1);assert(RF16((u32)actor,0x3B4)==0x8000&&!inventory_calls);
 return 0;
}
'''
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);(p/'test.c').write_text(code)
   subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror','-I',str(Path(__file__).resolve().parent),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
