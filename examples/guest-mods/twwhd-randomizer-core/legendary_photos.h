/* CC0 d_a_npc_photo_msg.cpp isPhotoDxOk; MIT randomizer milestone policy.
 * Day2 retains its ordered seven-event short circuit; no saved flags changed. */
#pragma once
WWHD_REPLACE(0x022D014C,s32,randomizer_legendary_photo_available,(u32 actor)) {
    (void)actor;
    switch(wwhd_dKy_get_dayofweek()) {
    case 0:return wwhd_dSv_player_collect_c_getTriforceNum(randomizer_daily_save()+0xD4)==8;
    case 1:return randomizer_daily_boss(2)!=0;
    case 2: {
        const u16 flags[]={0x3002,0x3001,0x3008,0x3004,0x3020,0x3010,0x3180};
        for(u32 i=0;i<7;i++)if(wwhd_dSv_event_c_isEventBit(randomizer_daily_event(),flags[i]))return 1;
        return 0;
    }
    case 3:return wwhd_dSv_memBit_c_isSwitch(randomizer_daily_save()+0x3A0,0x1C)!=0;
    case 4:return wwhd_dSv_player_collect_c_isTact(randomizer_daily_save()+0xD4,1)!=0;
    case 5:return randomizer_daily_boss(7)!=0;
    case 6:return randomizer_daily_boss(6)!=0;
    default:return 0;
    }
}
