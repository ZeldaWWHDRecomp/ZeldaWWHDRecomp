/* First-slot source identity from flexible_item_locations.asm (MIT, SuperDude88). */
#pragma once
static s32 randomizer_beedle_bait_slot(u32 actor) {
    return *(volatile s16*)(actor+0x940)==0 &&
        *(volatile u8*)(actor+0x99F)!=4 && *(volatile u8*)(actor+0x99F)!=5 &&
        wwhd_save_event_test(0x6902)==0;
}
