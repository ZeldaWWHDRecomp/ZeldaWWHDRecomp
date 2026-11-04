/* Shared presentation interception: each reward requires its native actor context. */
#pragma once
#include "reward_zunari_state.h"
WWHD_GAME_ORIGINAL(0x025D7DEC,u32,randomizer_native_present_item,
    (u32 pos,u32 item,u32 flags,u32 bit,u32 room,u32 angle,u32 scale));
WWHD_REPLACE(0x025D7DEC,u32,randomizer_present_item,
    (u32 pos,u32 item,u32 flags,u32 bit,u32 room,u32 angle,u32 scale)) {
    const int zunari=randomizer_zunari_present_source(pos,item,init_reward_zunari[0]);
    if(randomizer_ivan_actor && pos==randomizer_ivan_actor+0x314 && item==7 &&
        *(volatile s32*)(randomizer_ivan_actor+0x830)==13)
        item=init_reward_ivan;
    if(randomizer_battlesquid_actor && pos==randomizer_battlesquid_actor+0x314) {
        const s32 mode=*(volatile s32*)(randomizer_battlesquid_actor+0x9AC);
        if(mode==6 && item==7) item=init_reward_battlesquid[0];
        else if(mode==6 && item==0xCC) item=init_reward_battlesquid[1];
        else if(mode==9 && item==0xF1) item=init_reward_battlesquid[2];
    }
    const u32 result=randomizer_native_present_item(pos,item,flags,bit,room,angle,scale);
    if(zunari)wwhd_dSv_event_c_onEventBit(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,0x6940);
    return result;
}
