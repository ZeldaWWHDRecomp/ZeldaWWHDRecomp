/* Doc Bandam: public CC0 a874e1a9 d_a_npc_ds1_event.cpp/talk.cpp.
 * First-potion argument and bottle-gate changes: MIT TWWHD Randomizer1.2.0
 * flexible_item_locations.asm, Copyright(c)2024 SuperDude88. */
#pragma once
WWHD_GAME_FUNC(0x02542D88,s32,randomizer_doc_staff,(u32 manager,u32 name,u32 actor,s32 index));
WWHD_GAME_FUNC(0x025D7DEC,u32,randomizer_doc_present,(u32 position,s32 item,u32 flags,s32 bit,s32 room,u32 angle,u32 scale));
WWHD_GAME_FUNC(0x02543280,void,randomizer_doc_cut_end,(u32 manager,s32 staff));
WWHD_GAME_FUNC(0x025DB58C,void,randomizer_doc_demo_message,(void));
WWHD_GAME_FUNC(0x025440C8,s32,randomizer_doc_end_check,(u32 manager,s16 event));
WWHD_GAME_FUNC(0x025BCE04,void,randomizer_doc_show_items,(u32 items));
WWHD_GAME_FUNC(0x0222D37C,s32,randomizer_doc_create_drug,(u32 code));
WWHD_GAME_FUNC(0x025B5C54,s32,randomizer_doc_empty_bottle,(u32 items));
WWHD_GAME_ORIGINAL(0x0222FECC,u32,randomizer_native_doc_next_message,(u32 actor,u32 message,u32 second));
#ifndef RANDOMIZER_DOC_CALL
#define RANDOMIZER_DOC_CALL(target,self) ((u32(*)(u32,u32))(target))((self),0)
#endif
static void randomizer_doc_reset_event(void){*(volatile u16*)(wwhd_dComIfG_getGameInfo()+0x52B8)|=8;}
static void randomizer_doc_wait_action(u32 actor) {
    const u32 descriptor=actor+0x9AC,target=0x022311F4;
    const s16 index=*(volatile s16*)(descriptor+2);
    if(index==-1 && *(volatile s16*)descriptor==0 && *(volatile u32*)(descriptor+4)==target)return;
    if(index!=0) {
        *(volatile s8*)(actor+0xA60)=-1;
        const u32 self=actor+(u32)(s32)*(volatile s16*)descriptor;
        u32 old;
        if(index<0)old=*(volatile u32*)(descriptor+4);
        else {const u32 table=*(volatile u32*)(self+(u32)(s32)*(volatile s16*)(descriptor+6));old=*(volatile u32*)(table+(u32)(s32)index*8+4);}
        (void)RANDOMIZER_DOC_CALL(old,self);
    }
    *(volatile s8*)(actor+0xA60)=0;
    *(volatile s16*)descriptor=0;*(volatile s16*)(descriptor+2)=-1;*(volatile u32*)(descriptor+4)=target;
    (void)RANDOMIZER_DOC_CALL(target,actor);
}
WWHD_REPLACE(0x022312F8,u32,randomizer_doc_getdemo,(u32 actor,u32 argument)) {
    (void)argument;
    const s32 staff=randomizer_doc_staff(wwhd_dComIfG_getGameInfo()+0x52C4,0x10019BD8,0,0);
    const s8 status=*(volatile s8*)(actor+0xA60);
    if(status==0) {
        s32 item=*(volatile u8*)(actor+0x9A8);
        if(item==0xFF){const u8 code=*(volatile u8*)(actor+0xA63);item=code==0x49?0x51:code==0x4A?0x52:0x53;}
        const u32 player=*(volatile u32*)(wwhd_dComIfG_getGameInfo()+0x5B2C);
        *(volatile u32*)(player+0x3B8)&=~0x08000000u;
        *(volatile u32*)(actor+0x9B8)=0;*(volatile s16*)(actor+0x9B6)=0;*(volatile s16*)(actor+0x9B4)=0;
        *(volatile u8*)(actor+0xA5C)=*(volatile u8*)(actor+0xA5D);
        /* Read the live message at the actual presentation seam; repeat gifts
         * and purchases keep their native item, regardless of unlocked flags. */
        if(*(volatile u32*)(actor+0x998)==7627) {
            if(item==0x52)item=init_reward_doc_bandam[0];
            else if(item==0x53)item=init_reward_doc_bandam[1];
        }
        const u32 pid=randomizer_doc_present(actor+0x314,item,0,-1,(s32)*(volatile s8*)(actor+0x326),0,0);
        if(pid!=0xFFFFFFFFu)*(volatile u32*)(wwhd_dComIfG_getGameInfo()+0x52A0)=pid;
        randomizer_doc_cut_end(wwhd_dComIfG_getGameInfo()+0x52C4,staff);
        *(volatile s8*)(actor+0xA60)=(s8)(u8)(*(volatile u8*)(actor+0xA60)+1);return 1;
    }
    if(status!=-1) {
        randomizer_doc_demo_message();randomizer_doc_cut_end(wwhd_dComIfG_getGameInfo()+0x52C4,staff);
        const s16 event=*(volatile s16*)(actor+0xA56);
        if(randomizer_doc_end_check(wwhd_dComIfG_getGameInfo()+0x52C4,event)) {
            *(volatile s8*)(actor+0xA4F)=1;
            if(*(volatile u8*)(actor+0x9A8)!=0xFF) {
                *(volatile u32*)(actor+0x9A4)=0x1DDA;randomizer_doc_show_items(actor+0xA04);
                *(volatile u8*)(actor+0x9A8)=0xFF;*(volatile s16*)(actor+0xA04)=-1;randomizer_doc_reset_event();
            } else {
                const s32 made=randomizer_doc_create_drug(*(volatile u8*)(actor+0xA63));
                *(volatile u32*)(actor+0x9A4)=made?0x1DC6:0x1DCD;randomizer_doc_reset_event();
            }
            const u8 code=*(volatile u8*)(actor+0xA63);
            if(code==0x4A)wwhd_dSv_event_c_onEventBit(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,0x0D04);
            else if(code==0x4B)wwhd_dSv_event_c_onEventBit(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,0x0D02);
            randomizer_doc_wait_action(actor);
        }
    }
    return 1;
}
WWHD_REPLACE(0x0222FECC,u32,randomizer_doc_next_message,(u32 actor,u32 message,u32 second)) {
    if(*(volatile u32*)message!=0x1DCB)return randomizer_native_doc_next_message(actor,message,second);
    (void)randomizer_doc_empty_bottle(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x5C);
    return 0x10;
}
