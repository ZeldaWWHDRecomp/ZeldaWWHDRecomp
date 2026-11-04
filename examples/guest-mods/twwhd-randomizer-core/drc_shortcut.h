/* Complete CC0 daNpc_Ac1_c::init_AC1_0, public decomp a874e1a9.
 * MIT misc_rando_features: mirror the saved DRC shortcut into the loaded stage. */
#pragma once
#ifndef R_SHORTCUT
#define R_SHORTCUT(t,a) (*(volatile t*)(a))
#endif
WWHD_GAME_FUNC(0x025B8B94,s32,randomizer_shortcut_event,(u32 event,u32 flag));
WWHD_GAME_FUNC(0x021E3074,void,randomizer_shortcut_action,(u32 actor,u32 descriptor,u32 parameter));
WWHD_REPLACE(0x021E31A0,s32,randomizer_drc_shortcut,(u32 actor)) {
    const s32 ready=randomizer_shortcut_event(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,0x2E04)!=0;
    if(ready) {
        (void)randomizer_shortcut_event(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,0x1580);
        const u32 action[2]={WWHD_GAME_DATA(0x10015B28,u32),WWHD_GAME_DATA(0x10015B2C,u32)};
        randomizer_shortcut_action(actor,(u32)action,0);
    }
    const u32 saved=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    if(R_SHORTCUT(u8,saved+0x53C)&0x80) {
        const u32 current=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
        R_SHORTCUT(u8,current+0x7A8)|=0x40;
    }
    return ready;
}
