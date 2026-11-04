/* CC0 d_kankyo.cpp DayProc. MIT remove_cutscenes.asm replaces only two
 * story-event predicates with stage2 boss completion. Other daily work stays. */
#pragma once
WWHD_GAME_FUNC(0x02586E94,void,randomizer_daily_stock,(u32 letter));
WWHD_GAME_FUNC(0x02586E58,void,randomizer_daily_delivery,(u32 letter));
WWHD_GAME_FUNC(0x025B7840,u32,randomizer_daily_reserve_owned,(u32 bag,u32 slot));
/* Use actual native boss completion, avoiding unrelated source hint adapters. */
WWHD_GAME_ORIGINAL(0x02520A84,u32,randomizer_daily_boss,(u32 stage));
static u32 randomizer_daily_save(void){return WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);}
static u32 randomizer_daily_event(void){return randomizer_daily_save()+0x644;}
static void randomizer_daily_increment(u32 selector,u32 max) {
    const u32 value=(u32)wwhd_dSv_event_c_getEventReg(randomizer_daily_event(),selector)+1;
    const u32 event=randomizer_daily_event();
    wwhd_dSv_event_c_setEventReg(event,selector,value>max?max:value);
}
static void randomizer_daily_off(u32 flag){wwhd_dSv_event_c_offEventBit(randomizer_daily_event(),flag);}
static void randomizer_daily_tmp_off(u32 flag){wwhd_dSv_event_c_offEventBit(randomizer_daily_save()+0x1178,flag);}
WWHD_REPLACE(0x025542BC,void,randomizer_daily_mail,(void)) {
    if(randomizer_daily_boss(2))randomizer_daily_stock(0x8B03);
    if(randomizer_daily_boss(2) && wwhd_dSv_event_c_isEventBit(randomizer_daily_event(),0x0B80))randomizer_daily_stock(0xB203);
    const u16 deliveries[]={0xAC03,0xAE03,0xB003,0xAF03,0x9D03};
    for(u32 i=0;i<5;i++)randomizer_daily_delivery(deliveries[i]);
    randomizer_daily_increment(0xCF03,3);randomizer_daily_increment(0xAB03,3);
    if(randomizer_daily_reserve_owned(randomizer_daily_save()+0xB0,15) &&
       !wwhd_dSv_player_bag_item_c_checkReserveItem(randomizer_daily_save()+0x96,0x9B))randomizer_daily_increment(0xCCFF,2);
    const u16 resets[]={0xBCFF,0xCB03,0xCA03,0xC903};
    for(u32 i=0;i<4;i++)wwhd_dSv_event_c_setEventReg(randomizer_daily_event(),resets[i],0);
    randomizer_daily_off(0x1304);randomizer_daily_off(0x1302);randomizer_daily_off(0x1301);
    const u16 temporary[]={0x0120,0x0040,0x0020,0x0010,0x0008,0x0004,0x0002,0x0001,0x0180,0x0140,0x0580,0x0104};
    for(u32 i=0;i<12;i++)randomizer_daily_tmp_off(temporary[i]);
    if(wwhd_dSv_event_c_getEventReg(randomizer_daily_event(),0xC103)==1)wwhd_dSv_event_c_setEventReg(randomizer_daily_event(),0xC103,2);
    if(wwhd_dSv_event_c_isEventBit(randomizer_daily_event(),0x1F10)) {
        const u32 value=wwhd_dSv_event_c_getEventReg(randomizer_daily_event(),0xBB07);
        const u32 event=randomizer_daily_event();
        if(value==7)wwhd_dSv_event_c_onEventBit(event,0x1F08);
        else wwhd_dSv_event_c_setEventReg(event,0xBB07,value+1>7?7:value+1);
    }
    if(wwhd_dSv_event_c_getEventReg(randomizer_daily_event(),0xC407)==6)wwhd_dSv_event_c_setEventReg(randomizer_daily_event(),0xC407,7);
    const u8 value=wwhd_dSv_event_c_getEventReg(randomizer_daily_event(),0xB907);
    if(value&1)wwhd_dSv_event_c_setEventReg(randomizer_daily_event(),0xB907,(u8)(value+1));
    randomizer_daily_off(0x2680);
    if(wwhd_dSv_event_c_isEventBit(randomizer_daily_event(),0x2A20))randomizer_daily_increment(0xA60F,3);
    if(wwhd_dKy_get_dayofweek()==5) {
        const u16 flags[]={0x2080,0x2004,0x2002,0x2804,0x2802,0x2801,0x2980,0x2940,0x3B01,0x3C80,0x3C40,0x3C20,0x3C10,0x3C08,0x3C04,0x3C02};
        for(u32 i=0;i<16;i++)randomizer_daily_off(flags[i]);
    }
    randomizer_daily_tmp_off(0x0208);
    if(wwhd_dSv_event_c_isEventBit(randomizer_daily_event(),0x2F01))wwhd_dSv_event_c_onEventBit(randomizer_daily_event(),0x3080);
    randomizer_daily_tmp_off(0x0302);randomizer_daily_tmp_off(0x0301);
}
