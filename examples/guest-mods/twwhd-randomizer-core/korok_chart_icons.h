/* CC0 hd_screen_sea_chart_menu.cpp updateChartIconFrames.
 * MIT make_game_nonlinear.asm026790B0 replaces only the bottle query with1. */
#pragma once
WWHD_GAME_FUNC(0x0268936C,s32,randomizer_korok_chart_busy,(u32 ui));
WWHD_GAME_FUNC(0x02678B00,void,randomizer_korok_chart_hide,(u32 actor));
WWHD_GAME_FUNC(0x02005674,void,randomizer_korok_chart_operate,(u32 animations,u32 animation,u32 group));
WWHD_GAME_FUNC(0x0200552C,void,randomizer_korok_chart_frame,(u32 animations,u32 animation,u32 group,f32 frame));
static u32 randomizer_korok_animations(u32 actor) {
    const u32 owner=*(volatile u32*)(actor+0x44);return *(volatile u32*)(owner+0xD4);
}
WWHD_REPLACE(0x02678F70,void,randomizer_korok_chart_icons,(u32 actor)) {
    const u32 graphics=WWHD_GAME_DATA(0x101F8344,u32),ui=*(volatile u32*)(graphics+0x218);
    if(randomizer_korok_chart_busy(ui)){randomizer_korok_chart_hide(actor);return;}
    if(wwhd_dSv_event_c_isEventBit(randomizer_daily_event(),0x0102)) {
        for(u32 i=0;i<8;i++)randomizer_korok_chart_operate(randomizer_korok_animations(actor),0x62+i,0x63+i);
        return;
    }
    if(!wwhd_dSv_event_c_isEventBit(randomizer_daily_event(),0x3940))return;
    const f32 collected=WWHD_GAME_DATA(0x100F7E88,f32),available=WWHD_GAME_DATA(0x100F7E70,f32);
    (void)WWHD_GAME_DATA(0x100F7E84,f32);
    const u8 order[]={2,4,0,7,5,1,6,3};
    for(u32 i=0;i<8;i++) {
        (void)randomizer_daily_save(); /* Argument load retained before replaced call. */
        u32 complete=1;
        if(!wwhd_dSv_event_c_isEventBit(randomizer_daily_event(),0x0102))
            complete=(wwhd_dSv_event_c_getEventReg(randomizer_daily_event(),0x9EFF)>>i)&1;
        randomizer_korok_chart_frame(randomizer_korok_animations(actor),0x62+order[i],0x63+order[i],complete?collected:available);
    }
}
