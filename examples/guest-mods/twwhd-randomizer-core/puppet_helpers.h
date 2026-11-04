/* Complete public CC0 Puppet Ganon Execute support; native pointer APIs. */
#pragma once
#define RB_U8(a,o) (*(volatile u8*)((a)+(o)))
#define RB_S8(a,o) (*(volatile s8*)((a)+(o)))
#define RB_U16(a,o) (*(volatile u16*)((a)+(o)))
#define RB_S16(a,o) (*(volatile s16*)((a)+(o)))
#define RB_U32(a,o) (*(volatile unsigned int*)((a)+(o)))
#define RB_F32(a,o) (*(volatile f32*)((a)+(o)))
#ifndef RB_PTR
#define RB_PTR(a,o) (*(volatile u32*)((a)+(o)))
#endif
typedef struct{f32 x,y,z;} rb_vector;
typedef struct{s16 x,y,z;} rb_angle;
typedef struct{unsigned int x,y,z;} rb_words;
#define RB_VEC(a,o) (*(volatile rb_vector*)((a)+(o)))
#define RB_ANGLE(a,o) (*(volatile rb_angle*)((a)+(o)))
#define RB_WORD_PTR(p) ((volatile rb_words*)(p))
WWHD_GAME_FUNC(0x02555D0C,u32,rb_env,(void));
WWHD_GAME_FUNC(0x025626A4,void,rb_lighting,(u32 environment,u32 type,u32 position,u32 tev));
WWHD_GAME_FUNC(0x025DE508,u32,rb_search,(u32 callback,u32 actor));
WWHD_GAME_FUNC(0x0208857C,void,rb_move,(u32 actor));
WWHD_GAME_FUNC(0x02514F2C,void,rb_camera_stop,(u32 camera));
WWHD_GAME_FUNC(0x02514F38,void,rb_camera_start,(u32 camera));
WWHD_GAME_FUNC(0x02515280,void,rb_camera_trim,(u32 camera,s32 trim));
WWHD_GAME_FUNC(0x02514FE8,void,rb_camera_set,(u32 camera,u32 center,u32 eye,s16 bank,f32 fov));
WWHD_GAME_FUNC(0x0259169C,void,rb_meter_show,(void));
WWHD_GAME_FUNC(0x0200ED84,void,rb_calc_native,(u32 value,f32 target,f32 rate,f32 step));
WWHD_GAME_FUNC(0x0200EDC8,void,rb_zero_native,(u32 value,f32 rate,f32 step));
WWHD_GAME_FUNC(0x0200F428,void,rb_angle_native,(u32 value,s16 target,s16 rate,s16 step));
WWHD_GAME_FUNC(0x025F1884,void,rb_rotate_y_start,(u32 matrix,s16 angle));
WWHD_GAME_FUNC(0x025F18EC,void,rb_rotate_x_start,(u32 matrix,s16 angle));
WWHD_GAME_FUNC(0x025F1C28,void,rb_rotate_y,(u32 matrix,s16 angle));
WWHD_GAME_FUNC(0x0200FCD8,void,rb_position_native,(u32 source,u32 out));
WWHD_GAME_FUNC(0x02520540,s32,rb_reverb,(s32 room));
WWHD_GAME_FUNC(0x025E1A40,void,rb_sound,(u32 id,u32 position,u32 parameter,s32 reverb));
WWHD_GAME_FUNC(0x025E1A7C,void,rb_monster_sound,(u32 id,u32 position,u32 parameter,s32 reverb));
WWHD_GAME_FUNC(0x025E18EC,void,rb_bgm,(u32 id));
WWHD_GAME_FUNC(0x025D7B24,s32,rb_order,(u32 actor,u32 type,u32 flag,u32 argument));
WWHD_GAME_FUNC(0x025A847C,u32,rb_particle_native,(u32 controller,u32 group,u32 effect,u32 position,u32 angle,u32 scale,u32 alpha,u32 callback,s32 room,u32 color,u32 color2,u32 scale2));
WWHD_GAME_FUNC(0x025CB374,u8,rb_shock_native,(u32 vibration,s32 strength,s32 flags,u32 vector));
WWHD_GAME_FUNC(0x025B8B68,void,rb_event_on,(u32 save,u32 flag));
WWHD_GAME_FUNC(0x0207D008,s32,rb_keese_count,(u32 actor));
WWHD_GAME_FUNC(0x020198D8,f64,rb_random_f,(f64 limit));
WWHD_GAME_FUNC(0x02019918,f64,rb_random_fx,(f64 limit));
WWHD_GAME_FUNC(0x025D5834,u32,rb_create,(u32 process,u32 parameters,u32 position,s32 room,u32 angle,u32 scale,s32 subtype,u32 callback));
WWHD_GAME_FUNC(0x0252012C,void,rb_next_stage,(u32 stage,s16 point,s8 room,s8 layer,f32 speed,u32 mode,s32 setpoint,s8 wipe));
WWHD_GAME_FUNC(0x020195B0,s16,rb_atan,(f64 y,f64 x));
WWHD_GAME_FUNC(0x028F4384,f64,rb_sqrt,(f64 value));
WWHD_GAME_FUNC(0x027EC9E8,void,rb_report,(s32 x,s32 y,u32 format,...));
static f32 rb_add(f32 a,f32 b){if(a!=a)return a;if(b!=b)return b;return a+b;}
static f32 rb_add_wide(f64 a,f32 b){return(f32)(a+(f64)b);}
static f32 rb_sub(f32 a,f32 b){if(a!=a)return a;if(b!=b)return b;return a-b;}
static f32 rb_mul(f32 a,f32 b){if(a!=a)return a;if(b!=b)return b;return a*b;}
static f32 rb_fma(f32 a,f32 b,f32 c){
#pragma clang fp contract(fast)
return a*b+c;
}
static unsigned int rb_truncate(f32 v){if(v!=v)return 0x80000000u;if(v>=2147483648.0f)return 0x7FFFFFFFu;if(v<=-2147483648.0f)return 0x80000000u;return(unsigned int)(s32)v;}
static f32 rb_reg_f(u32 child,u32 i){return RB_F32((u32)&WWHD_GAME_DATA(0x1047B610,u8),0x90*child+4*i);}
static s16 rb_reg_s(u32 child,u32 i){return RB_S16((u32)&WWHD_GAME_DATA(0x1047B688,u8),0x90*child+2*i);}
static f32 rb_sine(s32 a){return RB_F32((u32)&WWHD_GAME_DATA(0x104A44F8,u8),((u16)a>>3)*8);}
static f32 rb_cosine(s32 a){return RB_F32((u32)&WWHD_GAME_DATA(0x104A44F8,u8),((u16)a>>3)*8+4);}
static u32 rb_matrix(void){return WWHD_GAME_DATA(0x1018C7B0,u32);}
static void rb_calc(volatile f32* value,f32 target,f32 rate,f32 step){rb_calc_native((u32)value,target,rate,step);}
static void rb_zero(volatile f32* value,f32 rate,f32 step){rb_zero_native((u32)value,rate,step);}
static void rb_angle_calc(volatile s16* value,s16 target,s16 rate,s16 step){rb_angle_native((u32)value,target,rate,step);}
static void rb_position(volatile rb_vector* source,volatile rb_vector* out){rb_position_native((u32)source,(u32)out);}
static void rb_camera(u32 camera,rb_vector* center,rb_vector* eye,s16 bank,f32 fov){rb_camera_set(camera,(u32)center,(u32)eye,bank,fov);}
static void rb_particle(u32 effect,volatile rb_vector* position,u32 angle,volatile rb_vector* scale){const u32 controller=RB_PTR(wwhd_dComIfG_getGameInfo(),0x5AB0);(void)rb_particle_native(controller,0,effect,(u32)position,angle,(u32)scale,255,0,-1,0,0,0);}
static void rb_shock(u32 vibration,s32 strength,s32 flags,rb_vector* vector){(void)rb_shock_native(vibration,strength,flags,(u32)vector);}
static void rb_event_reset(void){const u32 play=wwhd_dComIfG_getGameInfo();RB_U16(play,0x52B8)|=8;}
