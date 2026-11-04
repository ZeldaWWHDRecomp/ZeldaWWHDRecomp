/* CC0 d_save_5.cpp reinit; upstream make_game_nonlinear.asm omits five
 * pictograph-completion operations. Keep all other initialization and callbacks. */
#pragma once
WWHD_GAME_FUNC(0x025B9A18,void,randomizer_ng_init,(u32 info));
WWHD_GAME_FUNC(0x027200D0,u32,randomizer_ng_progress,(u32 hd));
WWHD_GAME_FUNC(0x02720144,u32,randomizer_ng_picture,(u32 hd));
WWHD_GAME_FUNC(0x027262B0,void,randomizer_ng_picture_count,(u32 picture,u32 count));
WWHD_GAME_FUNC(0x025B8CE0,void,randomizer_ng_switch,(u32 memory,u32 bit));
static u32 randomizer_ng_save(void){return WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);}
WWHD_REPLACE(0x025B9A90,void,randomizer_ng_reinit,(u32 info)) {
    u8 hold[17];
    const u32 before=randomizer_ng_save();
    for(u32 i=0;i<17;i++)hold[i]=wwhd_dSv_event_c_getEventReg(before+0x644,
        *(volatile u16*)((u32)&WWHD_GAME_DATA(0x101EADA8,u8)+i*2));
    u32 save=randomizer_ng_save();
    const u8 clear=*(volatile u8*)(save+0x1C0);
    u32 p=randomizer_ng_progress(save+0x12C0);
    save=randomizer_ng_save();const u8 first=*(volatile u8*)p;
    p=randomizer_ng_progress(save+0x12C0);
    save=randomizer_ng_save();const u8 second=*(volatile u8*)(p+3);
    const u8 vibration=*(volatile u8*)(save+0x1C7),sound=*(volatile u8*)(save+0x1C5);
    p=randomizer_ng_picture(save+0x12C0);
    const u32 event=randomizer_ng_save()+0x644;
    const u8 pictures=*(volatile u8*)(p+0x3C030C);
    const u32 reg89=wwhd_dSv_event_c_getEventReg(event,0x89FF);
    randomizer_ng_init(info);
    for(u32 i=0;i<17;i++)wwhd_dSv_event_c_setEventReg(randomizer_ng_save()+0x644,
        *(volatile u16*)((u32)&WWHD_GAME_DATA(0x101EADA8,u8)+i*2),hold[i]);
    for(u32 i=0;i<5;i++)wwhd_dSv_event_c_onEventBit(randomizer_ng_save()+0x644,
        *(volatile u16*)((u32)&WWHD_GAME_DATA(0x101EADCC,u8)+i*2));
    /* Omit C407=7 and misc onTbox0. */
    *(volatile u8*)(randomizer_ng_save()+0x1C0)=clear;
    randomizer_ng_switch(randomizer_ng_save()+0x3A0,0x47);
    randomizer_ng_switch(randomizer_ng_save()+0x3A0,0x5E);
    p=randomizer_ng_progress(randomizer_ng_save()+0x12C0);if(first<2)*(volatile u8*)p=first;
    p=randomizer_ng_progress(randomizer_ng_save()+0x12C0);if(second<2)*(volatile u8*)(p+3)=second;
    *(volatile u8*)(randomizer_ng_save()+0x1C5)=sound;
    *(volatile u8*)(randomizer_ng_save()+0x1C7)=vibration;
    *(volatile u8*)(randomizer_ng_save()+0x1C1)=3;
    /* Omit inventory ownership, but retain native play selection fields. */
    const u32 play=wwhd_dComIfG_getGameInfo();
    *(volatile u8*)(play+0x5BC8)=0x26;*(volatile u8*)(play+0x5BC7)=8;
    p=randomizer_ng_picture(randomizer_ng_save()+0x12C0);
    randomizer_ng_picture_count(p,pictures);
    wwhd_dSv_event_c_setEventReg(randomizer_ng_save()+0x644,0x89FF,reg89);
}
