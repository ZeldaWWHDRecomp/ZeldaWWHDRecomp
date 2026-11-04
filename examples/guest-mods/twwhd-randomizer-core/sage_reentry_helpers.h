/* Public CC0 pin a874e1: d_a_npc_cb1.cpp, d_a_npc_md_heap.cpp and layouts. */
#pragma once
#include "sage_spawn_data.h"
#define RS8(a,o) (*(volatile u8*)((a)+(o)))
#define RSS8(a,o) (*(volatile s8*)((a)+(o)))
#define RS16(a,o) (*(volatile u16*)((a)+(o)))
#define RSS16(a,o) (*(volatile s16*)((a)+(o)))
#define RS32(a,o) (*(volatile unsigned int*)((a)+(o)))
#ifndef RSP
#define RSP(a,o) (*(volatile u32*)((a)+(o)))
#endif
WWHD_GAME_FUNC(0x024450B4,void,rs_construct_npc,(u32 actor));
WWHD_GAME_FUNC(0x027F2BC0,void,rs_frame_ctor,(u32 frame,s32 end));
WWHD_GAME_FUNC(0x027DA984,void,rs_matrix_ctor,(u32 matrix));
WWHD_GAME_FUNC(0x028EFFD0,void,rs_array_ctor,(u32 array,u32 count,u32 size,u32 constructor));
WWHD_GAME_FUNC(0x0200BD2C,void,rs_status_ctor,(u32 status));
WWHD_GAME_FUNC(0x02515DA0,void,rs_gstatus_ctor,(u32 status));
WWHD_GAME_FUNC(0x02515FB8,void,rs_object_ctor,(u32 object));
WWHD_GAME_FUNC(0x02018590,void,rs_cylinder_ctor,(u32 cylinder));
WWHD_GAME_FUNC(0x0259DAA0,void,rs_joint_ctor,(u32 joint));
WWHD_GAME_FUNC(0x025E7820,void,rs_btp_ctor,(u32 animation));
WWHD_GAME_FUNC(0x025E7C6C,void,rs_btk_ctor,(u32 animation));
WWHD_GAME_FUNC(0x02008FEC,void,rs_line_ctor,(u32 check));
WWHD_GAME_FUNC(0x02018150,void,rs_capsule_ctor,(u32 capsule));
WWHD_GAME_FUNC(0x0252CE74,void,rs_mirror_ctor,(u32 mirror));
WWHD_GAME_FUNC(0x025A9084,void,rs_ripple_ctor,(u32 callback));
WWHD_GAME_FUNC(0x02520C0C,u32,rs_item,(u32 item));
WWHD_GAME_FUNC(0x025B7A2C,u32,rs_collect,(u32 save,u32 field,u32 bit));
WWHD_GAME_FUNC(0x025B8B94,s32,rs_event_native,(u32 save,u32 bit));
WWHD_GAME_FUNC(0x02520A84,s32,rs_stage_boss,(u32 stage));
WWHD_GAME_FUNC(0x02520460,u32,rs_load,(u32 phase,u32 name));
WWHD_GAME_FUNC(0x025D63E8,s32,rs_heap,(u32 actor,u32 callback,u32 size));
WWHD_GAME_FUNC(0x025B9834,void,rs_restart,(u32 restart,s32 mode,u32 position,s16 angle,s8 room));
WWHD_GAME_FUNC(0x025B8880,void,rs_priest,(u32 priest,s32 mode,u32 position,s16 angle,s8 room));
WWHD_GAME_FUNC(0x02445784,void,rs_check_restart,(u32 actor,s32 mode));
WWHD_GAME_FUNC(0x025D537C,void,rs_layer,(u32 actor));
WWHD_GAME_FUNC(0x0221ED08,void,rs_makar_matrix,(u32 actor));
WWHD_GAME_FUNC(0x0221F3C0,s32,rs_makar_init,(u32 actor));
WWHD_GAME_FUNC(0x02285A6C,void,rs_medli_matrix,(u32 actor));
WWHD_GAME_FUNC(0x02285580,s32,rs_medli_init,(u32 actor));
WWHD_GAME_FUNC(0x028F040C,void,rs_copy_string,(u32 dest,u32 source));
WWHD_GAME_FUNC(0x0259D734,s32,rs_letter,(void));
static u32 rs_save(void){return WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);}
static s32 rs_event(u32 bit){return rs_event_native(rs_save()+0x644,bit);}
typedef struct{u32 text,vtable;} rs_string;
static void rs_assure(rs_string* s){((void(*)(rs_string*))(RSP(s->vtable,0x14)))(s);}
static s32 rs_stage(u32 literal,u32 vtable){rs_string a={literal,vtable};rs_string b={wwhd_dComIfG_getGameInfo()+0x5134,vtable};rs_assure(&a);rs_assure(&a);u32 pa=a.text;rs_assure(&b);u32 pb=b.text;if(pa==pb)return 1;pa=a.text;pb=b.text;for(u32 i=0;i<0x40001;i++){const u8 ca=RS8(pa,i),cb=RS8(pb,i);if(ca!=cb)return 0;if(!ca)return 1;}return 0;}
/* Capture both native locals before the injected GameInfo call. A miss leaves
 * every priest byte and both locals intact, but still calls both setters.
 * Reload the singleton only after restart, retaining the original position pointer. */
static void rs_reset_priest(u32 save,s32 mode,const unsigned int rows[7][4]){
 /* The original flag load remains even though upstream removes its branch. */
 (void)RS8(save,0x1DB);
 const u32 priest=save+0x1CC;
 s8 room=RSS8(priest,0xE);s16 angle=RSS16(priest,0xC);
 const s16 spawn=RSS16(wwhd_dComIfG_getGameInfo(),0x513C);
 for(u32 i=0;i<7;i++)if((s32)(rows[i][0]>>24)==(s32)spawn){
  RS32(priest,0)=rows[i][1];RS32(priest,4)=rows[i][2];RS32(priest,8)=rows[i][3];
  angle=(s16)(u16)rows[i][0];RS16(priest,0xC)=(u16)angle;
  room=(s8)(u8)(rows[i][0]>>16);RS8(priest,0xE)=(u8)room;break;
 }
 rs_restart(save+0x1148,mode,priest,angle,room);
 rs_priest(rs_save()+0x1CC,mode,priest,angle,room);
}
static void rs_bck(u32 p){rs_frame_ctor(p,0);RSP(p,0x10)=0x1016E54C;rs_matrix_ctor(p+0x14);RS32(p,0x84)=0;RS32(p,0x88)=0;RS32(p,0x80)=0;RSP(p,0x58)=0;RSP(p,0x10)=0x10018D24;RS32(p,0x7C)=0;RSP(p,0x48)=0x1016D820;}
static void rs_status(u32 p){rs_status_ctor(p);rs_gstatus_ctor(p+0x1C);RSP(p,0x18)=0x1004AE88;RSP(p,0x1C)=0x1004AEC0;}
static void rs_cylinder(u32 p,u32 aabb){rs_object_ctor(p);RSP(p,0x114)=0x100015A8;RSP(p,0x110)=aabb;rs_cylinder_ctor(p+0x118);RSP(p,0x3C)=0x1004B108;RSP(p,0x114)=0x1004B160;RSP(p,0x12C)=0x1004B150;}
static void rs_makar_construct(u32 actor){
 rs_construct_npc(actor);RSP(actor,0xB4)=0x100191F0;rs_bck(actor+0x628);rs_bck(actor+0x6B4);
 rs_array_ctor(actor+0x740,2,0x40,0x024EFE94);rs_status(actor+0x7C0);rs_cylinder(actor+0x7FC,0x10018CF4);rs_cylinder(actor+0x92C,0x10018CF4);rs_joint_ctor(actor+0xA5C);
 RSP(actor,0xB34)=0;RSP(actor,0xB3C)=0x10018D04;RS16(actor,0xB30)=0xFFFF;RS16(actor,0xB32)=0x100;RS8(actor,0xAE3)=0xFF;RS32(actor,0xB38)=0xFFFFFFFFu;
}
static void rs_medli_construct(u32 actor){
 rs_construct_npc(actor);RSP(actor,0xB4)=0x1001E088;rs_btp_ctor(actor+0x63C);rs_btk_ctor(actor+0x6B0);rs_array_ctor(actor+0x724,2,0x40,0x024EFE94);
 const u32 line=actor+0x7A4;rs_line_ctor(line);RS8(line,0x60)=0;RS8(line,0x5D)=0;RS8(line,0x62)=0;RSP(line,0)=line+0x58;RSP(line,0x10)=0x1001D798;RSP(line,4)=line+0x64;RSP(line,0x20)=0x1001D7A8;RS8(line,0x61)=0;RS8(line,0x5F)=1;RSP(line,0x64)=0x1001D7B8;RS8(line,0x5E)=0;RS32(line,0x68)=0x1F;RSP(line,0x58)=0x1001D7C8;RS8(line,0x5C)=0;
 rs_status(actor+0x810);rs_cylinder(actor+0x84C,0x1001D6D8);rs_cylinder(actor+0x97C,0x1001D6D8);rs_cylinder(actor+0xAAC,0x1001D6D8);
 const u32 capsule=actor+0xBDC;rs_object_ctor(capsule);RSP(capsule,0x110)=0x1001D6D8;RSP(capsule,0x114)=0x100015A8;rs_capsule_ctor(capsule+0x118);RSP(capsule,0x3C)=0x1004AF18;RSP(capsule,0x130)=0x1004AF60;RSP(capsule,0x114)=0x1004AF70;
 rs_joint_ctor(actor+0xD14);rs_mirror_ctor(actor+0xD48);RSP(actor,0x4170)=0x100347F0;RSP(actor,0x417C)=0x1001E048;rs_ripple_ctor(actor+0x4198);
 RSP(actor,0x43D0)=0;RSP(actor,0x43D8)=0x1001D6E8;RS16(actor,0x43CC)=0xFFFF;RS16(actor,0x43CE)=0x100;RS32(actor,0x43D4)=0xFFFFFFFFu;
}
