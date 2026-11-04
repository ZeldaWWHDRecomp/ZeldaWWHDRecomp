/* Optional full-chart initialization; reveal_sea_chart.asm, TWWHD Randomizer
 * 1.2.0. Copyright (c) 2024 SuperDude88. MIT: LICENSE-TWWHD-Randomizer.txt. */
#pragma once
static void randomizer_reveal_starting_chart(u32 save,u32 enabled) {
    if(!enabled)return; /* Preserve the native initializer's three drawn cells. */
    for(u32 grid=0;grid<49;grid++)
        wwhd_dSv_player_map_c_onFmapBit(save+0xE4,grid,0);
}
