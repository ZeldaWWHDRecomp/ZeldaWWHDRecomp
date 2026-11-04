/* Kogoli init_BMD_1, public CC0 d_a_npc_bm1.cpp at 021FCDB0.
 * Randomizer policy: make_game_nonlinear.asm, MIT, upstream license alongside.
 * Preserve the event query and the complete native action initializer, but do
 * not remove Kogoli merely because Medli is already awakened. No flag mutation.
 */
#pragma once
WWHD_GAME_FUNC(0x025B8B94,u32,randomizer_kogoli_event,(u32 events,u32 flag));
WWHD_GAME_FUNC(0x021FCBD4,u32,randomizer_kogoli_init_action,(u32 actor));
WWHD_REPLACE(0x021FCDB0,u32,randomizer_kogoli_init,(u32 actor)) {
    (void)randomizer_kogoli_event(
        WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,0x1620);
    return randomizer_kogoli_init_action(actor);
}
