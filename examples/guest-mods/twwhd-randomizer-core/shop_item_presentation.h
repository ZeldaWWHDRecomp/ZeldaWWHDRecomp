/* Shop model selection from make_items_progressive.asm (MIT, SuperDude88).
 * Keep the original parameter outside construction: the shop uses it for slot identity. */
#pragma once
WWHD_GAME_ORIGINAL(0x024837F8,s32,randomizer_native_shop_create,(u32 actor));
WWHD_REPLACE(0x024837F8,s32,randomizer_shop_create,(u32 actor)) {
    volatile u8* item=(volatile u8*)(actor+0xB3);
    const u8 original=*item;
    const u8 display=(u8)randomizer_display_item(original);
    *item=display;
    const s32 phase=randomizer_native_shop_create(actor);
    if(*item==display) *item=original;
    return phase;
}

/* Match fix_shop_item_y_offsets: newly sellable items must clear the pedestal.
 * Arrow refill pickups deliberately retain their original zero offset. */
WWHD_HOOK(0x028EA120,randomizer_shop_model_offsets,(void)) {
    volatile u8* table=(volatile u8*)&WWHD_GAME_DATA(0x1003A930,u8);
    for(u32 item=0;item<256;item++) {
        volatile f32* y=(volatile f32*)(table+item*0x20+0x10);
        if(*y==0.0f && item!=0x10 && item!=0x11 && item!=0x12) *y=20.0f;
    }
}
