/* Full public CC0 a874e1a9 d_a_tbox.cpp actionOpenWait (024B7394).
 * MIT randomizer remove_cutscenes.asm removes the nonwooden buildup/light
 * branch and always requests the short event; demo-item/native callbacks stay. */
#pragma once
#ifndef R_FAST_CHEST
#define R_FAST_CHEST(t,a) (*(volatile t*)(a))
#endif
#ifndef FAST_CHEST_DATA
#define FAST_CHEST_DATA(a) ((u32)&WWHD_GAME_DATA(a,u8))
#endif
WWHD_GAME_FUNC(0x025D7E88,u32,fast_chest_demo_item,(u32 pos,u32 item,s32 room,s32 flag,u32 angle,u32 scale));
WWHD_GAME_FUNC(0x02542D88,s32,fast_chest_staff,(u32 manager,u32 name,u32 actor,u32 id));
WWHD_GAME_FUNC(0x024B4314,s32,fast_chest_shape,(u32 self));
WWHD_GAME_FUNC(0x024B6F9C,s32,fast_chest_demo,(u32 self));
WWHD_GAME_FUNC(0x024B72C8,s32,fast_chest_box_check,(u32 self));
WWHD_GAME_FUNC(0x0253E9B0,void,fast_chest_event_name,(u32 info,u32 name));
WWHD_REPLACE(0x024B7394,s32,randomizer_fast_chest_wait,(u32 self)){
    if(R_FAST_CHEST(u16,self+0xF8)==3){
        u32 info=wwhd_dComIfG_getGameInfo();
        R_FAST_CHEST(u16,info+0x52B8)|=4;
        const u32 item=((u32)(s32)R_FAST_CHEST(s16,self+0x2FC)>>8)&255;
        const u32 id=fast_chest_demo_item(self+0x314,item,-1,-1,0,0);
        if(id!=0xFFFFFFFFu){info=wwhd_dComIfG_getGameInfo();R_FAST_CHEST(u32,info+0x52A0)=id;}
        (void)fast_chest_shape(self); /* Upstream retains the real query. */
        R_FAST_CHEST(s16,self+0x6D6)=-1;R_FAST_CHEST(u32,self+0x6D8)=0x024B7708;R_FAST_CHEST(s16,self+0x6D4)=0;
        info=wwhd_dComIfG_getGameInfo();
        R_FAST_CHEST(s32,self+0x6E0)=fast_chest_staff(info+0x52C4,FAST_CHEST_DATA(0x10040014),0,0);
        (void)fast_chest_demo(self);
    }else if(fast_chest_box_check(self)){
        R_FAST_CHEST(u16,self+0xFA)|=4;
        (void)fast_chest_shape(self);
        fast_chest_event_name(self+0xF8,FAST_CHEST_DATA(0x10040020));
    }
    return 1;
}
/* Source data policy: only the three nonwooden openBckId halfwords differ.
 * Seed preparation validates the entire table against canonical+these edits. */
WWHD_HOOK(0x028EA120,randomizer_initialize_fast_chest_animations,(void)){
    WWHD_GAME_DATA(0x101D232E,u16)=9;
    WWHD_GAME_DATA(0x101D233A,u16)=9;
    WWHD_GAME_DATA(0x101D2346,u16)=9;
}
