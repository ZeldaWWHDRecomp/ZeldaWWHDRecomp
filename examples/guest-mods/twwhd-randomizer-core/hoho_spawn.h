/* CC0 d_a_npc_ah.cpp phase_1, pinned HD fields; upstream hint availability.
 * Only the three switch calls are omitted. The case2 event gate remains. */
#pragma once
WWHD_GAME_FUNC(0x021E5398,u32,randomizer_hoho_ctor,(u32 actor));
WWHD_GAME_FUNC(0x021E5438,u32,randomizer_hoho_argument,(u32 actor));
WWHD_GAME_FUNC(0x021E5470,u32,randomizer_hoho_switch_bit,(u32 actor));
WWHD_REPLACE(0x021E549C,s32,randomizer_hoho_spawn,(u32 actor)) {
    u32 condition=*(volatile unsigned int*)(actor+0x2E4);
    if(!(condition&8)) {
        if(actor){(void)randomizer_hoho_ctor(actor);condition=*(volatile unsigned int*)(actor+0x2E4);}
        *(volatile unsigned int*)(actor+0x2E4)=(unsigned int)(condition|8);
    }
    const u32 argument=randomizer_hoho_argument(actor);
    if(argument==2 || argument==5 || argument==9) {
        if(argument==9)(void)randomizer_hoho_switch_bit(actor);
        /* Original argument loads survive upstream's call-to-li replacement. */
        (void)*(volatile u8*)(actor+0x2FE);
        (void)WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
        if(argument==2 && !wwhd_dSv_event_c_isEventBit(
            WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,0x0520))return 3;
    }
    *(volatile u8*)(actor+0x8BB)=1;
    return 2;
}
