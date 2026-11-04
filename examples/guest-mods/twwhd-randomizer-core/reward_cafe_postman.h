/* Public CC0 a874e1a9 d_a_npc_bm1.cpp/_b.cpp narrow consumers.
 * Source flag6A02: MIT TWWHD Randomizer1.2.0 flexible_item_locations.asm,
 * Copyright(c)2024 SuperDude88. No inventory or reward-item ownership changes. */
#pragma once
WWHD_GAME_FUNC(0x021FC758,s32,randomizer_cafe_set_action,(u32 actor,u32 action,u32 argument));
WWHD_GAME_FUNC(0x025D76A8,u32,randomizer_cafe_speak,(u32 actor));
WWHD_GAME_FUNC(0x025D7A58,u32,randomizer_cafe_order,(u32 actor,s16 event,u8 tool,u16 parameter,u16 priority,u16 flags));
WWHD_REPLACE(0x021FCCB0,u8,randomizer_cafe_initialize,(u32 actor)) {
    if(wwhd_save_event_test(0x6A02)==1)return 0;
    if(wwhd_save_event_test(0x1808)!=1)return 0;
    u32 action[2]={WWHD_GAME_DATA(0x10016F58,u32),WWHD_GAME_DATA(0x10016F5C,u32)};
    (void)randomizer_cafe_set_action(actor,(u32)action,0);
    return 1;
}
WWHD_REPLACE(0x022022B4,u32,randomizer_cafe_message,(u32 actor)) {
    if(*(volatile s8*)(actor+0x9EC)!=0)return 0x1EF7;
    if(wwhd_save_event_test(0x6A02)==1)return 0x1EF9;
    return wwhd_save_event_test(0x1908)==1?0x1EF2:0x1EF0;
}
/* The native entry tail-returns its ordering callback's value, even though its
 * public declaration is void. Retain that register result across flag setting. */
WWHD_REPLACE(0x02200CD0,u32,randomizer_cafe_event_order,(u32 actor)) {
    const s8 condition=*(volatile s8*)(actor+0xA71);
    if(condition==1 || condition==2) {
        *(volatile u16*)(actor+0xFA)|=1;
        if(*(volatile s8*)(actor+0xA76)==9)*(volatile u16*)(actor+0xFA)|=0x20;
        if(*(volatile s8*)(actor+0xA71)==1)return randomizer_cafe_speak(actor);
    } else if(condition>=3) {
        const s16 index=(s16)(condition-3);
        *(volatile s16*)(actor+0x9DA)=index;
        const u32 result=randomizer_cafe_order(actor,*(volatile s16*)(actor+0x9D2+(u32)index*2),0xFF,0xFFFF,0,1);
        /* This is mEventIdx, not mSpecificType. Upstream reads it AFTER the
         * ordering callback; preserve changes that callback makes to it. */
        if(*(volatile s16*)(actor+0x9DA)==0)
            wwhd_dSv_event_c_onEventBit(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,0x6A02);
        return result;
    }
    return actor;
}
