/* CC0 a874e1a9 Magtail execution helpers and canonical native contracts.
 * randomF/sqrt/distance retain F1 double precision until the caller's explicit
 * single operation or store. Debug/actor floats themselves remain single.
 * Short-angle Add is expanded from CC0 c_sxyz.cpp0201A4DC because its6-byte
 * R3/R4 return is outside the scalar SDK ABI. Retain the real constructor and
 * its returned pointer, then capture all halfwords before caller writes.
 * The expanded Add entry observer is not reproduced; constructor observers are.
 * Vector copies preserve object bytes, including NaN payloads and signed zero. */
#pragma once
#define RMT8(a,o) (*(volatile u8*)((a)+(o)))
#define RMTS8(a,o) (*(volatile s8*)((a)+(o)))
#define RMT16(a,o) (*(volatile s16*)((a)+(o)))
#define RMTU16(a,o) (*(volatile u16*)((a)+(o)))
#define RMT32(a,o) (*(volatile unsigned int*)((a)+(o)))
#define RMTS32(a,o) (*(volatile s32*)((a)+(o)))
#define RMTF(a,o) (*(volatile f32*)((a)+(o)))
#ifndef RMTPTR
#define RMTPTR(a,o) (*(volatile u32*)((a)+(o)))
#endif
typedef struct{f32 x,y,z;} rmt_vector;
typedef struct{s16 x,y,z;} rmt_angle;
#define RMTV(a,o) (*(volatile rmt_vector*)((a)+(o)))
#define RMTA(a,o) (*(volatile rmt_angle*)((a)+(o)))
WWHD_GAME_FUNC(0x02008974,f64,rmt_ground_cross,(u32 background,u32 check));
WWHD_GAME_FUNC(0x02008860,s32,rmt_line_cross,(u32 background,u32 line));
WWHD_GAME_FUNC(0x020084C8,u32,rmt_plane,(u32 background,s32 index,u32 polygon));
WWHD_GAME_FUNC(0x02008B4C,void,rmt_line_destroy,(u32 line,s32 flag));
WWHD_GAME_FUNC(0x02008DAC,void,rmt_ground_destroy,(u32 check,s32 flag));
WWHD_GAME_FUNC(0x02008E0C,void,rmt_ground_construct,(u32 check));
WWHD_GAME_FUNC(0x02008FEC,void,rmt_line_construct,(u32 line));
WWHD_GAME_FUNC(0x0200E240,void,rmt_collision,(u32 collision,u32 sphere));
WWHD_GAME_FUNC(0x0200ED84,void,rmt_calc,(u32 value,f32 target,f32 rate,f32 step));
WWHD_GAME_FUNC(0x0200EDC8,void,rmt_zero,(u32 value,f32 rate,f32 step));
WWHD_GAME_FUNC(0x0200F428,void,rmt_angle_calc,(u32 value,s16 target,s16 rate,s16 step));
WWHD_GAME_FUNC(0x0200FCD8,void,rmt_position,(u32 source,u32 target));
WWHD_GAME_FUNC(0x02018C8C,void,rmt_sphere_radius,(u32 sphere,f32 radius));
WWHD_GAME_FUNC(0x02018D40,void,rmt_sphere_center,(u32 sphere,u32 center));
WWHD_GAME_FUNC(0x020195B0,s16,rmt_atan,(f64 y,f64 x));
WWHD_GAME_FUNC(0x020198D8,f64,rmt_random,(f64 range));
WWHD_GAME_FUNC(0x0201A478,u32,rmt_angle_constructor,(u32 angle,s16 x,s16 y,s16 z));
WWHD_GAME_FUNC(0x0201AD78,void,rmt_vector_add,(u32 a,u32 target,u32 b));
WWHD_GAME_FUNC(0x0201ADE0,void,rmt_vector_subtract,(u32 a,u32 target,u32 b));
WWHD_GAME_FUNC(0x020402C8,s32,rmt_ice,(u32 ice));
WWHD_GAME_FUNC(0x021D73C0,void,rmt_animation,(u32 actor,s32 animation,f32 morph,u32 loop,f32 speed,s32 sound));
WWHD_GAME_FUNC(0x021D74F8,void,rmt_bg_check,(u32 actor));
WWHD_GAME_FUNC(0x021D75D8,void,rmt_texture,(u32 actor,u16 index));
WWHD_GAME_FUNC(0x021D7E80,void,rmt_wall_check,(u32 actor));
WWHD_GAME_FUNC(0x021D8530,void,rmt_explode,(u32 actor));
WWHD_GAME_FUNC(0x021D86F8,void,rmt_water_sound,(u32 actor));
WWHD_GAME_FUNC(0x021DD518,void,rmt_sound,(u32 actor,u32 id,u32 parameter));
WWHD_GAME_FUNC(0x021DD584,void,rmt_monster_out,(u32 actor,u32 id,u32 parameter));
WWHD_GAME_FUNC(0x021DD620,f64,rmt_sin_out,(u32 table,s16 angle));
WWHD_GAME_FUNC(0x021DD634,void,rmt_body2,(u32 actor));
WWHD_GAME_FUNC(0x021DE1E0,void,rmt_body3,(u32 actor));
WWHD_GAME_FUNC(0x021DE888,void,rmt_body4,(u32 actor));
WWHD_GAME_FUNC(0x021DEBC8,void,rmt_body5,(u32 actor));
WWHD_GAME_FUNC(0x021DF238,void,rmt_roll,(u32 actor));
WWHD_GAME_FUNC(0x021DFDDC,f64,rmt_cos_out,(u32 table,s16 angle));
WWHD_GAME_FUNC(0x024EC8D0,u32,rmt_action_target,(u32 attention,s32 index));
WWHD_GAME_FUNC(0x024EDFCC,s32,rmt_lockon,(u32 attention));
WWHD_GAME_FUNC(0x024F1AFC,void,rmt_line_set,(u32 line,u32 start,u32 end,u32 actor));
WWHD_GAME_FUNC(0x02515E50,void,rmt_status_move,(u32 status));
WWHD_GAME_FUNC(0x025160DC,s32,rmt_attack_hit,(u32 sphere));
WWHD_GAME_FUNC(0x025162A4,s32,rmt_hit,(u32 sphere));
WWHD_GAME_FUNC(0x02516300,u32,rmt_hit_object,(u32 sphere));
WWHD_GAME_FUNC(0x02518CC8,void,rmt_defence,(u32 actor,u32 object,u32 sound));
WWHD_GAME_FUNC(0x02518DB0,void,rmt_attack_power,(u32 info));
WWHD_GAME_FUNC(0x025192A8,u32,rmt_attack_check,(u32 actor,u32 info));
WWHD_GAME_FUNC(0x02520540,s32,rmt_reverb,(s32 room));
WWHD_GAME_FUNC(0x0255F554,void,rmt_flush,(u32 position,s32 mode));
WWHD_GAME_FUNC(0x025A847C,u32,rmt_particle_native,(u32 controller,u32 group,u32 id,u32 position,u32 angle,u32 scale,u32 alpha,u32 callback,s32 room,u32 color,u32 color2,u32 scale2));
WWHD_GAME_FUNC(0x025A8D40,s32,rmt_simple_native,(u32 controller,u32 id,u32 position,u32 alpha,u32 color,u32 color2,s32 draw));
WWHD_GAME_FUNC(0x025BA5D4,void,rmt_on_actor,(u32 save,u32 id,s32 room));
WWHD_GAME_FUNC(0x025BA0C0,s32,rmt_switch,(u32 save,u32 flag,s32 room));
WWHD_GAME_FUNC(0x025B9E38,void,rmt_on_switch,(u32 save,u32 flag,s32 room));
WWHD_GAME_FUNC(0x025D57E0,s32,rmt_delete,(u32 actor));
WWHD_GAME_FUNC(0x025D672C,void,rmt_min,(u32 actor,f32 x,f32 y,f32 z));
WWHD_GAME_FUNC(0x025D673C,void,rmt_max,(u32 actor,f32 x,f32 y,f32 z));
WWHD_GAME_FUNC(0x025D6800,void,rmt_pos_move,(u32 actor,u32 collision));
WWHD_GAME_FUNC(0x025D68EC,f64,rmt_distance,(u32 actor,u32 target));
WWHD_GAME_FUNC(0x025D6894,s16,rmt_actor_angle,(u32 actor,u32 target));
WWHD_GAME_FUNC(0x025DAF3C,void,rmt_pillar,(u32 position,f32 scale));
WWHD_GAME_FUNC(0x025DE508,u32,rmt_search,(u32 callback,u32 data));
WWHD_GAME_FUNC(0x025E1AA4,void,rmt_monster_native,(u32 id,u32 position,u32 process,u32 parameter,s32 reverb));
WWHD_GAME_FUNC(0x025E1D30,void,rmt_bgm,(s32 sound));
WWHD_GAME_FUNC(0x025E1FD8,void,rmt_enemy_damage,(void));
WWHD_GAME_FUNC(0x025E535C,s32,rmt_morf_play,(u32 animation,u32 position,u32 flags,s32 reverb));
WWHD_GAME_FUNC(0x025F1884,void,rmt_rotate_y_start,(u32 matrix,s16 angle));
WWHD_GAME_FUNC(0x025F18EC,void,rmt_rotate_x_start,(u32 matrix,s16 angle));
WWHD_GAME_FUNC(0x025F1BF4,void,rmt_rotate_x,(u32 matrix,s16 angle));
WWHD_GAME_FUNC(0x025F1C28,void,rmt_rotate_y,(u32 matrix,s16 angle));
WWHD_GAME_FUNC(0x025F1C5C,void,rmt_rotate_z,(u32 matrix,s16 angle));
WWHD_GAME_FUNC(0x025F24E0,void,rmt_stack_translate,(f32 x,f32 y,f32 z));
WWHD_GAME_FUNC(0x025F2518,void,rmt_stack_scale,(f32 x,f32 y,f32 z));
WWHD_GAME_FUNC(0x0273AA24,void,rmt_assert,(u32 file,s32 line,u32 message));
WWHD_GAME_FUNC(0x028E8D88,void,rmt_vector_add_native,(u32 a,u32 b,u32 destination));
WWHD_GAME_FUNC(0x028E8F64,void,rmt_matrix_vector,(u32 matrix,u32 source,u32 destination));
WWHD_GAME_FUNC(0x028E93CC,void,rmt_translate,(u32 matrix,f32 x,f32 y,f32 z));
WWHD_GAME_FUNC(0x028F4384,f64,rmt_sqrt,(f64 value));
#define rmt_static(address) ((u32)&WWHD_GAME_DATA(address,u8))
static u32 rmt_hio(void){return rmt_static(0x10465870);}
static u32 rmt_matrix(void){return WWHD_GAME_DATA(0x1018C7B0,u32);}
static u32 rmt_stack(void){return rmt_static(0x1048D0CC);}
static u32 rmt_player(void){return RMTPTR(wwhd_dComIfG_getGameInfo(),0x5B2C);}
static u32 rmt_attention(void){return wwhd_dComIfG_getGameInfo()+0x5804;}
static u32 rmt_background(void){return wwhd_dComIfG_getGameInfo()+0x12A0;}
static u32 rmt_particle_controller(void){return RMTPTR(wwhd_dComIfG_getGameInfo(),0x5AB0);}
static f32 rmt_reg_f(unsigned int group,unsigned int index){return RMTF(rmt_static(0x1047B610),0x90*group+4*index);}
static s16 rmt_reg_s(unsigned int group,unsigned int index){return RMT16(rmt_static(0x1047B688),0x90*group+2*index);}
static s32 rmt_abs(s16 value){return value<0?-(s32)value:value;}
static unsigned int rmt_truncate(f64 value){if(value!=value)return 0x80000000u;if(value>=2147483648.0)return 0x7FFFFFFFu;if(value<=-2147483648.0)return 0x80000000u;return(unsigned int)(s32)value;}
/* Character accesses preserve float bits and may alias typed local vectors. */
static void rmt_copy_vector(u32 destination,u32 source){for(u32 i=0;i<12;i++)RMT8(destination,i)=RMT8(source,i);}
static void rmt_copy_model(u32 model,u32 matrix){unsigned int words[12];for(unsigned int i=0;i<12;i++)words[i]=RMT32(matrix,i*4);for(unsigned int i=0;i<12;i++)RMT32(model,0xC8+i*4)=words[i];}
static void rmt_line_init(u32 line,s32 object){rmt_line_construct(line);for(unsigned int i=0;i<7;i++)RMT8(line,0x5C+i)=0;if(object)RMT8(line,0x5C)=1;RMT32(line,0x68)=1;RMT32(line,4)=(unsigned int)(line+0x64);RMT32(line,0)=(unsigned int)(line+0x58);RMT32(line,0x10)=object?0x1001560C:0x100155CC;RMT32(line,0x64)=object?0x1001562C:0x100155EC;RMT32(line,0x58)=object?0x1001563C:0x100155FC;RMT32(line,0x20)=object?0x1001561C:0x100155DC;}
static void rmt_line_finish(u32 line){RMT32(line,0x58)=0x100155FC;RMT32(line,0x64)=0x100154CC;RMT32(line,0x20)=0x100154BC;rmt_line_destroy(line,0);}
static void rmt_ground_init(u32 check){rmt_ground_construct(check);RMT8(check,0x44)=1;for(unsigned int i=1;i<7;i++)RMT8(check,0x44+i)=0;RMT32(check,0x10)=0x1001555C;RMT32(check,0)=(unsigned int)(check+0x40);RMT32(check,4)=(unsigned int)(check+0x4C);RMT32(check,0x4C)=0x1001557C;RMT32(check,0x40)=0x1001558C;RMT32(check,0x50)=4;RMT32(check,0x20)=0x1001556C;}
static void rmt_ground_finish(u32 check){RMT32(check,0x20)=0x100154EC;RMT32(check,0x40)=0x1001550C;RMT32(check,0x4C)=0x100154CC;rmt_ground_destroy(check,0);}
static s32 rmt_player_guard(u32 player){return ((s32(*)(u32))RMTPTR(RMTPTR(player,0xB4),0x3C))(player);}
static void rmt_monster(u32 actor,u32 id){if(actor&&(unsigned int)(actor+0x37C)){const unsigned int process=RMT32(actor,4);const s32 room=RMTS8(actor,0x326);const s32 reverb=rmt_reverb(room);rmt_monster_native(id,actor+0x37C,process,0,reverb);}}
static void rmt_on_actor_wrapper(u32 id,s32 room){rmt_on_actor(wwhd_dComIfG_getGameInfo()+0x20,id,room);}
static void rmt_on_switch_wrapper(u32 flag,s32 room){rmt_on_switch(wwhd_dComIfG_getGameInfo()+0x20,flag,room);}
static s32 rmt_switch_wrapper(u32 flag,s32 room){return rmt_switch(wwhd_dComIfG_getGameInfo()+0x20,flag,room);}
static void rmt_collision_wrapper(u32 sphere){rmt_collision(wwhd_dComIfG_getGameInfo()+0x26A4,sphere);}
static f32 rmt_sin(s16 angle){return RMTF(rmt_static(0x104A44F8),((u16)angle>>3)*8);}
static f32 rmt_cos(s16 angle){return RMTF(rmt_static(0x104A44F8),((u16)angle>>3)*8+4);}
static f32 rmt_sin_short(s16 angle){return(f32)rmt_sin_out(rmt_static(0x104A44F8),angle);}
static f32 rmt_cos_short(s16 angle){return(f32)rmt_cos_out(rmt_static(0x104A44F8),angle);}
static f32 rmt_check_x(unsigned int i){return RMTF(rmt_static(0x101BAE74),4*i);}
static f32 rmt_check_y(unsigned int i){return RMTF(rmt_static(0x101BAE8C),4*i);}
static f32 rmt_check_z(unsigned int i){return RMTF(rmt_static(0x101BAEA4),4*i);}
static u8 rmt_check_bit(unsigned int i){return RMT8(rmt_static(0x101BADC8),i);}
static s32 rmt_trail(unsigned int i){return RMTS32(rmt_static(0x101BAE14),4*i);}
static s32 rmt_fast_trail(unsigned int i){return RMTS32(rmt_static(0x101BAE34),4*i);}
static void rmt_angles_add(u32 destination,u32 a,u32 b){_Alignas(4) u8 storage[6];const s16 x=(s16)(RMT16(a,0)+RMT16(b,0)),z=(s16)(RMT16(a,4)+RMT16(b,4)),y=(s16)(RMT16(a,2)+RMT16(b,2));const u32 made=rmt_angle_constructor((u32)storage,x,y,z);const s16 rx=RMT16(made,0),ry=RMT16(made,2),rz=RMT16(made,4);RMT16(destination,0)=rx;RMT16(destination,2)=ry;RMT16(destination,4)=rz;}
static u32 rmt_particle(u32 id,u32 position,u32 angle,u32 scale){return rmt_particle_native(rmt_particle_controller(),0,id,position,angle,scale,255,0,-1,0,0,0);}
static void rmt_simple(u32 id,u32 position){rmt_simple_native(rmt_particle_controller(),id,position,255,rmt_static(0x101D5E98),rmt_static(0x101D5E98),0);}

static f32 rmt_random_add(f32 range,f32 add){return(f32)(rmt_random(range)+(f64)add);}
static void rmt_attention_flags(u32 actor,unsigned int flags){RMT32(actor,0x39C)=flags;}

/* Explicit Gekko single fused operation; avoid a freestanding libc fmaf import. */
static f32 rmt_fma(f32 a,f32 b,f32 c){
#pragma clang fp contract(fast)
    return a*b+c;
}
