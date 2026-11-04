/* Chart-based salvage light gate from TWWHD Randomizer 1.2.0
 * flexible_item_locations.asm. Copyright (c) 2024 SuperDude88.
 * MIT: LICENSE-TWWHD-Randomizer.txt. Native call flow: public CC0 d_a_salvage.cpp. */
#pragma once
WWHD_GAME_ORIGINAL(0x02467364,u32,randomizer_native_salvage_wait,(u32 actor));
WWHD_GAME_ORIGINAL(0x025B80C8,u32,randomizer_native_salvage_open_map,(u32 map,s32 index));
WWHD_GAME_ORIGINAL(0x025510D0,u32,randomizer_native_salvage_triforce,(u32 item));
WWHD_GAME_ORIGINAL(0x025B8308,u32,randomizer_native_salvage_deciphered,(u32 map,s32 index));

static u32 randomizer_salvage_wait_depth;
static s32 randomizer_salvage_chart_index;
WWHD_REPLACE(0x02467364,u32,randomizer_salvage_wait,(u32 actor)) {
    const u32 depth=randomizer_salvage_wait_depth;
    const s32 chart=randomizer_salvage_chart_index;
    randomizer_salvage_wait_depth=depth+1;
    randomizer_salvage_chart_index=-1;
    const u32 result=randomizer_native_salvage_wait(actor);
    randomizer_salvage_chart_index=chart;
    randomizer_salvage_wait_depth=depth;
    return result;
}

/* proc_wait's kind-0 branch first queries the opened chart, then tests its
 * reward and (conditionally) deciphering. Capture that chart without changing
 * the open/completed gates, reward data, other salvage kinds or native effects. */
WWHD_REPLACE(0x025B80C8,u32,randomizer_salvage_open_map,(u32 map,s32 index)) {
    if(randomizer_salvage_wait_depth)randomizer_salvage_chart_index=index;
    return randomizer_native_salvage_open_map(map,index);
}
WWHD_REPLACE(0x025510D0,u32,randomizer_salvage_triforce,(u32 item)) {
    if(!randomizer_salvage_wait_depth)return randomizer_native_salvage_triforce(item);
    /* HD chart slots 1, 3 and 5 remain decipherable; indices here are zero-based. */
    const s32 chart=randomizer_salvage_chart_index;
    return chart==0 || chart==2 || chart==4;
}
WWHD_REPLACE(0x025B8308,u32,randomizer_salvage_deciphered,(u32 map,s32 index)) {
    if(randomizer_salvage_wait_depth)index=randomizer_salvage_chart_index;
    return randomizer_native_salvage_deciphered(map,index);
}
