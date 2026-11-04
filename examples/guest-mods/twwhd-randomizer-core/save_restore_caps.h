/* Public CC0 ZeldaWWHDDecomp/wwhd d_save_5.cpp and d_gameover.cpp control flow.
 * TWWHD Randomizer 1.2.0 misc_rando_features.asm, MIT, SuperDude88 (2024). */
#pragma once
/* The native copy is the C runtime import, not a game function. Use the
 * SDK memcpy service, which copies the same bytes and has no game callbacks. */
static void randomizer_card_copy(u32 dst,u32 src,u32 size) {
    memcpy((void*)dst,(const void*)src,size);
}
WWHD_GAME_FUNC(0x025E1C74,void,randomizer_card_audio,(u32 mode));
WWHD_GAME_ORIGINAL(0x025499A0,u32,randomizer_gameover_original,(u32 actor));

static u16 randomizer_restore_health(u16 maximum) {
    const u16 whole=(u16)(maximum&0xFFFCu);
    return whole<12?whole:12;
}
/* Offsets include the native allocation's 0x20-byte prefix. Packed blocks
 * deliberately omit the native player's alignment gaps. */
static const u16 randomizer_card_head[][2]={
    {0x20,0x18},{0x38,0x18},{0x50,0x0C},{0x5C,0x15},
    {0x71,0x15},{0x86,8},{0x8E,8},{0x96,0x18},
    {0xB0,0x0C},{0xBC,0x18},{0xD4,0x0D},{0xE4,0x84},
    {0x168,0x5C},{0x1C4,5}
};
static u32 randomizer_card_tail(u32 buf,int loading) {
    u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    if(loading)randomizer_card_copy(save+0x1CC,buf,0x10);
    else randomizer_card_copy(buf,save+0x1CC,0x10);
    buf+=0x10;
    for(u32 i=0;i<4;i++) {
        save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
        if(loading)randomizer_card_copy(save+0x1DC+i*0x70,buf,0x70);
        else randomizer_card_copy(buf,save+0x1DC+i*0x70,0x70);
        buf+=0x70;
    }
    static const u16 tail[][2]={{0x3A0,0x240},{0x5E0,0x64},{0x644,0x100},{0x744,0x50}};
    for(u32 i=0;i<4;i++) {
        save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
        if(loading)randomizer_card_copy(save+tail[i][0],buf,tail[i][1]);
        else randomizer_card_copy(buf,save+tail[i][0],tail[i][1]);
        buf+=tail[i][1];
    }
    return buf;
}
WWHD_REPLACE(0x025BA7B0,s32,randomizer_card_to_memory,(u32 info,u32 card,s32 slot)) {
    (void)info;
    const u32 start=card+(u32)slot*0xA94u;
    u32 buf=start;
    for(u32 i=0;i<14;i++) {
        const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
        randomizer_card_copy(save+randomizer_card_head[i][0],buf,randomizer_card_head[i][1]);
        buf+=randomizer_card_head[i][1];
    }
    *(volatile u8*)(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x1C5)=2;
    randomizer_card_audio(2);
    buf=randomizer_card_tail(buf,1);
    /* Upstream's refill is after every copy and the sound callback. It writes
     * the loaded current MP directly, never the pending MP accumulators. */
    const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    *(volatile u8*)(save+0x34)=*(volatile u8*)(save+0x33);
    return (buf-start)>0xA8Cu?-1:0;
}
WWHD_REPLACE(0x025BA9FC,s32,randomizer_memory_to_card,(u32 info,u32 card,s32 slot)) {
    (void)info;
    const u32 start=card+(u32)slot*0xA94u;
    u32 buf=start;
    for(u32 i=0;i<14;i++) {
        const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
        randomizer_card_copy(buf,save+randomizer_card_head[i][0],randomizer_card_head[i][1]);
        if(i==0 && *(volatile u16*)(buf+2)<12)
            *(volatile u16*)(buf+2)=randomizer_restore_health(*(volatile u16*)buf);
        buf+=randomizer_card_head[i][1];
    }
    buf=randomizer_card_tail(buf,0);
    return (buf-start)>0xA8Cu?-1:0;
}
/* OPEN has no callbacks. Delegate every other state, including the captured
 * pair of virtual state-ID callbacks, to the native implementation unchanged. */
WWHD_REPLACE(0x025499A0,u32,randomizer_gameover_execute,(u32 actor)) {
    if(*(volatile u8*)(actor+0x110)!=3)return randomizer_gameover_original(actor);
    const u32 graphics=WWHD_GAME_DATA(0x101F8344,u32);
    *(volatile u8*)(graphics+0x248)=1;
    *(volatile u8*)(actor+0x110)=4;
    const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    *(volatile u16*)(save+0x22)=randomizer_restore_health(*(volatile u16*)(save+0x20));
    return 1;
}
