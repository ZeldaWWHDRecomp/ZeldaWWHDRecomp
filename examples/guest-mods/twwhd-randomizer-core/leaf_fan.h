/* Zero-MP fan behavior: TWWHD Randomizer 1.2.0 misc_rando_features.asm.
 * Copyright (c) 2024 SuperDude88. MIT: LICENSE-TWWHD-Randomizer.txt.
 * Native visible path: public CC0 d_a_player_main_02.cpp, setShapeFanLeaf. */
#pragma once
WWHD_REPLACE(0x023E771C,u32,randomizer_leaf_fan_shape,(u32 actor)) {
    u32 material=0;
    const u32 model=*(volatile u32*)(actor+0x4440);
    if(model) {
        const u32 data=*(volatile u32*)(model+0xAC);
        const u32 joints=*(volatile u32*)(data+8);
        material=*(volatile u32*)(joints+0x10);
    }
    const u16 angle=(u16)((s32)*(volatile s16*)(actor+0x69AC)+0x82F);
    const volatile f32* sine=&WWHD_GAME_DATA(0x104A44F8,f32);
    *(volatile s16*)(actor+0x69AC)=(s16)angle;
    *(volatile s16*)(actor+0x69A8)=(s16)(s32)(1536.0f*sine[(angle>>3)*2]);
    *(volatile s16*)(actor+0x69AA)=(s16)(s32)(2048.0f*sine[((u16)(angle-0xE00)>>3)*2]);
    /* Upstream removes only the zero-MP branch. Show every native material and
     * report that the fan is usable; do not change magic or flying eligibility. */
    while(material) {
        const u32 shape=*(volatile u32*)(material+8);
        *(volatile u8*)(shape+4)=1;
        material=*(volatile u32*)(material+4);
    }
    return 1;
}
