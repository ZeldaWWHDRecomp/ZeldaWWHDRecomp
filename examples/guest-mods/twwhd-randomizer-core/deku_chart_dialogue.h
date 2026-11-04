/* CC0 d_a_npc_de1.cpp getMsg; MIT make_game_nonlinear.asm0222C9BC.
 * Upstream jumps before both type branches, directly to the question text. */
#pragma once
WWHD_REPLACE(0x0222C998,u32,randomizer_deku_chart_dialogue,(u32 actor)) {
    (void)*(volatile u8*)(actor+0x935); /* Native type read precedes the branch. */
    if(*(volatile u8*)(actor+0x8BF))return 0x13AC;
    *(volatile u8*)(actor+0x8BF)=1;
    return 0x13A8;
}
