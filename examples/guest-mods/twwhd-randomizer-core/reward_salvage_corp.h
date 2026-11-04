/* Public CC0 a874e1a9 d_a_npc_sv.cpp constructor and eventGetItemInit.
 * Source-flag changes: MIT TWWHD Randomizer1.2.0 flexible_item_locations.asm,
 * Copyright(c)2024 SuperDude88. The seed gift byte101C63BF is already among
 * the validated reward-table writes; retain the live signed-word reload. */
#pragma once
WWHD_GAME_FUNC(0x0273AD10,u32,randomizer_salvage_allocate,(u32 size));
WWHD_GAME_FUNC(0x022E50B0,u8,randomizer_salvage_number,(u32 actor));
WWHD_GAME_FUNC(0x025A1458,void,randomizer_salvage_base_ctor,(u32 actor));
WWHD_GAME_FUNC(0x0254487C,u32,randomizer_salvage_substance,(u32 manager,s32 staff,const char* name,s32 type));
WWHD_GAME_FUNC(0x025D7DEC,u32,randomizer_salvage_present,(u32 position,s32 item,u32 flags,s32 bit,s32 room,u32 angle,u32 scale));
WWHD_REPLACE(0x022E50CC,u32,randomizer_salvage_constructor,(u32 actor)) {
    if(!actor){actor=randomizer_salvage_allocate(0x860);if(!actor)return 0;}
    randomizer_salvage_base_ctor(actor);
    *(volatile u32*)(actor+0xB4)=0x100226A4;
    const u8 number=randomizer_salvage_number(actor);
    *(volatile u8*)(actor+0x851)=0;
    *(volatile u8*)(actor+0x84F)=number;
    *(volatile s16*)(actor+0x83C)=*(volatile s16*)(actor+0x2FA);
    *(volatile u8*)(actor+0x856)=1;
    *(volatile u8*)(actor+0x84D)=0;
    *(volatile u8*)(actor+0x830)=1;
    *(volatile u8*)(actor+0x84E)=0;
    *(volatile s32*)(actor+0x82C)=0;
    *(volatile u8*)(actor+0x855)=0;
    *(volatile u16*)(actor+0x848)=0;
    *(volatile f32*)(actor+0x820)=-1.0f;
    if(wwhd_save_event_test(0x6980)==1)*(volatile u16*)(actor+0x848)|=0x20;
    return actor;
}
WWHD_REPLACE(0x022E5F4C,void,randomizer_salvage_get_item,(u32 actor,s32 staff)) {
    const u32 parameter=randomizer_salvage_substance(wwhd_dComIfG_getGameInfo()+0x52C4,staff,"ItemNo",3);
    /* Native captures room after the event callback, then reads the chosen
     * source. The parameter's contents are intentionally never dereferenced. */
    const s32 room=*(volatile s8*)(actor+0x326);
    const s32 item=parameter?(s32)WWHD_GAME_DATA(0x101C63BC,u32):*(volatile s32*)(actor+0x828);
    const u32 pid=randomizer_salvage_present(actor+0x314,item,0,-1,room,0,0);
    /* Upstream's helper sets the source flag after creation even on failure,
     * preserves the returned PID, then the native consumer stores its partner. */
    wwhd_dSv_event_c_onEventBit(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,0x6980);
    if(pid!=0xFFFFFFFFu)*(volatile u32*)(wwhd_dComIfG_getGameInfo()+0x52A0)=pid;
}
