/* Complete public CC0 a874e1a9 hd_screen_item_shortcut.cpp consumers.
 * MIT TWWHD Randomizer 1.2.0 fix_vanilla_bugs.asm independent icon bits:
 * baton1, grapple2, bombs4, invalid8 (not the original ordered enum). */
#pragma once
#ifndef R_SHORTCUT
#define R_SHORTCUT(t,a) (*(volatile t*)(a))
#endif
#ifndef SHORTCUT_DATA
#define SHORTCUT_DATA(a) ((u32)&WWHD_GAME_DATA(a,u8))
#endif
WWHD_GAME_FUNC(0x026DD4D0,void,shortcut_set_frame,(u32 self,u32 animation,u32 frame));
WWHD_GAME_FUNC(0x026DD670,void,shortcut_start_animation,(u32 self,u32 animation));
WWHD_GAME_FUNC(0x026DD694,void,shortcut_miiverse,(u32 self));
WWHD_GAME_FUNC(0x02700ADC,u32,shortcut_base_construct,(u32 self));
WWHD_GAME_FUNC(0x0273AD10,u32,shortcut_allocate,(u32 size));
WWHD_REPLACE(0x026DD56C,void,randomizer_shortcut_kind,(u32 self)) {
    const u32 info=wwhd_dComIfG_getGameInfo();
    const s32 ship=(R_SHORTCUT(u32,info+0x5CD8)&0x10000u)!=0;
    u32 kind=0;
    R_SHORTCUT(u32,self+0x54)=0;
    /* One allocation capture, exactly as the native inline inventory accesses. */
    const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    if(ship) {
        if(R_SHORTCUT(u8,save+0x69)==0x31)kind|=4;
        if(R_SHORTCUT(u8,save+0x5F)==0x25)kind|=2;
    }
    if(R_SHORTCUT(u8,save+0x5E)==0x22)kind|=1;
    R_SHORTCUT(u32,self+0x54)=kind;
}
WWHD_REPLACE(0x026DD6C4,void,randomizer_shortcut_in,(u32 self)) {
    shortcut_start_animation(self,0);
    shortcut_set_frame(self,6,R_SHORTCUT(u32,self+0x54)==0?1:0);
    shortcut_set_frame(self,(R_SHORTCUT(u32,self+0x54)&2)?3:1,0);
    shortcut_set_frame(self,(R_SHORTCUT(u32,self+0x54)&4)?4:2,0);
    R_SHORTCUT(u32,self+0x64)=2;
    shortcut_miiverse(self);
}
WWHD_REPLACE(0x026DD894,void,randomizer_shortcut_change,(u32 self)) {
    const u32 prev=R_SHORTCUT(u32,self+0x58);
    R_SHORTCUT(u32,self+0x5C)=9;
    const u32 kind=R_SHORTCUT(u32,self+0x54);
    R_SHORTCUT(u32,self+0x60)=9;
    const u32 changed=prev^kind;
    if(changed&2)R_SHORTCUT(u32,self+0x5C)=(kind&2)?1:3;
    if(changed&4)R_SHORTCUT(u32,self+0x60)=(kind&4)?2:4;
    if(R_SHORTCUT(u32,self+0x5C)!=9)shortcut_start_animation(self,R_SHORTCUT(u32,self+0x5C));
    if(R_SHORTCUT(u32,self+0x60)!=9)shortcut_start_animation(self,R_SHORTCUT(u32,self+0x60));
}
WWHD_REPLACE(0x026DDA04,u32,randomizer_shortcut_construct,(u32 self)) {
    if(!self) {
        self=shortcut_allocate(0x6C);
        if(!self)return 0;
    }
    (void)shortcut_base_construct(self);
    R_SHORTCUT(u32,self+0x58)=8;
    R_SHORTCUT(u32,self+0x54)=8;
    R_SHORTCUT(u32,self+0x30)=SHORTCUT_DATA(0x10104CD8);
    R_SHORTCUT(u32,self+4)=SHORTCUT_DATA(0x10104B88);
    R_SHORTCUT(u32,self+0x64)=2;
    R_SHORTCUT(u32,self+0x50)=0;
    R_SHORTCUT(u32,self+0x5C)=0;
    R_SHORTCUT(u32,self+0x2C)=SHORTCUT_DATA(0x10104CC8);
    R_SHORTCUT(u8,self+0x68)=255;
    R_SHORTCUT(u32,self+0x60)=0;
    return self;
}
