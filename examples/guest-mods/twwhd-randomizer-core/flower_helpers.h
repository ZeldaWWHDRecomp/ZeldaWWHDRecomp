/* Public CC0 d_a_kokiie.cpp/.h, c_sxyz.cpp and HD register/matrix bindings. */
#pragma once
#define RF8(a,o) (*(volatile u8*)((a)+(o)))
#define RFS8(a,o) (*(volatile s8*)((a)+(o)))
#define RF16(a,o) (*(volatile u16*)((a)+(o)))
#define RFS16(a,o) (*(volatile s16*)((a)+(o)))
#define RF32(a,o) (*(volatile unsigned int*)((a)+(o)))
#define RFF(a,o) (*(volatile f32*)((a)+(o)))
#ifndef RFP
#define RFP(a,o) (*(volatile u32*)((a)+(o)))
#endif
typedef struct {f32 x,y,z;} randomizer_flower_vector;
WWHD_GAME_FUNC(0x0200ED84,void,rf_calc,(u32 value,f32 target,f32 rate,f32 step));
WWHD_GAME_FUNC(0x0200EDC8,void,rf_zero,(u32 value,f32 rate,f32 step));
WWHD_GAME_FUNC(0x0200F428,void,rf_angle,(u32 value,s16 target,s16 rate,s16 step));
WWHD_GAME_FUNC(0x02520C0C,s32,rf_inventory,(u8 item));
WWHD_GAME_FUNC(0x0201A478,u32,rf_angles_ctor,(u32 out,s16 x,s16 y,s16 z));
WWHD_GAME_FUNC(0x0201AD78,void,rf_vector_add,(u32 first,randomizer_flower_vector* out,u32 second));
WWHD_GAME_FUNC(0x025B9E38,void,rf_switch_on,(u32 save,s32 bit,s32 room));
WWHD_GAME_FUNC(0x02520540,s32,rf_reverb,(s32 room));
WWHD_GAME_FUNC(0x025E1A40,void,rf_sound,(u32 sound,u32 position,u32 parameter,s32 reverb));
WWHD_GAME_FUNC(0x025CB374,u8,rf_shock_native,(u32 vibration,s32 strength,s32 flags,randomizer_flower_vector* direction));
WWHD_GAME_FUNC(0x025A847C,u32,rf_particle_native,(u32 control,u32 group,u32 id,u32 position,u32 angle,u32 scale,u32 alpha,u32 callback,s32 setup,u32 primary,u32 environment,u32 scale2d));
WWHD_GAME_FUNC(0x0200FAD8,void,rf_translate,(f32 x,f32 y,f32 z,u32 concat));
WWHD_GAME_FUNC(0x025F1884,void,rf_y_start,(u32 matrix,s16 angle));
WWHD_GAME_FUNC(0x025F1C28,void,rf_y_rotate,(u32 matrix,s16 angle));
WWHD_GAME_FUNC(0x025F1BF4,void,rf_x_rotate,(u32 matrix,s16 angle));
WWHD_GAME_FUNC(0x025F1C5C,void,rf_z_rotate,(u32 matrix,s16 angle));
WWHD_GAME_FUNC(0x0200FCD8,void,rf_position,(randomizer_flower_vector* source,u32 result));
WWHD_GAME_FUNC(0x0200FCF0,void,rf_push,(void));
WWHD_GAME_FUNC(0x0200FD38,void,rf_pull,(void));
WWHD_GAME_FUNC(0x028E90D4,void,rf_matrix_copy,(u32 source,u32 result));
WWHD_GAME_FUNC(0x024F43DC,void,rf_background_move,(u32 background));
WWHD_GAME_FUNC(0x025D7B24,s32,rf_order_event,(u32 actor,u32 type,u32 id,u32 argument));
WWHD_GAME_FUNC(0x02514F2C,void,rf_camera_stop,(u32 camera));
WWHD_GAME_FUNC(0x02514F38,void,rf_camera_start,(u32 camera));
WWHD_GAME_FUNC(0x02515280,void,rf_camera_trim,(u32 camera,s32 trim));
WWHD_GAME_FUNC(0x0251510C,void,rf_camera_reset,(u32 camera,randomizer_flower_vector* center,randomizer_flower_vector* eye));
WWHD_GAME_FUNC(0x02514FE8,void,rf_camera_set,(u32 camera,randomizer_flower_vector* center,randomizer_flower_vector* eye,s16 bank,f32 fov));
WWHD_GAME_FUNC(0x025E1988,void,rf_sound_global,(u32 sound));
WWHD_GAME_FUNC(0x027EC9E8,void,rf_report,(s32 x,s32 y,u32 format,s32 count));
static f32 rf_reg_f(u32 child,u32 i){return RFF((u32)&WWHD_GAME_DATA(0x1047B610,u8),0x90*child+4*i);}
static s16 rf_reg_s(u32 child,u32 i){return RFS16((u32)&WWHD_GAME_DATA(0x1047B688,u8),0x90*child+2*i);}
static u32 rf_matrix(void){return WWHD_GAME_DATA(0x1018C7B0,u32);}
static f32 rf_sine(s32 angle){return RFF((u32)&WWHD_GAME_DATA(0x104A44F8,u8),((u16)angle>>3)*8);}
static f32 rf_cosine(s32 angle){return RFF((u32)&WWHD_GAME_DATA(0x104A44F8,u8),((u16)angle>>3)*8+4);}
static unsigned int rf_truncate(f32 value){
    if(value!=value)return 0x80000000u;
    if(value>=2147483648.0f)return 0x7FFFFFFFu;
    if(value<=-2147483648.0f)return 0x80000000u;
    return (unsigned int)(s32)value;
}
/* Expand public csXyz_Add0201A4DC to avoid its GHS six-byte R3/R4 ABI.
 * Keep native constructor0201A478 (including callbacks/redirected pointer),
 * x/z/y wrapping sums, then snapshot all returned halfwords before stores.
 * The outer Add entry observers are not called by this source expansion. */
static void rf_add_angles(u32 first,u32 second,u32 result){
    const s16 ax=RFS16(first,0),bx=RFS16(second,0),bz=RFS16(second,4),az=RFS16(first,4),by=RFS16(second,2),ay=RFS16(first,2);
    u16 temporary[3];
    const u32 value=rf_angles_ctor((u32)temporary,(s16)(u16)((s32)ax+bx),(s16)(u16)((s32)ay+by),(s16)(u16)((s32)az+bz));
    const u16 x=RF16(value,0),y=RF16(value,2),z=RF16(value,4);
    RF16(result,0)=x;RF16(result,2)=y;RF16(result,4)=z;
}
static void rf_raw_vector(u32 out,u32 in){const unsigned int x=RF32(in,0),y=RF32(in,4),z=RF32(in,8);RF32(out,0)=x;RF32(out,4)=y;RF32(out,8)=z;}
static void rf_model_matrix(u32 source,u32 model){unsigned int data[12];for(u32 i=0;i<12;i++)data[i]=RF32(source,4*i);for(u32 i=0;i<12;i++)RF32(model,0xC8+4*i)=data[i];}
static void rf_sound_current(u32 actor,u32 sound){const u32 position=actor+0x314;if(position){const s32 reverb=rf_reverb(RFS8(actor,0x326));rf_sound(sound,position,0,reverb);}}
static void rf_shock(u32 child,u32 index,s32 extra){const u32 vibration=wwhd_dComIfG_getGameInfo()+0x599C;const s32 strength=rf_reg_s(child,index)+extra;randomizer_flower_vector direction={0,1,0};(void)rf_shock_native(vibration,strength,-0x21,&direction);}
static void rf_particle(u32 id,u32 position){const u32 control=RFP(wwhd_dComIfG_getGameInfo(),0x5AB0);(void)rf_particle_native(control,0,(u32)(s32)(s16)(u16)id,position,0,0,255,0,-1,0,0,0);}
