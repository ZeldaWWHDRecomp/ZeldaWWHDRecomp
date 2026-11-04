/* Public CC0 daNpc_Hr_c::demoProcTact0, d_a_npc_hr_demo.cpp.
 * MIT randomizer flexible_item_locations.asm removes only the AI's hardcoded
 * Wind's Requiem grant. The event remains responsible for its seeded reward.
 */
#pragma once
WWHD_GAME_FUNC(0x025F795C,s32,randomizer_zephos_message_status,(u32 manager));
WWHD_GAME_FUNC(0x025F74D0,void,randomizer_zephos_set_status,(u32 manager,s32 status));
WWHD_GAME_FUNC(0x02543280,void,randomizer_zephos_cut_end,(u32 manager,s32 staff));
WWHD_REPLACE(0x02245138,s32,randomizer_zephos_tact,(u32 actor)) {
    const u32 talking=*(volatile u16*)(actor+0x784)&2;
    if(talking) {
        const u32 message=WWHD_GAME_DATA(0x101F4B5C,u32);
        if(randomizer_zephos_message_status(message)!=14)return 1;
        randomizer_zephos_set_status(message,16);
        *(volatile u8*)(message+0x921)=1;
        *(volatile u16*)(actor+0x784)&=(u16)~0x200;
        if(*(volatile u8*)(wwhd_dComIfG_getGameInfo()+0x5BD3))
            *(volatile u16*)(actor+0x784)|=0x200;
    }
    const s32 staff=*(volatile s32*)(actor+0x7B8);
    randomizer_zephos_cut_end(wwhd_dComIfG_getGameInfo()+0x52C4,staff);
    if(talking)*(volatile u16*)(actor+0x784)&=(u16)~2;
    return 1;
}
