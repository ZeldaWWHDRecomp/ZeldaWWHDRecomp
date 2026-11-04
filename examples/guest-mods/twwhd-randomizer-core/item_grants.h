/* Item grant behavior ported from TWWHD Randomizer 1.2.0 custom_funcs.asm.
 * Copyright (c) 2024 SuperDude88. MIT: see LICENSE-TWWHD-Randomizer.txt. */
#pragma once
#include "progressive_ids.h"
#include "dungeon_items.h"

WWHD_GAME_ORIGINAL(0x0254DA38, void, randomizer_native_item_get, (u32 item));
WWHD_GAME_ORIGINAL(0x0254E630, void, randomizer_native_bow, (void));
WWHD_GAME_ORIGINAL(0x0254E888, void, randomizer_native_bombs, (void));
WWHD_GAME_ORIGINAL(0x0254E92C, void, randomizer_native_leaf, (void));

static volatile u8* randomizer_save(void) {
    return (volatile u8*)WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
}

/* The original table's concrete handlers must be called without redispatching
 * through this replacement, or a concrete second tier could advance twice. */
static void randomizer_grant_item(u32 item) {
    if(randomizer_grant_dungeon_item(item))return;
    volatile u8* save=randomizer_save();
    const u32 field=rando_progressive_field(item);
    if(field!=RANDO_NO_PROGRESSIVE_FIELD)
        item=rando_progressive_next(item,save[field],init_progressive_magic_always_double);
    if(item==RANDO_NO_ITEM) return;
    /* The vanilla Hurricane Spin item handler is empty; upstream grants its ownership bit. */
    if(item==0xAA) { wwhd_save_event_set(0x6901,1);return; }
    if(field==0x32) { save[0x32]=(u8)(item==0xAB?1:2);return; }
    if(field==0x90 || field==0x8F) {
        const u8 capacity=(u8)(item==0xAD || item==0xAF?60:99);
        save[field]=save[field-6]=capacity;
        return;
    }
    if(item==0xB1) {
        const u32 play=wwhd_dComIfG_getGameInfo();
        *(volatile u16*)(play+0x5B60)=16;
        *(volatile u16*)(play+0x5B64)=16;
        return;
    }
    if(item==0xB2) { wwhd_item_func_max_mp_up1();return; }
    randomizer_native_item_get(item);
}

WWHD_REPLACE(0x0254DA38, void, randomizer_item_get, (u32 item)) {
    randomizer_grant_item(item);
}

/* These are the source-level equivalents of upstream's removed stores. The
 * inventory and equipment side effects of the native handlers still run. */
WWHD_REPLACE(0x0254E630, void, randomizer_bow, (void)) {
    volatile u8* save=randomizer_save();
    const u8 count=save[0x89], capacity=save[0x8F];
    randomizer_native_bow();
    save[0x89]=count; save[0x8F]=capacity;
}
WWHD_REPLACE(0x0254E888, void, randomizer_bombs, (void)) {
    volatile u8* save=randomizer_save();
    const u8 count=save[0x8A], capacity=save[0x90];
    randomizer_native_bombs();
    save[0x8A]=count; save[0x90]=capacity;
}
WWHD_REPLACE(0x0254E92C, void, randomizer_leaf, (void)) {
    const u32 play=wwhd_dComIfG_getGameInfo();
    volatile u16* magic=(volatile u16*)(play+0x5B60);
    volatile u16* max_magic=(volatile u16*)(play+0x5B64);
    const u16 pending=*magic,pending_max=*max_magic;
    randomizer_native_leaf();
    *magic=pending; *max_magic=pending_max;
}
