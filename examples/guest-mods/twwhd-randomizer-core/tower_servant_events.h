/* Public CC0 d_a_npc_os_a.cpp/eventOrderCheck and d_a_npc_os_c.cpp/finish02NpcAction.
 * MIT make_game_nonlinear.asm: servants can finish in any order. */
#pragma once
WWHD_GAME_FUNC(0x025BA0C0,s32,randomizer_servant_switch,(u32 info,s32 number,s32 room));
WWHD_GAME_FUNC(0x025B8B94,s32,randomizer_servant_event,(u32 event,u32 flag));
WWHD_GAME_FUNC(0x022ADA3C,u32,randomizer_servant_finish_animation,(u32 actor,s32 animation));
WWHD_GAME_ORIGINAL(0x022AE980,s32,randomizer_native_servant_finish,(u32 actor,u32 parameter));
WWHD_REPLACE(0x022AC030,void,randomizer_servant_event_order,(u32 actor)) {
    if((s8)*(volatile u8*)(actor+0x915)!=-1)return;
    const s32 room=(s8)*(volatile u8*)(actor+0x326);
    const s32 argument=(s8)*(volatile u8*)(actor+0x2DD);
    /* The east servant no longer schedules the tablet-disappearance event. */
    if((argument==1 || argument==2) && room==7 &&
       randomizer_servant_switch(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x20,
                                (s32)*(volatile u32*)(actor+0x904),room) &&
       !randomizer_servant_event(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,
                                argument==1?0x2608:0x2604))
        *(volatile u8*)(actor+0x915)=(u8)(argument==1?8:9);
}
WWHD_REPLACE(0x022AE980,s32,randomizer_servant_finish,(u32 actor,u32 parameter)) {
    if((s8)*(volatile u8*)(actor+0x919)!=0)
        return randomizer_native_servant_finish(actor,parameter);
    if(!parameter) {
        (void)randomizer_servant_event(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,0x1B01);
        (void)randomizer_servant_finish_animation(actor,7);
    }
    *(volatile u32*)(actor+0x8F4)|=1;
    *(volatile f32*)(actor+0x378)=WWHD_GAME_DATA(0x1001F644,f32);
    *(volatile f32*)(actor+0x374)=WWHD_GAME_DATA(0x10467ECC,f32);
    *(volatile f32*)(actor+0x8F8)=WWHD_GAME_DATA(0x1001F594,f32);
    const u8 state=*(volatile u8*)(actor+0x919);
    *(volatile f32*)(actor+0x370)=WWHD_GAME_DATA(0x1001F548,f32);
    *(volatile u8*)(actor+0x919)=(u8)(state+1);
    return 1;
}
