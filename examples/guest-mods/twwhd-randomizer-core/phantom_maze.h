/* Complete CC0 move0213B3AC from d_a_fganon.cpp (a874e1a9).
 * MIT flexible_item_locations: only PG2's Light Arrow query becomes stage8
 * chest0; PG3 continues to query genuine item ownership. */
#pragma once
#ifndef R_PGM
#define R_PGM(t,address) (*(volatile t*)(address))
#endif
WWHD_GAME_FUNC(0x025200D4,u32,pgm_game,(void));
WWHD_GAME_FUNC(0x024F8044,u32,pgm_camera,(void));
WWHD_GAME_FUNC(0x02514EE4,void,pgm_camera_type,(u32 camera,u32 name,u32 actor));
WWHD_GAME_FUNC(0x02520C0C,s32,pgm_item_owned,(u32 item));
WWHD_GAME_FUNC(0x02520864,s32,pgm_chest_open,(u32 stage,u32 chest));
WWHD_GAME_FUNC(0x020198D8,f64,pgm_random,(f64 range));
WWHD_GAME_FUNC(0x02019918,f64,pgm_random_signed,(f64 range));
WWHD_GAME_FUNC(0x028F4384,f64,pgm_sqrt,(f64 square));
WWHD_GAME_FUNC(0x028E8DD0,f64,pgm_square_magnitude,(u32 vector));
WWHD_GAME_FUNC(0x025F1884,void,pgm_y_rotation,(u32 matrix,s32 angle));
WWHD_GAME_FUNC(0x025F1BF4,void,pgm_x_rotation,(u32 matrix,s32 angle));
WWHD_GAME_FUNC(0x0200FCD8,void,pgm_transform,(u32 vector,u32 output));
WWHD_GAME_FUNC(0x0201AD78,void,pgm_vector_add,(u32 a,u32 result,u32 b));
WWHD_GAME_FUNC(0x0201ADE0,void,pgm_vector_subtract,(u32 a,u32 result,u32 b));
WWHD_GAME_FUNC(0x028E8D88,void,pgm_vector_add_inplace,(u32 a,u32 b,u32 result));
WWHD_GAME_FUNC(0x0200EDC8,void,pgm_decelerate,(u32 value,f32 ratio,f32 maximum));
WWHD_GAME_FUNC(0x0200ED84,void,pgm_calculate,(u32 value,f32 target,f32 ratio,f32 maximum));
WWHD_GAME_FUNC(0x0200F428,void,pgm_angle_calculate,(u32 value,s32 target,s32 ratio,s32 maximum));
WWHD_GAME_FUNC(0x020195B0,s32,pgm_atan,(f32 x,f32 z));
WWHD_GAME_FUNC(0x025D6894,s32,pgm_search_angle,(u32 actor,u32 target));
WWHD_GAME_FUNC(0x02134108,void,pgm_animation,(u32 actor,s32 index,f32 blend,u32 loop,f32 speed,s32 sound));
WWHD_GAME_FUNC(0x021345C8,void,pgm_appear,(u32 actor));
WWHD_GAME_FUNC(0x021346F0,void,pgm_disappear,(u32 actor,u32 which));
WWHD_GAME_FUNC(0x02134840,void,pgm_position_move,(u32 actor,u32 direct));
WWHD_GAME_FUNC(0x021349B8,void,pgm_flight_sound,(u32 actor));
WWHD_GAME_FUNC(0x02138670,void,pgm_shot2,(u32 actor));
WWHD_GAME_FUNC(0x02139194,void,pgm_spin2,(u32 actor));
WWHD_GAME_FUNC(0x02139680,void,pgm_down,(u32 actor));
WWHD_GAME_FUNC(0x02139AA0,void,pgm_start,(u32 actor));
WWHD_GAME_FUNC(0x02139E80,void,pgm_end,(u32 actor));
WWHD_GAME_FUNC(0x0213A228,void,pgm_last_end,(u32 actor));
/* Normal dispatch deliberately composes the existing hammer damage source port. */
WWHD_GAME_FUNC(0x0213A6FC,void,pgm_damage,(u32 actor));
WWHD_GAME_FUNC(0x025D57E0,void,pgm_delete,(u32 actor));
WWHD_GAME_FUNC(0x02520540,s32,pgm_reverb,(s32 room));
WWHD_GAME_FUNC(0x025E1A40,void,pgm_sound,(u32 id,u32 position,u32 volume,s32 reverb));
WWHD_GAME_FUNC(0x025E1AA4,void,pgm_monster_sound,(u32 id,u32 position,u32 process,u32 parameter,s32 reverb));
WWHD_GAME_FUNC(0x025E1A7C,void,pgm_ball_sound,(u32 id,u32 position,u32 volume,s32 reverb));
WWHD_GAME_FUNC(0x02008FEC,void,pgm_line_construct,(u32 line));
WWHD_GAME_FUNC(0x02008B4C,void,pgm_line_destroy,(u32 line,u32 flags));
WWHD_GAME_FUNC(0x024F1AFC,void,pgm_line_set,(u32 line,u32 start,u32 end,u32 actor));
WWHD_GAME_FUNC(0x02008860,s32,pgm_line_cross,(u32 background,u32 line));
WWHD_GAME_FUNC(0x028E90D4,void,pgm_matrix_copy,(u32 source,u32 destination));
WWHD_GAME_FUNC(0x025D5834,u32,pgm_actor_create,(s32 name,u32 params,u32 position,s32 room,u32 angle,u32 scale,s32 subtype,u32 function));
WWHD_GAME_FUNC(0x025D5218,u32,pgm_actor_judge,(u32 callback,u32 key));
WWHD_GAME_FUNC(0x025D9D0C,void,pgm_carry,(u32 actor,u32 mode));
WWHD_GAME_FUNC(0x025D9D24,void,pgm_carry_cancel,(u32 actor));
static f32 pgm_hio_float(u32 offset){return R_PGM(f32,(u32)&WWHD_GAME_DATA(0x10463D50,u8)+offset);}
static s16 pgm_hio_short(u32 offset){return R_PGM(s16,(u32)&WWHD_GAME_DATA(0x10463D50,u8)+offset);}
static u8 pgm_hio_byte(u32 offset){return R_PGM(u8,(u32)&WWHD_GAME_DATA(0x10463D50,u8)+offset);}
static f32 pgm_reg_float(u32 child,u32 index){return R_PGM(f32,(u32)&WWHD_GAME_DATA(0x1047B610,u8)+child*0x90+index*4);}
static s16 pgm_reg_short(u32 child,u32 index){return R_PGM(s16,(u32)&WWHD_GAME_DATA(0x1047B688,u8)+child*0x90+index*2);}
static u32 pgm_matrix(void){return WWHD_GAME_DATA(0x1018C7B0,u32);}
static s32 pgm_integer(f64 value){if(value!=value||value<=-2147483648.0)return(s32)0x80000000u;if(value>=2147483648.0)return 0x7FFFFFFF;return(s32)value;}
/* Match Gekko fmadds explicitly; the C99 fmaf intrinsic would become an
 * unavailable libc service for clang's 750 target. Restrict contraction to
 * this single expression so other native rounding points remain separate. */
static f32 pgm_fma(f32 a,f32 b,f32 c){
#pragma clang fp contract(fast)
 return a*b+c;
}
static s32 pgm_stopped(u32 actor){const u32 morf=R_PGM(u32,actor+0x3DC);return(R_PGM(u8,morf+0xA7)&1)||R_PGM(f32,morf+0x98)==0.0f;}
static void pgm_voice(u32 actor,u32 id){if(actor+0x37C){const s32 room=R_PGM(s8,actor+0x326);const u32 process=actor?R_PGM(u32,actor+4):0xFFFFFFFFu;pgm_monster_sound(id,actor+0x37C,process,0,pgm_reverb(room));}}
static void pgm_effect_sound(u32 actor,u32 id,u32 volume){if(actor&&actor+0x37C)pgm_sound(id,actor+0x37C,volume,pgm_reverb(R_PGM(s8,actor+0x326)));}
static void pgm_copy_vector(u32 to,u32 from){for(u32 i=0;i<3;i++)R_PGM(u32,to+4*i)=R_PGM(u32,from+4*i);}
static void pgm_line_init(unsigned int* line){
 pgm_line_construct((u32)line);u8* bytes=(u8*)line;for(u32 i=0;i<7;i++)bytes[0x5C+i]=0;
 line[0x68/4]=1;line[1]=(unsigned int)((u32)line+0x64);line[0]=(unsigned int)((u32)line+0x58);
 line[0x10/4]=0x1000ED34;line[0x64/4]=0x1000ED54;line[0x58/4]=0x1000ED64;line[0x20/4]=0x1000ED44;
}
static void pgm_line_finish(unsigned int* line){line[0x58/4]=0x1000ED64;line[0x64/4]=0x1000EC34;line[0x20/4]=0x1000EC24;pgm_line_destroy((u32)line,0);}
static u32 pgm_joint_matrix(u32 model,u32 index){const u32 block=R_PGM(u32,model+0x2C);R_PGM(u16,block+4)|=0x10;return R_PGM(u32,block+0x10)+index*0x30;}
static void pgm_standby(u32 actor){
 const u32 player=R_PGM(u32,pgm_game()+0x5B2C);R_PGM(s16,actor+0x5E2)=3;
 switch(R_PGM(s16,actor+0x5BC)){
 case -1:{
  if((R_PGM(u8,actor+0x3D8)==2&&!pgm_item_owned(0x36))||
     (R_PGM(u8,actor+0x3D8)==1&&pgm_chest_open(8,0))){R_PGM(f32,actor+0x318)=-20000.0f;break;}
  const f32 z=R_PGM(f32,player+0x31C)-R_PGM(f32,actor+0x2F4),x=R_PGM(f32,player+0x314)-R_PGM(f32,actor+0x2EC);
  const f64 distance=pgm_sqrt(pgm_fma(x,x,z*z));
  if(distance<(f32)R_PGM(u8,actor+0x3D9)*10.0f){R_PGM(s16,actor+0x5D8)=pgm_hio_short(4);R_PGM(s8,actor+0xDBD)=22;R_PGM(s16,actor+0x5BC)=1;R_PGM(u32,actor+0x2E0)|=0x20;}
  break;}
 case 0:R_PGM(s16,actor+0x5D8)=pgm_hio_short(4);R_PGM(s16,actor+0x5BC)++;/* fall through */
 case 1:if(R_PGM(s16,actor+0x5D8)==0){R_PGM(f32,actor+0x8D0)=0;R_PGM(f32,actor+0x8CC)=0;R_PGM(s16,actor+0x5BA)=1;R_PGM(f32,actor+0x8C8)=0;R_PGM(s16,actor+0x5BC)=0;}break;
 }
}
#include "phantom_maze_actions.h"
WWHD_REPLACE(0x0213B3AC,s32,randomizer_phantom_move,(u32 actor)){
 u32 play=pgm_game();
 if(!(R_PGM(u32,play+0x5CD8)&0x10000)){
  play=pgm_game();if(!(R_PGM(u32,play+0x5CD8)&0x100000)&&R_PGM(s16,actor+0x5BA)!=0){
   const u32 type=R_PGM(s16,actor+0x5BA)==10?0x1000ED94:(R_PGM(u8,actor+0x3D8)==1?0x1000EDAC:0x1000EDA0);
   pgm_camera_type(pgm_camera(),type,0);
  }
 }
 if(R_PGM(u8,actor+0x3D8)==0&&R_PGM(s16,actor+0x5BA)<20){
  const u32 player=R_PGM(u32,pgm_game()+0x5B2C);
  if(R_PGM(s16,actor+0x5BA)==0&&R_PGM(f32,player+0x318)<710.0f)return 0;
  if(R_PGM(s16,actor+0x5BA)!=0&&R_PGM(s16,actor+0x5BA)!=2&&R_PGM(f32,player+0x318)<710.0f){R_PGM(s16,actor+0x5BA)=2;R_PGM(s16,actor+0x5BC)=0;}
 }
 s32 result=0;
 switch(R_PGM(s16,actor+0x5BA)){
 case 0:pgm_standby(actor);break;
 case 1:pgm_emerge(actor);break;
 case 2:pgm_vanish(actor);break;
 case 3:pgm_fail(actor);result=1;break;
 case 5:pgm_fly(actor,0);result=1;break;
 case 6:pgm_shot(actor);result=1;break;
 case 7:pgm_spin(actor);result=1;break;
 case 8:pgm_down(actor);result=1;break;
 case 9:pgm_fly(actor,1);result=1;break;
 case 10:pgm_spin2(actor);break;
 case 11:pgm_shot2(actor);result=1;break;
 case 20:pgm_start(actor);break;
 case 21:pgm_end(actor);break;
 case 22:pgm_last_end(actor);break;
 }
 pgm_damage(actor);R_PGM(s8,actor+0x8C0)=0;
 if(actor+0xAF0){for(u32 i=0;i<3;i++)R_PGM(f32,actor+0x314+i*4)=R_PGM(f32,actor+0x314+i*4)+R_PGM(f32,actor+0xAF0+i*4);}
 if(R_PGM(f32,actor+0x5E4)>0.01f){
  f32 offset[3]={0.0f,0.0f,R_PGM(f32,actor+0x5E4)},transformed[3];
  pgm_y_rotation(pgm_matrix(),R_PGM(s16,actor+0x5E8));pgm_x_rotation(pgm_matrix(),R_PGM(s16,actor+0x5EA));
  pgm_transform((u32)offset,(u32)transformed);pgm_vector_add_inplace(actor+0x314,(u32)transformed,actor+0x314);
  pgm_decelerate(actor+0x5E4,1.0f,7.0f);
 }
 return result;
}
