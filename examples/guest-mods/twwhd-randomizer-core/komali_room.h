/* Public CC0 daNpc_Co1_c::init_CO1_0 and daKnob00_c::chkException.
 * MIT randomizer make_game_nonlinear.asm: remove the duplicate room Komali
 * from the start, and keep his door unlocked independently of Din's Pearl.
 */
#pragma once
WWHD_GAME_FUNC(0x025B7D90,s32,randomizer_komali_room_symbol,(u32 collect,u32 symbol));
WWHD_REPLACE(0x02227D74,u32,randomizer_komali_room_init,(u32 actor)) {
    (void)actor;
    /* The upstream branch change retains this query and its callbacks, but
     * skips status changes and action setup regardless of its result. */
    (void)randomizer_komali_room_symbol(
        WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0xD4,1);
    return 0;
}
WWHD_GAME_ORIGINAL(0x021A4B04,s32,randomizer_native_knob_exception,(u32 actor));
WWHD_REPLACE(0x021A4B04,s32,randomizer_knob_exception,(u32 actor)) {
    /* Native dispatch does only the angle read/comparisons before this case.
     * Mesa changes only for seeds carrying the separate door-shuffle patch. */
    if(*(volatile s16*)(actor+0x2FC)==0x6AA)return 1;
    if(init_mesa_door_unlocked && *(volatile s16*)(actor+0x2FC)==0x6A7)return 1;
    return randomizer_native_knob_exception(actor);
}
