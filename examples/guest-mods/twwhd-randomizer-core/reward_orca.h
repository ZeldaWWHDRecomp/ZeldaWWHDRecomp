/* Reward control flow follows public CC0 d_a_npc_ji1_event.cpp (02258574).
 * Only the first 500-hit prize is shuffled; training and repeat rewards stay native. */
#pragma once
WWHD_REPLACE(0x02258574,void,randomizer_orca_prize,(u32 actor)) {
    u32 item;
    if(*(volatile u8*)(actor+0xFA4)) item=0x38;
    else if(*(volatile u8*)(actor+0xFA3)==1) item=0xAA;
    else {
        const s32 rank=wwhd_save_event_read(0xD003);
        if(rank==1) item=5;
        else if(rank==2) item=6;
        else if(*(volatile s32*)(actor+0xF98)>=WWHD_GAME_DATA(0x1046730E,s16) &&
                wwhd_save_event_test(0x0F10)==1) item=0x0F;
        else {
            item=init_reward_orca_500;
            wwhd_save_event_set(0x0F10,1);
        }
    }
    const u32 id=wwhd_fopAcM_createItemForPresentDemo(actor+0x314,item,0,
        0xFFFFFFFFu,(u32)*(volatile s8*)(actor+0x326),0,0);
    if(id!=0xFFFFFFFFu) *(volatile u32*)(wwhd_dComIfG_getGameInfo()+0x52A0)=id;
    *(volatile u8*)(actor+0xFA5)=(u8)item;
}
