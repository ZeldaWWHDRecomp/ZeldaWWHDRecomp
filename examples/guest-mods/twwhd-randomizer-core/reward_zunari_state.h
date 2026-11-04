/* Shared native presentation and checkItemGet contexts for Zunari's armor source. */
#pragma once
static u32 randomizer_zunari_actor,randomizer_zunari_query;
static inline int randomizer_zunari_item_query(u8 item,s32 flag) {
    if(!randomizer_zunari_query || item!=0x2A || flag!=1)return 0;
    randomizer_zunari_query=0;return 1;
}
static inline int randomizer_zunari_present_source(u32 position,u32 item,u32 reward) {
    return randomizer_zunari_actor && position==randomizer_zunari_actor+0x314 && item==reward;
}
