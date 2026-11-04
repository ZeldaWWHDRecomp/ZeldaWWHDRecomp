/* Time progression: public CC0 a874e1a9 d_kankyo_4.cpp setDaytime.
 * Wind Waker gate removal: MIT TWWHD Randomizer1.2.0 make_game_nonlinear.asm,
 * SuperDude88(2024). The replaced ownership call is deliberately absent. */
#pragma once
WWHD_GAME_FUNC(0x02556BC0,s32,randomizer_time_night_stop,(void));
WWHD_GAME_FUNC(0x025542BC,void,randomizer_time_new_day,(void));
WWHD_GAME_FUNC(0x02556D14,s32,randomizer_time_daynight,(void));
WWHD_GAME_FUNC(0x02556C34,s32,randomizer_time_hour,(void));
WWHD_GAME_FUNC(0x02556C68,s32,randomizer_time_minute,(void));
WWHD_GAME_FUNC(0x02554288,s32,randomizer_time_weekday,(void));
WWHD_GAME_FUNC(0x025E1F80,void,randomizer_time_set_hour,(s32 value));
WWHD_GAME_FUNC(0x025E1F90,void,randomizer_time_set_minute,(s32 value));
WWHD_GAME_FUNC(0x025E1FA0,void,randomizer_time_set_weekday,(s32 value));

/* Preserve both resolutions of the literal and then the stage resolution,
 * including the captured literal pointer and the native unterminated bound. */
static int randomizer_time_stage(u32 literal) {
    u32 a[2]={literal,(u32)&WWHD_GAME_DATA(0x1004EF7C,u8)};
    const u32 play=wwhd_dComIfG_getGameInfo();
    u32 b[2]={play+0x5134,(u32)&WWHD_GAME_DATA(0x1004EF7C,u8)};
    ((void(*)(u32))(*(volatile u32*)(a[1]+0x14)))((u32)a);
    ((void(*)(u32))(*(volatile u32*)(a[1]+0x14)))((u32)a);
    const u32 table=b[1];u32 first=a[0];
    ((void(*)(u32))(*(volatile u32*)(table+0x14)))((u32)b);
    u32 second=b[0];
    if(first==second)return 1;
    /* Native pointer identity uses the pre-callback capture, but the byte
     * comparison reloads the literal after the stage resolver returns. */
    first=a[0];
    for(u32 remaining=0x40001;remaining;remaining--) {
        const u8 c1=*(volatile u8*)first,c2=*(volatile u8*)second;
        if(c1!=c2)return 0;if(!c1)return 1;first++;second++;
    }
    return 0;
}
/* Defined PPC fctiwz behavior, also used by the port's runtime: saturate
 * signed overflow and use INT_MIN for NaN before the unsigned reinterpretation. */
static unsigned int randomizer_time_truncate(f32 value) {
    if(value!=value)return 0x80000000u;
    if(value>=2147483648.0f)return 0x7FFFFFFFu;
    if(value<=-2147483648.0f)return 0x80000000u;
    return (unsigned int)(s32)value;
}
WWHD_REPLACE(0x0255B300,void,randomizer_set_daytime,(u32 environment)) {
    *(volatile f32*)(environment+0x1020)=*(volatile f32*)(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x44);
    *(volatile u16*)(environment+0x108A)=*(volatile u16*)(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x48);
    int stop=0;
    if(randomizer_time_stage((u32)&WWHD_GAME_DATA(0x1004F22C,u8)) &&
       (s8)WWHD_GAME_DATA(0x1047E6C8,u8)==14) {
        if(*(volatile s8*)(wwhd_dComIfG_getGameInfo()+0x513F)==2 ||
           *(volatile s8*)(wwhd_dComIfG_getGameInfo()+0x513F)==3)stop=1;
    }
    const s32 night_stop=randomizer_time_night_stop();
    const f32 evening=WWHD_GAME_DATA(0x1004F21C,f32);
    int end_sea;
    if(night_stop || *(volatile u8*)(wwhd_dComIfG_getGameInfo()+0x5292)!=0 || stop) {
        if(randomizer_time_stage((u32)&WWHD_GAME_DATA(0x1004F224,u8))) {
            const f32 next=*(volatile f32*)(environment+0x1020)+*(volatile f32*)(environment+0x1028);
            if(next<evening)*(volatile f32*)(environment+0x1020)=next;
        }
        end_sea=randomizer_time_stage((u32)&WWHD_GAME_DATA(0x1004F230,u8));
    } else {
        (void)wwhd_dComIfG_getGameInfo();
        if(WWHD_GAME_DATA(0x101EBFD8,u8)!=0) {
            const f32 next=*(volatile f32*)(environment+0x1020)+*(volatile f32*)(environment+0x1028);
            const f32 bias=WWHD_GAME_DATA(0x1004F220,f32);
            const unsigned int integer=next<bias?randomizer_time_truncate(next):randomizer_time_truncate(next-bias)+0x80000000u;
            const f32 unsigned_value=(f32)(f64)integer;
            if(!(unsigned_value<WWHD_GAME_DATA(0x1004F108,f32))) {
                *(volatile u16*)(environment+0x108A)=(u16)(*(volatile u16*)(environment+0x108A)+1);
                *(volatile f32*)(environment+0x1020)=WWHD_GAME_DATA(0x1004F0B0,f32);
                randomizer_time_new_day();
            } else *(volatile f32*)(environment+0x1020)=next;
        } else {
            const s32 daynight=randomizer_time_daynight();
            const f32 current=*(volatile f32*)(environment+0x1020);
            const int advance=daynight==0?current<WWHD_GAME_DATA(0x1004F12C,f32):!(current<evening)&&current<WWHD_GAME_DATA(0x1004F118,f32);
            if(advance)*(volatile f32*)(environment+0x1020)=current+*(volatile f32*)(environment+0x1028);
        }
        end_sea=randomizer_time_stage((u32)&WWHD_GAME_DATA(0x1004F230,u8));
    }
    if(end_sea) {
        const f32 current=*(volatile f32*)(environment+0x1020);
        if(current<WWHD_GAME_DATA(0x1004F10C,f32))
            *(volatile f32*)(environment+0x1020)=current+*(volatile f32*)(environment+0x1028);
    }
    randomizer_time_set_hour(randomizer_time_hour());
    randomizer_time_set_minute(randomizer_time_minute());
    randomizer_time_set_weekday(randomizer_time_weekday());
    *(volatile f32*)(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x44)=*(volatile f32*)(environment+0x1020);
    const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    *(volatile u16*)(save+0x48)=*(volatile u16*)(environment+0x108A);
}
