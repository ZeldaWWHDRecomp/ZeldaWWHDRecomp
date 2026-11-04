/* Expanded field pickup behavior from field_items.asm (MIT, SuperDude88).
 * Demo collision cleanup follows the public CC0 d_a_item.cpp. */
#pragma once
#include "field_item_logic.h"
WWHD_GAME_ORIGINAL(0x0217DE18,void,randomizer_native_item_pickup,(u32 actor));
WWHD_GAME_ORIGINAL(0x02180DE4,s32,randomizer_native_rupee_action,(u32 actor));

WWHD_REPLACE(0x0217DE18,void,randomizer_field_pickup,(u32 actor)) {
    if(!rando_field_needs_new_demo(*(volatile u8*)(actor+0x74E))) {
        randomizer_native_item_pickup(actor);
        return;
    }
    volatile u8* status=(volatile u8*)(actor+0x785);
    if(*status==5) return; /* STATUS_INIT_NORMAL: already collected */
    *status=7; /* STATUS_INIT_GET_DEMO; grant and item flags commit when the event ends. */
    *(volatile u8*)(actor+0x783)&=(u8)~4u;
    /* Native collision words are big-endian; byte access preserves neighboring flags. */
    for(u32 i=0;i<4;i++) *(volatile u8*)(actor+0x63C+i)=0; /* mCyl.mTgType */
    *(volatile u8*)(actor+0x643)&=(u8)~1u; /* mCyl.mObjCo.mSPrm bit 0 */
    wwhd_dCcD_GObjInf_ClrTgHit((void*)(actor+0x614));
    wwhd_dCcD_GObjInf_ClrCoHit((void*)(actor+0x614));
}

/* The game's sole caller of itemActionForRupee is mode_wait's default case.
 * Keep its shared water/lava tail and all explicitly handled native cases;
 * route its new equipment/bag/chart defaults to the arrow-style idle action. */
WWHD_REPLACE(0x02180DE4,s32,randomizer_field_default_action,(u32 actor)) {
    if(*(volatile u8*)(actor+0x74E)>=0x20)
        return wwhd_daItem_c__itemActionForArrow((void*)actor);
    return randomizer_native_rupee_action(actor);
}
