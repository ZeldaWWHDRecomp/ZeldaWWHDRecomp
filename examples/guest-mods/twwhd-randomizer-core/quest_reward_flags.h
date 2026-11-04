/* One-time reward bookkeeping from TWWHD Randomizer flexible_item_locations.asm.
 * Copyright (c) 2024 SuperDude88. MIT: see LICENSE-TWWHD-Randomizer.txt.
 * Native branches/layout verified in public CC0 d_a_npc_photo_msg.cpp and d_a_npc_kp1.cpp. */
#pragma once
#include "npc_availability_state.h"
WWHD_GAME_ORIGINAL(0x0227D104,u32,randomizer_native_maggie_message,(u32 actor));
WWHD_REPLACE(0x0227D104,u32,randomizer_maggie_message,(u32 actor)) {
    /* Shown items and just-finished events keep their native priority and flag clearing. */
    if(*(volatile u8*)(actor+0x92E)!=0xFF || *(volatile u8*)(actor+0x92F) ||
       *(volatile u8*)(actor+0x930))return randomizer_native_maggie_message(actor);
    /* Upstream unconditionally skips the old Moblin-letter reserve branch,
     * then tests our first-source flag instead of owning Maggie's Letter. */
    if(wwhd_save_event_test(0x6A01)==1)
        return wwhd_dSv_player_get_bag_item_c_isBeast(
            WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0xB0,0)?0x1E87:0x1E88;
    return *(volatile u8*)(actor+0x931)?0x1E93:0x1E8A;
}

WWHD_GAME_ORIGINAL(0x022D031C,u32,randomizer_native_lenzo_message,(u32 actor));
WWHD_GAME_ORIGINAL(0x02520C0C,u8,randomizer_native_quest_item_owned,(u32 item));
static u32 randomizer_lenzo_first_query;
WWHD_REPLACE(0x022D031C,u32,randomizer_lenzo_message,(u32 actor)) {
    const u32 info=wwhd_dComIfG_getGameInfo();
    const u8 state=*(volatile u8*)(actor+0xB35),button=*(volatile u8*)(info+0x52B0);
    const u32 previous=randomizer_lenzo_first_query;
    /* Native getMsg's 022D05B4 check is the first item query only in this branch.
     * Photo events, shown items and the initial Picto ownership paths stay native. */
    randomizer_lenzo_first_query=!*(volatile u8*)(info+0x52B2) && !(button>=1 && button<=4) &&
        state!=1 && state!=3 && state!=4 && wwhd_save_event_test(0x1601)==1;
    const u32 message=randomizer_native_lenzo_message(actor);
    randomizer_lenzo_first_query=previous;
    return message;
}
WWHD_REPLACE(0x02520C0C,u8,randomizer_quest_item_owned,(u32 item)) {
    const u32 first=randomizer_lenzo_first_query;
    randomizer_lenzo_first_query=0;
    if(first && item==0x26)return (u8)(wwhd_save_event_test(0x6920)==1);
    return randomizer_native_quest_item_owned(item);
}
