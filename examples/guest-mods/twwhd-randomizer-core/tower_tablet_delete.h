/* Public CC0 d_a_obj_hsehi1.cpp/actionDeleteEvent (TotG tablet, not mknjd).
 * MIT make_game_nonlinear.asm: the tablet owns its completion flag. */
#pragma once
WWHD_GAME_FUNC(0x0207A9A0,u8,randomizer_totg_tablet_timer,(u32 timer));
WWHD_GAME_FUNC(0x0235B454,void,randomizer_totg_tablet_draw_stop,(u32 actor));
WWHD_GAME_FUNC(0x025B8B68,void,randomizer_totg_tablet_event_on,(u32 event,u32 flag));
WWHD_REPLACE(0x0235BF0C,s32,randomizer_totg_tablet_delete,(u32 actor,s32 staff)) {
    (void)staff;
    if(randomizer_totg_tablet_timer(actor+0x62D)!=0)return 0;
    randomizer_totg_tablet_draw_stop(actor);
    randomizer_totg_tablet_event_on(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,0x2510);
    return 1;
}
