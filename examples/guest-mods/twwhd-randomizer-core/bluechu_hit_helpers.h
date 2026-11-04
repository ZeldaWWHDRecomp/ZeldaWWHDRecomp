/* CC0 a874e1a9 ChuChu hit helpers; guest offsets and canonical native ABI. */
#pragma once
#define RBC8(a,o) (*(volatile u8*)((a)+(o)))
#define RBCS8(a,o) (*(volatile s8*)((a)+(o)))
#define RBC16(a,o) (*(volatile s16*)((a)+(o)))
#define RBC32(a,o) (*(volatile unsigned int*)((a)+(o)))
#define RBCF(a,o) (*(volatile f32*)((a)+(o)))
#ifndef RBCPTR
#define RBCPTR(a,o) (*(volatile u32*)((a)+(o)))
#endif
typedef struct{f32 x,y,z;} rbc_vector;
typedef struct{s16 x,y,z;} rbc_angle;
#define RBCV(a,o) (*(volatile rbc_vector*)((a)+(o)))
#define RBCA(a,o) (*(volatile rbc_angle*)((a)+(o)))
WWHD_GAME_FUNC(0x020195B0,s16,rbc_atan,(f64 y,f64 x));
WWHD_GAME_FUNC(0x02041C30,void,rbc_fire_remove,(u32 fire));
WWHD_GAME_FUNC(0x0210CC00,void,rbc_electric_end,(u32 actor));
WWHD_GAME_FUNC(0x02515BBC,u32,rbc_hit_actor,(u32 target_info));
WWHD_GAME_FUNC(0x02515E50,void,rbc_status_move,(u32 status));
WWHD_GAME_FUNC(0x025162A4,s32,rbc_hit,(u32 cylinder));
WWHD_GAME_FUNC(0x02516300,u32,rbc_hit_object,(u32 cylinder));
WWHD_GAME_FUNC(0x025192A8,u32,rbc_attack_check,(u32 actor,u32 info));
WWHD_GAME_FUNC(0x02520540,s32,rbc_reverb,(s32 room));
WWHD_GAME_FUNC(0x025A847C,u32,rbc_particle_native,(u32 controller,u32 group,u32 id,u32 position,u32 angle,u32 scale,u32 alpha,u32 callback,s32 room,u32 color,u32 color2,u32 scale2));
WWHD_GAME_FUNC(0x025E1A40,void,rbc_sound_native,(u32 id,u32 position,u32 parameter,s32 reverb));
WWHD_GAME_FUNC(0x025E1AA4,void,rbc_monster_native,(u32 id,u32 position,u32 process,u32 parameter,s32 reverb));
static u32 rbc_player(void){return RBCPTR(wwhd_dComIfG_getGameInfo(),0x5B2C);}
static f32 rbc_reg_f(unsigned int group,unsigned int index){return RBCF((u32)&WWHD_GAME_DATA(0x1047B610,u8),0x90*group+4*index);}
static s16 rbc_reg_s(unsigned int group,unsigned int index){return RBC16((u32)&WWHD_GAME_DATA(0x1047B688,u8),0x90*group+2*index);}
static u32 rbc_particle(u32 id,u32 position,u32 angle,u32 scale){const u32 play=wwhd_dComIfG_getGameInfo();return rbc_particle_native(RBCPTR(play,0x5AB0),0,id,position,angle,scale,255,0,-1,0,0,0);}
static void rbc_sound_nn(u32 actor,u32 id,u32 parameter){if((unsigned int)(actor+0x37C)){const s32 reverb=rbc_reverb(RBCS8(actor,0x326));rbc_sound_native(id,actor+0x37C,parameter,reverb);}}
static void rbc_sound(u32 actor,u32 id,u32 parameter){if(actor)rbc_sound_nn(actor,id,parameter);}
static void rbc_monster_nn(u32 actor,u32 id,u32 parameter){if((unsigned int)(actor+0x37C)){const s32 room=RBCS8(actor,0x326);const unsigned int process=actor?RBC32(actor,4):0xFFFFFFFFu;const s32 reverb=rbc_reverb(room);rbc_monster_native(id,actor+0x37C,process,parameter,reverb);}}
static void rbc_monster(u32 actor,u32 id,u32 parameter){if(actor)rbc_monster_nn(actor,id,parameter);}
