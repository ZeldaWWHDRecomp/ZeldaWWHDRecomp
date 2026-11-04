/* Complete CC0 a874e1a9 d_a_npc_ym1_a2.cpp createInit, with MIT classic
 * coordinate relocation omitted. Other initialization/physics callbacks stay. */
#pragma once
#ifndef WWHD_RANDOMIZER_CLASSIC_MODE
#error "Regenerate seed data: missing validated classic mode"
#elif WWHD_RANDOMIZER_CLASSIC_MODE != 0 && WWHD_RANDOMIZER_CLASSIC_MODE != 1
#error "Invalid validated classic mode"
#elif WWHD_RANDOMIZER_CLASSIC_MODE
#ifndef R_CLASSIC_ABE
#define R_CLASSIC_ABE(t,a) (*(volatile t*)(a))
#endif
#ifndef CLASSIC_ABE_DATA
#define CLASSIC_ABE_DATA(a) ((u32)&WWHD_GAME_DATA(a,u8))
#endif
WWHD_GAME_FUNC(0x0259F814,void,classic_abe_staff,(u32 event,u32 name,u32 self));
WWHD_GAME_FUNC(0x022F74B4,u32,classic_abe_init0,(u32 self));
WWHD_GAME_FUNC(0x022F75E4,u32,classic_abe_init1,(u32 self));
WWHD_GAME_FUNC(0x022F7668,u32,classic_abe_init2,(u32 self));
WWHD_GAME_FUNC(0x022F7710,u32,classic_abe_init3,(u32 self));
WWHD_GAME_FUNC(0x022F77B8,u32,classic_abe_init4,(u32 self));
WWHD_GAME_FUNC(0x022F7848,u32,classic_abe_init5,(u32 self));
WWHD_GAME_FUNC(0x022F78D8,u32,classic_abe_init_error,(u32 self));
WWHD_GAME_FUNC(0x02515F14,void,classic_abe_status,(u32 status,u32 weight,u32 flags,u32 self));
WWHD_GAME_FUNC(0x02516518,void,classic_abe_cylinder,(u32 cylinder,u32 source));
WWHD_GAME_FUNC(0x028E93CC,void,classic_abe_translate,(u32 matrix,f32 x,f32 y,f32 z));
WWHD_GAME_FUNC(0x025F1C28,void,classic_abe_rotate,(u32 matrix,s32 angle));
WWHD_GAME_FUNC(0x028E8F64,void,classic_abe_mult,(u32 matrix,u32 input,u32 output));
WWHD_GAME_FUNC(0x020182E0,void,classic_abe_center,(u32 cylinder,u32 center));
WWHD_GAME_FUNC(0x020184DC,void,classic_abe_radius,(u32 cylinder,f32 radius));
WWHD_GAME_FUNC(0x02018428,void,classic_abe_height,(u32 cylinder,f32 height));
WWHD_GAME_FUNC(0x025200D4,u32,classic_abe_play,(void));
WWHD_GAME_FUNC(0x02516C14,void,classic_abe_mass,(u32 manager,u32 cylinder,u32 priority));
WWHD_GAME_FUNC(0x022F79D0,void,classic_abe_animation,(u32 self));
WWHD_GAME_FUNC(0x024F08A8,void,classic_abe_correct,(u32 acch,u32 bg));
WWHD_GAME_FUNC(0x024EF130,u32,classic_abe_room,(u32 bg,u32 poly));
WWHD_GAME_FUNC(0x024EEEB8,u32,classic_abe_color,(u32 bg,u32 poly));
WWHD_GAME_FUNC(0x025E4A54,void,classic_abe_morph,(u32 morph,f32 value));
WWHD_GAME_FUNC(0x022F7B28,void,classic_abe_matrix,(u32 self,u32 enabled));
WWHD_REPLACE(0x022F7D68,u32,randomizer_classic_abe_init,(u32 self)){
    const s32 staff=R_CLASSIC_ABE(s8,self+0xA21);
    const u32 name=R_CLASSIC_ABE(u32,CLASSIC_ABE_DATA(0x101C6A38)+(u32)(staff*4));
    classic_abe_staff(self+0x3E0,name,self);
    const f32 x=R_CLASSIC_ABE(f32,self+0x314),target_x=R_CLASSIC_ABE(f32,CLASSIC_ABE_DATA(0x100235F4));
    R_CLASSIC_ABE(u32,self+0x39C)=10;
    const s32 subtype=R_CLASSIC_ABE(s8,self+0xA20);
    const f32 threshold=R_CLASSIC_ABE(f32,CLASSIC_ABE_DATA(0x100235F0));
    const u32 distances=CLASSIC_ABE_DATA(0x101C6A30)+(u32)(subtype*2);
    R_CLASSIC_ABE(u8,self+0x389)=R_CLASSIC_ABE(u8,distances);
    const u8 speak=R_CLASSIC_ABE(u8,distances+1);
    R_CLASSIC_ABE(s8,self+0xA1B)=13;R_CLASSIC_ABE(u8,self+0x38B)=speak;
    /* Retain the pre-branch coordinate/constant reads. The patched unconditional
     * branch skips all position/old-position stores, including unordered inputs. */
    const f32 delta=x-target_x;
    const f32 absolute=delta<0?-delta:delta;
    if(absolute<threshold){
        (void)R_CLASSIC_ABE(f32,self+0x31C);
        (void)R_CLASSIC_ABE(f32,CLASSIC_ABE_DATA(0x100235F8));
    }
    u32 initialized=0;
    switch(R_CLASSIC_ABE(s8,self+0xA21)){
    case 0:initialized=classic_abe_init0(self);break;
    case 1:initialized=classic_abe_init1(self);break;
    case 2:initialized=classic_abe_init2(self);break;
    case 3:initialized=classic_abe_init3(self);break;
    case 4:initialized=classic_abe_init4(self);break;
    case 5:initialized=classic_abe_init5(self);break;
    case 6:case 7:initialized=classic_abe_init_error(self);break;
    }
    if(!initialized)return 0;
    const u16 ax=R_CLASSIC_ABE(u16,self+0x320),ay=R_CLASSIC_ABE(u16,self+0x322);
    R_CLASSIC_ABE(u16,self+0x328)=ax;R_CLASSIC_ABE(u16,self+0x9B8)=ay;R_CLASSIC_ABE(u16,self+0x9B6)=ax;
    const f32 gravity=R_CLASSIC_ABE(f32,CLASSIC_ABE_DATA(0x10023608));
    const u16 az=R_CLASSIC_ABE(u16,self+0x324);R_CLASSIC_ABE(f32,self+0x374)=gravity;
    R_CLASSIC_ABE(u16,self+0x9BA)=az;R_CLASSIC_ABE(u16,self+0x32C)=az;R_CLASSIC_ABE(u16,self+0x32A)=ay;
    classic_abe_status(self+0x654,255,255,self);
    R_CLASSIC_ABE(u32,self+0x6D4)=self+0x654;
    classic_abe_cylinder(self+0x690,CLASSIC_ABE_DATA(0x101EA190));
    const s8 current_staff=R_CLASSIC_ABE(s8,self+0xA21);
    const f32 zero=R_CLASSIC_ABE(f32,CLASSIC_ABE_DATA(0x100234D4));
    if(current_staff==0){
        f32 offset[3]={zero,zero,R_CLASSIC_ABE(f32,CLASSIC_ABE_DATA(0x1002360C))},center[3];
        R_CLASSIC_ABE(u32,self+0x8B8)=self+0x654;
        classic_abe_cylinder(self+0x874,CLASSIC_ABE_DATA(0x101EA190));
        const u32 matrix=CLASSIC_ABE_DATA(0x1048D0CC);
        const f32 z=R_CLASSIC_ABE(f32,self+0x31C),px=R_CLASSIC_ABE(f32,self+0x314),y=R_CLASSIC_ABE(f32,self+0x318);
        classic_abe_translate(matrix,px,y,z);
        classic_abe_rotate(matrix,R_CLASSIC_ABE(s16,self+0x322));
        classic_abe_mult(matrix,(u32)offset,(u32)center);
        classic_abe_center(self+0x98C,(u32)center);
        classic_abe_radius(self+0x98C,R_CLASSIC_ABE(f32,CLASSIC_ABE_DATA(0x10023610)));
        classic_abe_height(self+0x98C,R_CLASSIC_ABE(f32,CLASSIC_ABE_DATA(0x100235CC)));
        classic_abe_mass(classic_abe_play()+0x4EF8,self+0x874,3);
    }
    classic_abe_animation(self);
    if(R_CLASSIC_ABE(s8,self+0xA21)!=1)classic_abe_correct(self+0x450,classic_abe_play()+0x12A0);
    const u32 room=classic_abe_room(classic_abe_play()+0x12A0,self+0x538);R_CLASSIC_ABE(u8,self+0x1C9)=(u8)room;
    const u32 color=classic_abe_color(classic_abe_play()+0x12A0,self+0x538);R_CLASSIC_ABE(u8,self+0x1CA)=(u8)color;
    classic_abe_morph(R_CLASSIC_ABE(u32,self+0x44C),zero);classic_abe_matrix(self,1);return 1;
}
#endif
