/* Beedle purchase branches from public CC0 d_a_npc_bs1_msg.cpp.
 * Seed sold-out checks and progressive animation selection follow TWWHD Randomizer
 * 1.2.0 (MIT; see LICENSE-TWWHD-Randomizer.txt). Other dialogue stays native. */
#pragma once
#include "beedle_stock_rules.h"
WWHD_GAME_ORIGINAL(0x02214858,u16,randomizer_native_beedle_message,(u32 actor,u32 message,u32 second));
WWHD_GAME_FUNC(0x025BBA1C,s32,randomizer_beedle_error,(u32 items,s32 arg,s32 price));
WWHD_GAME_FUNC(0x025BCECC,void,randomizer_beedle_sold,(u32 items,s16 index));
WWHD_GAME_FUNC(0x025BD260,u32,randomizer_beedle_show_message,(u32 items));
WWHD_GAME_FUNC(0x025BD22C,u8,randomizer_beedle_selected,(u32 items));
WWHD_GAME_FUNC(0x025BCD90,void,randomizer_beedle_hide,(u32 items));
WWHD_GAME_FUNC(0x025E1A40,void,randomizer_beedle_sound,(u32 sound,u32 pos,u32 arg,s32 reverb));
WWHD_GAME_FUNC(0x0254DA50,s32,randomizer_beedle_owned,(u8 item,s32 flag));
static void randomizer_beedle_mark_sold(u32 actor) {
    const s16 index=*(volatile s16*)(actor+0x940);
    randomizer_beedle_sold(actor+0x940,index);
    *(volatile u8*)(actor+0x8E4+index)=1;
}
WWHD_REPLACE(0x02214858,u16,randomizer_beedle_message,(u32 actor,u32 message,u32 second)) {
    const u32 incoming=*(volatile u32*)message;
    u32 rock=0;
    switch(incoming) {
    case 0xF47:case 0xF68:case 0xF6A:case 0xF6C:case 0xF6E:break;
    case 0x2F4D:case 0x2F4E:case 0x2F4F:case 0x2F73:case 0x2F75:case 0x2F77:rock=1;break;
    default:return randomizer_native_beedle_message(actor,message,second);
    }
    const u32 info=wwhd_dComIfG_getGameInfo(),items=actor+0x940;
    const u32 manager=WWHD_GAME_DATA(0x101F4B5C,u32);
    if(*(volatile u8*)(info+0x5BD3) || *(volatile u32*)(manager+0x948)) {
        *(volatile u32*)message=randomizer_beedle_show_message(items);return 0xF;
    }
    const s16 price=*(volatile s16*)(info+0x5BA4);
    const s32 status=randomizer_beedle_error(items,0,price);
    u32 error=0;
    if(!rock && (status&1))error=0xF49;
    else if(!rock && (status&2))error=0xF48;
    else if(status&0x20)error=rock?0x2F50:0xF4A;
    else if(status&4)error=rock?0x2F51:0xF4B;
    else if(status&0x10)error=rock?0x2F52:0xF4D;
    if(error){*(volatile u32*)message=error;return 0xF;}
    randomizer_beedle_sound(0x87F,actor+0x37C,0,
        (s32)wwhd_dComIfGp_getReverb((u32)(s32)*(volatile s8*)(actor+0x326)));
    randomizer_beedle_hide(items);
    *(volatile s32*)(info+0x5B48)-=price;
    if(!rock) {
        if(randomizer_beedle_bait_slot(actor)) {
            randomizer_beedle_sold(items,*(volatile s16*)items);
            wwhd_save_event_set(0x6902,1);
            *(volatile u8*)(actor+0x8E4+*(volatile s16*)items)=1;
        }
    } else {
        const u8 raw=randomizer_beedle_selected(items);
        const u16 flags[3]={0x2020,0x2010,0x2008};
        /* The compiled native switch tests the heart-piece slot first. Keep
         * that precedence when a seed puts identical items in multiple slots. */
        const u8 order[3]={1,0,2};
        for(u32 n=0;n<3;n++)if(raw==init_reward_beedle[order[n]]) {
            randomizer_beedle_mark_sold(actor);wwhd_save_event_set(flags[order[n]],1);break;
        }
    }
    /* Only the ownership/animation test uses the concrete progressive tier. */
    if(!randomizer_beedle_owned((u8)randomizer_display_item(randomizer_beedle_selected(items)),0)) {
        *(volatile s8*)(actor+0x996)=3;return 0x10;
    }
    wwhd_execItemGet(randomizer_beedle_selected(items));
    if(rock || *(volatile s8*)(actor+0x99E)!=0)*(volatile u32*)message=0x2F53;
    else *(volatile u32*)message=wwhd_dSv_event_c_getEventReg(
        (u32)wwhd_save_flags_detail_event_object(),0x86FF)?0xF4C:0xF4E;
    return 0xF;
}

/* Randomized one-time slots may hold an item that the player already owns.
 * Their sold-out event flags, rather than generic item ownership, govern stock. */
WWHD_HOOK(0x028EA120,randomizer_beedle_purchase_requirements,(void)) {
    volatile u8* table=(volatile u8*)&WWHD_GAME_DATA(0x101EAEA4,u8);
    const u8 slots[4]={0,0xB,0xC,0xD};
    for(u32 n=0;n<4;n++)table[slots[n]*0x10+0xC]&=(u8)~2u;
}
