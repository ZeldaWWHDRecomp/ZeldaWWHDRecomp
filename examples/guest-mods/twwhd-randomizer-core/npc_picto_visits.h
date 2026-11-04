/* CC0 d_a_npc_mk/uk visit methods; MIT flexible_item_locations four branches.
 * Retain the unrelated first story event and the final1208 query, but skip
 * ownership-driven special events without changing inventory or event flags. */
#pragma once
WWHD_GAME_FUNC(0x02299290,void,randomizer_mk_visit_demo,(u32 actor));
WWHD_GAME_FUNC(0x022F40EC,void,randomizer_uk_visit_demo,(u32 actor));
WWHD_GAME_FUNC(0x025D5218,u32,randomizer_visit_search,(u32 callback,u32 id));
WWHD_GAME_FUNC(0x0253F124,u32,randomizer_visit_partner,(u32 events,u32 partner));
WWHD_GAME_FUNC(0x0253E9B0,void,randomizer_visit_event_name,(u32 info,u32 name));
static s32 randomizer_visit_first(void) {
    return wwhd_dSv_event_c_isEventBit(randomizer_daily_event(),0x1F80) &&
        !wwhd_dSv_event_c_isEventBit(randomizer_daily_event(),0x1E02);
}
WWHD_REPLACE(0x02299358,u32,randomizer_mk_visit_talk,(u32 actor)) {
    if(randomizer_visit_first()) {
        const u16 event=*(volatile u16*)(actor+0xFC);
        *(volatile u8*)(actor+0x835)=12;*(volatile u16*)(actor+0x85E)=event;
        randomizer_mk_visit_demo(actor);
        wwhd_dSv_event_c_onEventBit(randomizer_daily_event(),0x1E04);return 12;
    }
    (void)wwhd_dSv_event_c_isEventBit(randomizer_daily_event(),0x1208);return 1;
}
WWHD_REPLACE(0x022F41B4,u32,randomizer_uk_visit_talk,(u32 actor)) {
    if(randomizer_visit_first()) {
        const u16 event=*(volatile u16*)(actor+0xFC);
        unsigned int id=*(volatile unsigned int*)(actor+0x804);
        *(volatile u16*)(actor+0x85A)=event;
        u32 partner=0;if(id!=0xFFFFFFFF)partner=randomizer_visit_search(0x025E1234,(u32)&id);
        const u32 events=wwhd_dComIfG_getGameInfo()+0x51D0;
        const u32 result=randomizer_visit_partner(events,partner);
        *(volatile u32*)(events+0xCC)=result;
        *(volatile u8*)(actor+0x82D)=4;randomizer_uk_visit_demo(actor);
        wwhd_dSv_event_c_onEventBit(randomizer_daily_event(),0x1E04);return 4;
    }
    (void)wwhd_dSv_event_c_isEventBit(randomizer_daily_event(),0x1208);return 1;
}
static void randomizer_visit_name(u32 actor,u32 special,u32 ordinary) {
    if(randomizer_visit_first())randomizer_visit_event_name(actor+0xF8,special);
    else {
        (void)wwhd_dSv_event_c_isEventBit(randomizer_daily_event(),0x1208);
        randomizer_visit_event_name(actor+0xF8,ordinary);
    }
}
WWHD_REPLACE(0x022994F0,void,randomizer_mk_visit_event,(u32 actor)) {
    randomizer_visit_name(actor,(u32)&WWHD_GAME_DATA(0x1001E4C8,u8),(u32)&WWHD_GAME_DATA(0x1001E4C0,u8));
}
WWHD_REPLACE(0x022F5088,void,randomizer_uk_visit_event,(u32 actor)) {
    randomizer_visit_name(actor,(u32)&WWHD_GAME_DATA(0x100233A4,u8),(u32)&WWHD_GAME_DATA(0x1002339C,u8));
}
