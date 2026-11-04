/* Field-item positioning from TWWHD Randomizer 1.2.0 field_items.asm.
 * Copyright (c) 2024 SuperDude88. MIT: see LICENSE-TWWHD-Randomizer.txt. */
#pragma once
static inline float rando_field_y_offset(unsigned item) {
    if(item==0x38) return 20.0f;
    if(item<=0x1F || (item>=0x45 && item<=0x4B)) return 0.0f;
    return 23.0f;
}

/* Existing sword/shield, consumable, spoils and bait behavior stays native.
 * These are the additional itemGetExecute branches enabled by field_items.asm. */
static inline int rando_field_needs_new_demo(unsigned item) {
    if(item>=0x16 && item<=0x1D) return 1;
    if(item>=0x20 && item<=0x37) return 1;
    if(item>=0x39 && item<=0x44 && item!=0x3B && item!=0x3D) return 1;
    return item>=0x4C && item<=0xFF && item!=0x82 && item!=0x83;
}
