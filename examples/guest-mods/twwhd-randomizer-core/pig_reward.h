/* Public CC0 d_com_static.cpp dig_main; MIT flexible_item_locations.asm.
 * Async createItem returns an ID: never dereference it as an actor pointer. */
#pragma once
WWHD_GAME_FUNC(0x025BA0C0,s32,randomizer_pig_switch,(u32 save,u32 bit,s32 room));
WWHD_GAME_FUNC(0x025B9E38,void,randomizer_pig_set_switch,(u32 save,u32 bit,s32 room));
WWHD_GAME_FUNC(0x025D4604,u32,randomizer_pig_is_actor,(u32 actor));
WWHD_GAME_FUNC(0x025D57E0,void,randomizer_pig_delete,(u32 actor));
WWHD_GAME_FUNC(0x020198D8,f64,randomizer_pig_random,(f32 range));
WWHD_GAME_FUNC(0x02019918,f64,randomizer_pig_random_signed,(f32 range));
static s32 randomizer_pig_integer(f64 value) {
    if(value!=value)return (s32)0x80000000u;
    if(value>=2147483648.0)return 0x7FFFFFFF;
    if(value<=-2147483648.0)return (s32)0x80000000u;
    return (s32)value;
}
WWHD_REPLACE(0x02526330,void,randomizer_pig_dig,(u32 actor)) {
    const s32 room=(s8)*(volatile u8*)(actor+0x2FE);
    const u32 save=randomizer_daily_save(),bit=*(volatile unsigned int*)(actor+0x3C0);
    if(randomizer_pig_switch(save+0x20,bit,room))return;
    if(!*(volatile u8*)(actor+0x3B5) || *(volatile u8*)(actor+0x3B4))return;
    unsigned int scale[3];s16 angle[3];
    scale[0]=WWHD_GAME_DATA(0x101FFBA8,unsigned int);scale[1]=WWHD_GAME_DATA(0x101FFBAC,unsigned int);
    angle[0]=WWHD_GAME_DATA(0x101FFB14,s16);angle[1]=WWHD_GAME_DATA(0x101FFB16,s16);
    scale[2]=WWHD_GAME_DATA(0x101FFBB0,unsigned int);angle[2]=WWHD_GAME_DATA(0x101FFB18,s16);
    if(!randomizer_pig_is_actor(*(volatile u32*)(actor+0x3C4)))return;
    u32 pig=*(volatile u32*)(actor+0x3C4);
    if(!pig || *(volatile s16*)(pig+8)!=220)return;
    u32 done=0;
    if(*(volatile u8*)(actor+0x3BC)!=255) {
        angle[1]=(s16)randomizer_pig_integer(randomizer_pig_random(WWHD_GAME_DATA(0x1004BCC4,f32)));
        const f32 range=WWHD_GAME_DATA(0x1004BCC8,f32);
        (void)randomizer_pig_random(range);const f64 vertical=randomizer_pig_random_signed(range);
        const u32 item=*(volatile u8*)(actor+0x3BC);
        pig=*(volatile u32*)(actor+0x3C4);
        const u32 item_bit=*(volatile unsigned int*)(actor+0x3B8);
        const f32 speed=WWHD_GAME_DATA(0x1004BCCC,f32);
        const s32 current_room=(s8)*(volatile u8*)(actor+0x326);
        volatile f32 unused_vertical=(f32)(vertical+(f64)speed);
        (void)unused_vertical;(void)WWHD_GAME_DATA(0x1004BCD0,f32);
        if(item!=255)(void)wwhd_fopAcM_createItem(pig+0x314,item,item_bit,(u32)current_room,3,(u32)angle,5,(u32)scale);
        const u32 sw=*(volatile unsigned int*)(actor+0x3C0);
        if(sw!=255)randomizer_pig_set_switch(randomizer_daily_save()+0x20,sw,(s8)*(volatile u8*)(actor+0x2FE));
        else randomizer_pig_delete(actor);
        done=1;
    }
    const u32 enemy=*(volatile u8*)(actor+0x3BD);
    if(enemy!=255) {
        pig=*(volatile u32*)(actor+0x3C4);*(volatile u16*)(pig+0x2FC)=(u16)enemy;
        const u32 save_now=randomizer_daily_save();const s32 home=(s8)*(volatile u8*)(actor+0x2FE);
        randomizer_pig_set_switch(save_now+0x20,*(volatile unsigned int*)(actor+0x3C0),home);
        randomizer_pig_delete(actor);done=1;
    }
    if(done)*(volatile u8*)(actor+0x3B4)=1;
    *(volatile u8*)(actor+0x3B5)=0;
}
