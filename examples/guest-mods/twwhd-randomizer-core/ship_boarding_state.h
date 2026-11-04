/* Only execute's first matching boarding predicate is changed. Native ship
 * checkInitMessage is the other public 1980 consumer and suspends this scope. */
#pragma once
static u32 randomizer_ship_boarding_scope;
static inline int randomizer_ship_boarding_query(u32 events,u32 bit) {
    if(!randomizer_ship_boarding_scope || bit!=0x1980 ||
       events!=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644)return 0;
    randomizer_ship_boarding_scope=0;
    return 1;
}
