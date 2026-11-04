/* Source-expanded CC0 a874e1a9 Stalfos helpers and canonical native ABI.
 * All guest data words remain 32 bits even in host pointer-seam fixtures.
 * Attack-info scratch is initialized at the same fields as native before
 * native attack callbacks populate the remaining fields. Matrix/vector
 * copies capture raw words before writes, retaining NaNs and alias order.
 * randomFX returns double: do not round before the original fctiwz. */
#pragma once
#define RST8(a,o) (*(volatile u8*)((a)+(o)))
#define RSTS8(a,o) (*(volatile s8*)((a)+(o)))
#define RST16(a,o) (*(volatile s16*)((a)+(o)))
#define RSTU16(a,o) (*(volatile u16*)((a)+(o)))
#define RST32(a,o) (*(volatile unsigned int*)((a)+(o)))
#define RSTF(a,o) (*(volatile f32*)((a)+(o)))
#ifndef RSTPTR
#define RSTPTR(a,o) (*(volatile u32*)((a)+(o)))
#endif
typedef struct{f32 x,y,z;} rst_vector;
typedef struct{s16 x,y,z;} rst_angle;
#define RSTV(a,o) (*(volatile rst_vector*)((a)+(o)))
#define RSTA(a,o) (*(volatile rst_angle*)((a)+(o)))
WWHD_GAME_FUNC(0x02008860,s32,rst_line_cross_native,(u32 background,u32 line));
WWHD_GAME_FUNC(0x02008B4C,void,rst_line_destroy,(u32 line,s32 flag));
WWHD_GAME_FUNC(0x02008FEC,void,rst_line_construct,(u32 line));
WWHD_GAME_FUNC(0x0200E240,void,rst_collision_set,(u32 collision,u32 sphere));
WWHD_GAME_FUNC(0x0200EDC8,void,rst_zero,(u32 value,f32 rate,f32 step));
WWHD_GAME_FUNC(0x0200F428,void,rst_angle_calc,(u32 value,s16 target,s16 rate,s16 step));
WWHD_GAME_FUNC(0x0200FAD8,void,rst_translate,(s32 mode,f32 x,f32 y,f32 z));
WWHD_GAME_FUNC(0x0200FC74,void,rst_scale,(s32 mode,f32 x,f32 y,f32 z));
WWHD_GAME_FUNC(0x0200FCD8,void,rst_position,(u32 source,u32 target));
WWHD_GAME_FUNC(0x02018C8C,void,rst_sphere_radius,(u32 sphere,f32 radius));
WWHD_GAME_FUNC(0x02018D40,void,rst_sphere_center,(u32 sphere,u32 center));
WWHD_GAME_FUNC(0x02019918,f64,rst_random_signed,(f64 range));
WWHD_GAME_FUNC(0x020402C8,s32,rst_ice,(u32 ice));
WWHD_GAME_FUNC(0x02491330,void,rst_head_animation,(u32 actor,s32 animation,f32 morph,u32 loop,f32 speed,s32 sound));
WWHD_GAME_FUNC(0x02491458,void,rst_chain,(u32 actor));
WWHD_GAME_FUNC(0x0249196C,void,rst_weapon_chain,(u32 actor));
WWHD_GAME_FUNC(0x0249210C,void,rst_wait,(u32 actor));
WWHD_GAME_FUNC(0x024925B8,void,rst_part,(u32 actor,u32 index));
WWHD_GAME_FUNC(0x024972A4,void,rst_move,(u32 actor));
WWHD_GAME_FUNC(0x024EF0F4,u32,rst_attribute,(u32 background,u32 polygon));
WWHD_GAME_FUNC(0x024F1AFC,void,rst_line_set,(u32 line,u32 start,u32 end,u32 actor));
WWHD_GAME_FUNC(0x02515E50,void,rst_status_move,(u32 status));
WWHD_GAME_FUNC(0x025162A4,s32,rst_hit,(u32 sphere));
WWHD_GAME_FUNC(0x02516300,u32,rst_hit_object,(u32 sphere));
WWHD_GAME_FUNC(0x025167C0,void,rst_start_attack,(u32 sphere,u32 center));
WWHD_GAME_FUNC(0x025167E4,void,rst_move_attack,(u32 sphere,u32 center));
WWHD_GAME_FUNC(0x02518CC8,void,rst_defence_sound,(u32 actor,u32 object,u32 sound));
WWHD_GAME_FUNC(0x02518DB0,void,rst_attack_power,(u32 info));
WWHD_GAME_FUNC(0x025192A8,u32,rst_attack_check,(u32 actor,u32 info));
WWHD_GAME_FUNC(0x02520540,s32,rst_reverb,(s32 room));
WWHD_GAME_FUNC(0x02555D0C,u32,rst_environment,(void));
WWHD_GAME_FUNC(0x025626A4,void,rst_lighting,(u32 environment,s32 type,u32 position,u32 tev));
WWHD_GAME_FUNC(0x025A5F88,void,rst_smoke_end,(u32 callback));
WWHD_GAME_FUNC(0x025A847C,u32,rst_particle_native,(u32 controller,u32 group,u32 id,u32 position,u32 angle,u32 scale,u32 alpha,u32 callback,s32 room,u32 color,u32 color2,u32 scale2));
WWHD_GAME_FUNC(0x025D5218,u32,rst_judge,(u32 callback,u32 data));
WWHD_GAME_FUNC(0x025D57E0,s32,rst_delete,(u32 actor));
WWHD_GAME_FUNC(0x025D5834,u32,rst_create,(u32 process,u32 parameter,u32 position,s32 room,u32 angle,u32 scale,s32 subtype,u32 callback));
WWHD_GAME_FUNC(0x025D9000,void,rst_steal,(u32 position,u32 table,s32 room,u32 flag,u32 index));
WWHD_GAME_FUNC(0x025D9D24,void,rst_cancel_carry,(u32 actor));
WWHD_GAME_FUNC(0x025E1A40,void,rst_sound_native,(u32 id,u32 position,u32 parameter,s32 reverb));
WWHD_GAME_FUNC(0x025E1AA4,void,rst_monster_native,(u32 id,u32 position,u32 process,u32 parameter,s32 reverb));
WWHD_GAME_FUNC(0x025E1D30,void,rst_bgm_hit,(s32 sound));
WWHD_GAME_FUNC(0x025E1FD8,void,rst_enemy_damage,(void));
WWHD_GAME_FUNC(0x025E535C,s32,rst_morf_play,(u32 animation,u32 position,u32 flags,s32 reverb));
WWHD_GAME_FUNC(0x025E55A0,void,rst_morf_calc,(u32 animation));
WWHD_GAME_FUNC(0x025F1884,void,rst_rotate_start,(u32 matrix,s16 angle));
WWHD_GAME_FUNC(0x025F1C28,void,rst_rotate,(u32 matrix,s16 angle));
WWHD_GAME_FUNC(0x028E90D4,void,rst_matrix_copy,(u32 source,u32 target));
static u32 rst_matrix(void){return WWHD_GAME_DATA(0x1018C7B0,u32);}
static f32 rst_reg_f(unsigned int group,unsigned int index){return RSTF((u32)&WWHD_GAME_DATA(0x1047B610,u8),0x90*group+4*index);}
static s16 rst_reg_s(unsigned int group,unsigned int index){return RST16((u32)&WWHD_GAME_DATA(0x1047B688,u8),0x90*group+2*index);}
static unsigned int rst_truncate(f64 value){if(value!=value)return 0x80000000u;if(value>=2147483648.0)return 0x7FFFFFFFu;if(value<=-2147483648.0)return 0x80000000u;return(unsigned int)(s32)value;}
static void rst_copy_vector(u32 destination,u32 source){unsigned int x=RST32(source,0),y=RST32(source,4),z=RST32(source,8);RST32(destination,0)=x;RST32(destination,4)=y;RST32(destination,8)=z;}
static u32 rst_find(u32 id){unsigned int value=(unsigned int)id;return value==0xFFFFFFFFu?0:rst_judge(0x025E1234,(u32)&value);}
static void rst_copy_model(u32 model,u32 matrix){unsigned int words[12];for(unsigned int i=0;i<12;i++)words[i]=RST32(matrix,i*4);for(unsigned int i=0;i<12;i++)RST32(model,0xC8+i*4)=words[i];}
static void rst_joint(u32 actor,s32 joint){u32 model=RSTPTR(RSTPTR(actor,0x3D8),0x90),data=RSTPTR(model,0x2C);const u16 flags=RSTU16(data,4);const u32 matrices=RSTPTR(data,0x10);RSTU16(data,4)=flags|0x10;rst_matrix_copy(matrices+(unsigned int)joint*0x30,rst_matrix());}
static u32 rst_particle(u32 id,u32 position){const u32 play=wwhd_dComIfG_getGameInfo();return rst_particle_native(RSTPTR(play,0x5AB0),0,id,position,0,0,255,0,-1,0,0,0);}
static u32 rst_particle_angle(u32 id,u32 position,u32 angle){const u32 play=wwhd_dComIfG_getGameInfo();return rst_particle_native(RSTPTR(play,0x5AB0),0,id,position,angle,0,255,0,-1,0,0,0);}
static u32 rst_toon(u32 actor,u32 id,u32 position,u32 angle,u32 callback){const s32 room=RSTS8(actor,0x326);const u32 play=wwhd_dComIfG_getGameInfo();return rst_particle_native(RSTPTR(play,0x5AB0),2,id,position,angle,0,185,callback,room,0,0,0);}
static void rst_sound(u32 actor,u32 sound,u32 level){if(!actor||!((unsigned int)actor+0x37C))return;const s32 reverb=rst_reverb(RSTS8(actor,0x326));rst_sound_native(sound,actor+0x37C,level,reverb);}
static void rst_monster(u32 actor,u32 sound,u32 level){if(!actor||!((unsigned int)actor+0x37C))return;const u32 process=RST32(actor,4);const s32 reverb=rst_reverb(RSTS8(actor,0x326));rst_monster_native(sound,actor+0x37C,process,level,reverb);}
static void rst_line_initialize(u32 line){rst_line_construct(line);for(unsigned int i=0;i<7;i++)RST8(line,0x5C+i)=0;RST32(line,0x64)=0x1003D59C;RST32(line,0x10)=0x1003D57C;RST32(line,0)=(unsigned int)(line+0x58);RST32(line,4)=(unsigned int)(line+0x64);RST32(line,0x68)=1;RST32(line,0x20)=0x1003D58C;RST32(line,0x58)=0x1003D5AC;}
static void rst_line_finish(u32 line){RST32(line,0x58)=0x1003D5AC;RST32(line,0x64)=0x1003D4FC;RST32(line,0x20)=0x1003D4EC;rst_line_destroy(line,0);}
static s32 rst_line_cross(u32 line,u32 start,u32 end,u32 actor){rst_line_set(line,start,end,actor);return rst_line_cross_native(wwhd_dComIfG_getGameInfo()+0x12A0,line)!=0;}
static void rst_collision_register(u32 sphere){rst_collision_set(wwhd_dComIfG_getGameInfo()+0x26A4,sphere);}
static void rst_visible(u32 actor){for(unsigned int joint=0;joint<26;joint++)if(RSTPTR(actor,0xFF0+0x6C*joint)&&(!RSTS8(actor,0x2010)||(joint!=3&&joint!=4)))rst_part(actor,joint);if(RST16(actor,0x3E2)!=33&&RST16(actor,0x3E0)!=35)rst_chain(actor);if(RSTS8(actor,0x2010))rst_weapon_chain(actor);}
