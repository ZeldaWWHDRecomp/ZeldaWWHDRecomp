/* Source-level adapters for make_items_progressive.asm's demo/field paths.
 * Copyright (c) 2024 SuperDude88. MIT: see LICENSE-TWWHD-Randomizer.txt. */
#pragma once
static u32 randomizer_display_item(u32 item) {
    const u32 field=rando_progressive_field(item);
    if(field==RANDO_NO_PROGRESSIVE_FIELD) return item;
    return rando_progressive_display(item,randomizer_save()[field],init_progressive_magic_always_double);
}
WWHD_GAME_ORIGINAL(0x025D7D40,u32,randomizer_native_demo_item,
    (u32 pos,u32 item,u32 bit,u32 angle,u32 room,u32 scale,u32 flags));
WWHD_REPLACE(0x025D7D40,u32,randomizer_demo_item,
    (u32 pos,u32 item,u32 bit,u32 angle,u32 room,u32 scale,u32 flags)) {
    return randomizer_native_demo_item(pos,randomizer_display_item(item),bit,angle,room,scale,flags);
}

/* The low parameter byte is the item ID. The native constructor preserves the
 * incoming parameters and reads this byte before choosing its model resource. */
WWHD_HOOK(0x021805CC,randomizer_field_item,(u32 actor)) {
    volatile u8* item=(volatile u8*)(actor+0xB3);
    *item=(u8)randomizer_display_item(*item);
}

#include "field_item_logic.h"
WWHD_REPLACE(0x0217F1B4,f32,randomizer_field_y_offset,(u32 actor)) {
    return rando_field_y_offset(*(volatile u8*)(actor+0x74E));
}
