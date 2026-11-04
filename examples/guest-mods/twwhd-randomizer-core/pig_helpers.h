/* Public CC0 pig layout and inline methods. Narrow randomizer changes are in
 * the complete Execute consumer, not shared inventory/attack queries. */
#pragma once
#define RP_U8(a,o) (*(volatile u8*)((a)+(o)))
#define RP_S8(a,o) (*(volatile s8*)((a)+(o)))
#define RP_U16(a,o) (*(volatile u16*)((a)+(o)))
#define RP_S16(a,o) (*(volatile s16*)((a)+(o)))
#define RP_U32(a,o) (*(volatile unsigned int*)((a)+(o)))
#define RP_S32(a,o) (*(volatile int*)((a)+(o)))
#define RP_F32(a,o) (*(volatile f32*)((a)+(o)))
#ifndef RP_PTR
#define RP_PTR(a,o) (*(volatile u32*)((a)+(o)))
#endif
typedef struct{f32 x,y,z;} rp_vector;
typedef struct{s16 x,y,z;} rp_angle;
#define RP_VEC(a,o) (*(volatile rp_vector*)((a)+(o)))
#define RP_ANGLE(a,o) (*(volatile rp_angle*)((a)+(o)))
WWHD_GAME_FUNC(0x0200ED84,void,rp_calc_native,(u32 a,f32 target,f32 rate,f32 step));
WWHD_GAME_FUNC(0x0200EDC8,void,rp_zero_native,(u32 a,f32 rate,f32 step));
WWHD_GAME_FUNC(0x0200F428,void,rp_angle_native,(u32 a,s16 target,s16 rate,s16 step));
WWHD_GAME_FUNC(0x0200FAAC,s32,rp_distance_angle,(s16 a,s16 b));
WWHD_GAME_FUNC(0x020195B0,s16,rp_atan,(f32 y,f32 x));
/* Preserve native F1 width. Execute rounds random results only after adding
 * the offset; sqrt and unit-random comparisons consume the unrounded result.
 * Stock helpers generally return single-representable values, but callbacks
 * need not. No affected result remains live across a subsequent callback. */
WWHD_GAME_FUNC(0x02019788,f64,rp_random,(void));
WWHD_GAME_FUNC(0x020198D8,f64,rp_random_f,(f64 limit));
WWHD_GAME_FUNC(0x02019918,f64,rp_random_fx,(f64 limit));
WWHD_GAME_FUNC(0x028F4384,f64,rp_sqrt,(f64 value));
WWHD_GAME_FUNC(0x027F2BF8,s32,rp_check_frame,(u32 ctrl,f32 frame));
WWHD_GAME_FUNC(0x0218EB64,void,rp_animation,(u32 actor,s32 animation,f32 morph,u8 loop,f32 rate,s32 sound));
WWHD_GAME_FUNC(0x0218EF4C,s32,rp_carry,(u32 actor));
WWHD_GAME_FUNC(0x0218F1FC,void,rp_splash,(u32 actor));
WWHD_GAME_FUNC(0x0218F380,s32,rp_swim_change,(u32 actor));
WWHD_GAME_FUNC(0x0218F4EC,void,rp_gas,(u32 actor));
WWHD_GAME_FUNC(0x0218F600,void,rp_smoke,(u32 actor));
WWHD_GAME_FUNC(0x02190400,void,rp_target,(u32 actor,u8 mode));
WWHD_GAME_FUNC(0x02190708,s32,rp_food_check,(u32 actor));
WWHD_GAME_FUNC(0x02190BFC,void,rp_background,(u32 actor));
WWHD_GAME_FUNC(0x0219463C,s32,rp_eye,(u32 actor));
WWHD_GAME_FUNC(0x02194654,void,rp_speed,(u32 actor));
WWHD_GAME_FUNC(0x0218FABC,void,rp_draw,(u32 actor));
WWHD_GAME_FUNC(0x021947AC,void,rp_attack,(u32 actor));
WWHD_GAME_FUNC(0x0219509C,void,rp_food,(u32 actor));
WWHD_GAME_FUNC(0x02194798,f32,rp_sine_native,(u32 table,s16 angle));
WWHD_GAME_FUNC(0x025162A4,s32,rp_hit,(u32 sphere));
WWHD_GAME_FUNC(0x02516300,u32,rp_hit_object,(u32 sphere));
WWHD_GAME_FUNC(0x0251621C,void,rp_clear_hit,(u32 sphere));
WWHD_GAME_FUNC(0x025192A8,void,rp_attack_check,(u32 actor,u32 info));
WWHD_GAME_FUNC(0x025A5AC8,void,rp_follow_end,(u32 callback));
WWHD_GAME_FUNC(0x025A9270,void,rp_ripple_end,(u32 callback));
WWHD_GAME_FUNC(0x025A5F88,void,rp_smoke_end,(u32 callback));
WWHD_GAME_FUNC(0x02520540,s32,rp_reverb,(s32 room));
WWHD_GAME_FUNC(0x025E1AA4,void,rp_monster_sound,(u32 id,u32 position,u32 pid,u32 argument,s32 reverb));
WWHD_GAME_FUNC(0x025CB374,u8,rp_shock,(u32 vibration,s32 strength,s32 flags,u32 direction));
WWHD_GAME_FUNC(0x025A847C,u32,rp_particle,(u32 controller,u32 group,u32 id,u32 pos,u32 rotation,u32 scale,u32 alpha,u32 callback,s32 room,u32 color,u32 color2,u32 scale2));
WWHD_GAME_FUNC(0x025D68EC,f32,rp_distance,(u32 actor,u32 player));
WWHD_GAME_FUNC(0x025D6894,s16,rp_player_angle,(u32 actor,u32 player));
WWHD_GAME_FUNC(0x025D9A70,s32,rp_ground_angle,(u32 actor,u32 result));
WWHD_GAME_FUNC(0x025052BC,void,rp_lock_off,(u32 camera,u32 id));
WWHD_GAME_FUNC(0x02556D14,s32,rp_daynight,(void));
WWHD_GAME_FUNC(0x025B8B68,void,rp_event_on,(u32 save,u32 flag));
WWHD_GAME_FUNC(0x025B8BB0,u32,rp_event_reg,(u32 save,u32 flag));
WWHD_GAME_FUNC(0x025B8AF4,void,rp_event_set_reg,(u32 save,u32 flag,u32 value));
WWHD_GAME_FUNC(0x024EF09C,s32,rp_special,(u32 bg,u32 poly));
WWHD_GAME_FUNC(0x024EF0F4,s32,rp_attribute,(u32 bg,u32 poly));
WWHD_GAME_FUNC(0x024EECAC,u32,rp_material,(u32 bg,u32 poly));
WWHD_GAME_FUNC(0x02516C14,void,rp_mass,(u32 manager,u32 object,u8 priority));
WWHD_GAME_FUNC(0x02008570,void,rp_triangle,(u32 bg,u32 poly,u32 a,u32 b,u32 c));
WWHD_GAME_FUNC(0x02008FEC,void,rp_line_ctor,(u32 line));
WWHD_GAME_FUNC(0x025F1884,void,rp_rotate_y,(u32 matrix,s16 angle));
WWHD_GAME_FUNC(0x0200FCD8,void,rp_position,(u32 source,u32 result));
WWHD_GAME_FUNC(0x028E8D88,void,rp_vector_add,(u32 a,u32 b,u32 out));
WWHD_GAME_FUNC(0x0201ADE0,void,rp_vector_subtract,(u32 a,u32 out,u32 b));
WWHD_GAME_FUNC(0x028E8DD0,f32,rp_square_mag,(u32 vec));
WWHD_GAME_FUNC(0x02018D40,void,rp_sphere_center,(u32 sphere,u32 center));
WWHD_GAME_FUNC(0x02018C8C,void,rp_sphere_radius,(u32 sphere,f32 radius));
WWHD_GAME_FUNC(0x025E535C,s32,rp_morf_play,(u32 morf,u32 position,u32 material,s8 reverb));
WWHD_GAME_FUNC(0x025200D4,u32,rp_play,(void));
WWHD_GAME_FUNC(0x02008B4C,void,rp_line_poly_dtor,(u32 line,s32 flags));
static void rp_copy_vector(u32 out,u32 in){const unsigned int x=RP_U32(in,0),y=RP_U32(in,4),z=RP_U32(in,8);RP_U32(out,0)=x;RP_U32(out,4)=y;RP_U32(out,8)=z;}
static void rp_set_vector(volatile rp_vector* out,f32 x,f32 y,f32 z){out->x=x;out->y=y;out->z=z;}
static unsigned int rp_truncate(f32 value){if(value!=value)return 0x80000000u;if(value>=2147483648.0f)return 0x7FFFFFFFu;if(value<=-2147483648.0f)return 0x80000000u;return (unsigned int)(s32)value;}
#ifndef RP_FMA
static f32 rp_fma(f32 a,f32 b,f32 c){
#pragma clang fp contract(fast)
return a*b+c;
}
static f32 rp_fnmsub(f32 a,f32 b,f32 c){
#pragma clang fp contract(fast)
return -(a*b-c);
}
#else
#define rp_fma RP_FMA
#define rp_fnmsub(a,b,c) (-RP_FMA(a,b,-(c)))
#endif
static s32 rp_stopped(u32 actor){const u32 morf=RP_PTR(actor,0x744);return (RP_U8(morf,0xA7)&1)||RP_F32(morf,0x98)==0.0f;}
static f32 rp_reg_f(u32 child,u32 i){return RP_F32((u32)&WWHD_GAME_DATA(0x1047B610,u8),0x90*child+4*i);}
static f32 rp_sine(s16 angle){return RP_F32((u32)&WWHD_GAME_DATA(0x104A44F8,u8),((u16)angle>>3)*8);}
static u32 rp_player(void){return RP_PTR(rp_play(),0x5B2C);}
static u32 rp_event(void){return WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644;}
static f32 rp_water(u32 actor){return RP_F32(actor,0xB90);}
static void rp_sound(u32 actor,u32 sound){const u32 eye=actor+0x37C;if(eye){const u32 id=actor?RP_U32(actor,4):0xFFFFFFFFu;const s32 reverb=rp_reverb(RP_S8(actor,0x326));rp_monster_sound(sound,eye,id,0,reverb);}}
static void rp_sound_a(u32 actor,u32 sound){if(actor)rp_sound(actor,sound);}
static void rp_catch_sound(u32 actor){rp_sound(actor,RP_U8(actor,0x748)>=8?0x4917:0x4822);}
static void rp_scale(u32 emitter,f32 x,f32 y,f32 z){RP_F32(emitter,0x220)=x;RP_F32(emitter,0x224)=y;RP_F32(emitter,0x228)=z;RP_F32(emitter,0x238)=x;RP_F32(emitter,0x23C)=y;RP_F32(emitter,0x240)=z;}
typedef struct{u32 text,vtable;} rp_string;
static void rp_assure(rp_string* s){((void(*)(rp_string*))(RP_PTR(s->vtable,0x14)))(s);}
static s32 rp_sea(u32 literal){rp_string a={literal,0x100127E0};rp_string b={rp_play()+0x5134,0x100127E0};rp_assure(&a);rp_assure(&a);u32 x=a.text;rp_assure(&b);if(x==b.text)return 1;x=a.text;u32 y=b.text;for(u32 i=0;i<0x40001;i++){const u8 c=RP_U8(x,i);if(c!=RP_U8(y,i))return 0;if(!c)return 1;}return 0;}
static void rp_line_construct(u32 line){rp_line_ctor(line);for(u32 i=0;i<7;i++)RP_U8(line,0x5C+i)=0;RP_U32(line,0x68)=1;RP_PTR(line,4)=line+0x64;RP_PTR(line,0)=line+0x58;RP_PTR(line,0x10)=0x10012888;RP_PTR(line,0x64)=0x100128A8;RP_PTR(line,0x58)=0x100128B8;RP_PTR(line,0x20)=0x10012898;}
static void rp_line_destruct(u32 line){RP_PTR(line,0x58)=0x100128B8;RP_PTR(line,0x64)=0x10012808;RP_PTR(line,0x20)=0x100127F8;rp_line_poly_dtor(line,0);}
WWHD_GAME_FUNC(0x0200E240,void,rp_collision_set,(u32 scene,u32 object));
WWHD_GAME_FUNC(0x02194644,void,rp_off_bit,(u32 value,u32 bit));

static void rp_calc(volatile f32* a,f32 target,f32 rate,f32 step){rp_calc_native((u32)a,target,rate,step);}
static void rp_zero(volatile f32* a,f32 rate,f32 step){rp_zero_native((u32)a,rate,step);}
static void rp_angle_calc(volatile s16* a,s16 target,s16 rate,s16 step){rp_angle_native((u32)a,target,rate,step);}
static f32 rp_player_distance(u32 actor){const u32 player=rp_player();return rp_distance(actor,player);}
static s16 rp_search_angle(u32 actor){const u32 player=rp_player();return rp_player_angle(actor,player);}
static void rp_force_unlock(u32 actor){const u32 camera=RP_PTR(rp_play(),0x5AF8)+0x248;rp_lock_off(camera,actor?RP_U32(actor,4):0xFFFFFFFFu);}
static void rp_shock_up(volatile rp_vector* dir){(void)rp_shock(rp_play()+0x599C,1,-0x21,(u32)dir);}
static void rp_particle_set(u32 controller,u32 group,u32 id,volatile rp_vector* pos,volatile rp_angle* rotation,u32 scale,u32 alpha,u32 cb,s32 room,u32 color,u32 color2,u32 scale2){(void)rp_particle(controller,group,(u32)(u16)id,(u32)pos,(u32)rotation,scale,alpha,cb,room,color,color2,scale2);}
static void rp_mtx_position(rp_vector* source,volatile rp_vector* out){rp_position((u32)source,(u32)out);}
static void rp_vec_add(volatile rp_vector* a,volatile rp_vector* b,volatile rp_vector* out){rp_vector_add((u32)a,(u32)b,(u32)out);}
static void rp_vec_sub(volatile rp_vector* a,rp_vector* out,volatile rp_vector* b){rp_vector_subtract((u32)a,(u32)out,(u32)b);}
static f32 rp_magnitude(rp_vector* a){return rp_square_mag((u32)a);}
static void rp_triangle_get(u32 bg,u32 poly,rp_vector* a,rp_vector* b,rp_vector* c){rp_triangle(bg,poly,(u32)a,(u32)b,(u32)c);}
static void rp_ground_get(u32 actor,volatile rp_angle* angle){(void)rp_ground_angle(actor,(u32)angle);}
