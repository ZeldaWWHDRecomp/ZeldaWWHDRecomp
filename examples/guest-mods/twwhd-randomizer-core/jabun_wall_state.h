#pragma once
typedef struct {u32 actor;u8 pending;} randomizer_jabun_wall_scope;
static randomizer_jabun_wall_scope randomizer_jabun_wall;
static u32 randomizer_jabun_wall_query(u32 events,u32 flag) {
    if(!randomizer_jabun_wall.pending || flag!=0xA02 ||
       events!=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644)return 0;
    /* Later array-return observers run before the caller marks construction
     * complete. Their story queries must not consume the target decision. */
    if(!randomizer_jabun_wall.actor ||
       !(*(volatile u32*)(randomizer_jabun_wall.actor+0x2E4)&8))return 0;
    /* Consume before the original query, so its callbacks see real flags. */
    randomizer_jabun_wall.pending=0;
    return 1;
}
