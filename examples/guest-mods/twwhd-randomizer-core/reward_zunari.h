/* Zunari reward consumers: public CC0 HD decompilation a874e1a9,
 * d_a_npc_rsh1_move.cpp/msg.cpp. Changes from MIT TWWHD Randomizer1.2.0
 * flexible_item_locations.asm, Copyright(c)2024 SuperDude88. */
#pragma once
#include "reward_zunari_state.h"
#ifndef RANDOMIZER_ZUNARI_CALL
#define RANDOMIZER_ZUNARI_CALL(target,self) ((u32(*)(u32,u32))(target))((self),0)
#endif
WWHD_GAME_FUNC(0x022D9C18,u16,randomizer_zunari_talk,(u32 actor));
WWHD_GAME_FUNC(0x025BC620,void,randomizer_zunari_camera_reset,(u32 camera));
WWHD_GAME_FUNC(0x022D8B20,void,randomizer_zunari_animation,(u32 actor,s8 animation));
WWHD_GAME_FUNC(0x022D68B8,s32,randomizer_zunari_shop_message,(u32 message));
WWHD_GAME_FUNC(0x025BD278,u32,randomizer_zunari_buy_message,(u32 items));
WWHD_GAME_FUNC(0x02542D88,s32,randomizer_zunari_staff,(u32 manager,u32 name,u32 actor,s32 index));
WWHD_GAME_FUNC(0x02543280,void,randomizer_zunari_cut_end,(u32 manager,s32 staff));
WWHD_GAME_FUNC(0x0254457C,s32,randomizer_zunari_end_check,(u32 manager,u32 name));
WWHD_GAME_FUNC(0x025D7DEC,u32,randomizer_zunari_present,(u32 pos,u32 item,u32 flags,u32 bit,u32 room,u32 angle,u32 scale));
WWHD_GAME_FUNC(0x0254DA50,s32,randomizer_zunari_received_query,(u8 item,s32 flag));
WWHD_GAME_FUNC(0x02520C0C,s32,randomizer_zunari_owned,(u32 item));
WWHD_GAME_FUNC(0x025B8B94,s32,randomizer_zunari_event_query,(u32 event,u32 flag));
WWHD_GAME_FUNC(0x022D6828,s32,randomizer_zunari_shop_count,(void));
static void randomizer_zunari_event_reset(void) {
    const u32 info=wwhd_dComIfG_getGameInfo();*(volatile u16*)(info+0x52B8)|=8;
}
static void randomizer_zunari_action(u32 actor,u32 action) {
    const u32 descriptor=actor+0x920;
    if(*(volatile s16*)descriptor==0 && *(volatile s16*)(descriptor+2)==-1 && *(volatile u32*)(descriptor+4)==action)return;
    if(*(volatile s16*)(descriptor+2)!=0) {
        *(volatile s8*)(actor+0xAC8)=-1;
        const u32 self=actor+(u32)(s32)*(volatile s16*)descriptor;
        const s16 index=*(volatile s16*)(descriptor+2);
        u32 target;
        if(index<0)target=*(volatile u32*)(descriptor+4);
        else {
            const u32 table=*(volatile u32*)(self+(u32)(s32)*(volatile s16*)(descriptor+6));
            target=*(volatile u32*)(table+(u32)(s32)index*8+4);
        }
        (void)RANDOMIZER_ZUNARI_CALL(target,self);
    }
    *(volatile s8*)(actor+0xAC8)=0;
    *(volatile u32*)(descriptor+4)=action;
    *(volatile s16*)descriptor=0;*(volatile s16*)(descriptor+2)=-1;
    (void)RANDOMIZER_ZUNARI_CALL(action,actor);
}
WWHD_REPLACE(0x022DA4C4,u8,randomizer_zunari_talk01,(u32 actor)) {
    const u16 result=randomizer_zunari_talk(actor);
    if(result==0x12) {
        const u32 link=*(volatile u32*)(wwhd_dComIfG_getGameInfo()+0x5B34);
        *(volatile s8*)(actor+0xAC4)=(s8)*(volatile u8*)(actor+0xAC5);
        randomizer_zunari_event_reset();randomizer_zunari_camera_reset(actor+0x928);
        *(volatile u32*)(link+0x3B8)&=~0x08000000u;
        *(volatile u8*)(actor+0x8E5)=0;randomizer_zunari_animation(actor,0);
        const u32 message=*(volatile u32*)(actor+0x8EC);
        if(message==0x2856 || message==0x288E || message==0x285F) {
            *(volatile s8*)(actor+0xAC3)=4;
            *(volatile u8*)(actor+0xACB)=message==0x2856?0x8C:message==0x288E?0x78:init_reward_zunari[0];
        } else if(message==0x2883 && *(volatile u8*)(actor+0x907)==1)
            randomizer_zunari_action(actor,0x022DAA78);
    } else if(result==8 && randomizer_zunari_shop_message(*(volatile u32*)(actor+0x8EC)))
        (void)randomizer_zunari_buy_message(*(volatile u32*)(actor+0x978));
    if(*(volatile u8*)(*(volatile u32*)(actor+0xABC)+0xB4)) {
        *(volatile u8*)(wwhd_dComIfG_getGameInfo()+0x5BBA)=0x17;
        *(volatile u8*)(wwhd_dComIfG_getGameInfo()+0x5BB9)=0x27;
    }
    return *(volatile f32*)(*(volatile u32*)(actor+0x3B4)+0xB0)<1.0f;
}
WWHD_REPLACE(0x022DA818,s32,randomizer_zunari_getdemo,(u32 actor,u32 argument)) {
    (void)argument;
    const u32 previous=randomizer_zunari_actor;randomizer_zunari_actor=0;
    const s32 staff=randomizer_zunari_staff(wwhd_dComIfG_getGameInfo()+0x52C4,0x10021860,0,0);
    if(*(volatile s8*)(actor+0xAC8)==0) {
        const u32 link=*(volatile u32*)(wwhd_dComIfG_getGameInfo()+0x5B34);
        *(volatile u32*)(link+0x3B8)&=~0x08000000u;
        *(volatile s8*)(actor+0xAC4)=(s8)*(volatile u8*)(actor+0xAC5);
        randomizer_zunari_camera_reset(actor+0x928);
        randomizer_zunari_actor=actor;
        const u32 pid=randomizer_zunari_present(actor+0x314,*(volatile u8*)(actor+0xACB),0,0xFFFFFFFFu,(u32)(s32)*(volatile s8*)(actor+0x326),0,0);
        randomizer_zunari_actor=0;
        if(pid!=0xFFFFFFFFu)*(volatile u32*)(wwhd_dComIfG_getGameInfo()+0x52A0)=pid;
        randomizer_zunari_cut_end(wwhd_dComIfG_getGameInfo()+0x52C4,staff);
        *(volatile s8*)(actor+0xAC8)=(s8)(u8)(*(volatile u8*)(actor+0xAC8)+1);
    } else if(*(volatile s8*)(actor+0xAC8)!=-1) {
        randomizer_zunari_cut_end(wwhd_dComIfG_getGameInfo()+0x52C4,staff);
        if(randomizer_zunari_end_check(wwhd_dComIfG_getGameInfo()+0x52C4,0x10021868)) {
            *(volatile s8*)(actor+0xAC3)=1;randomizer_zunari_event_reset();
            const u8 item=*(volatile u8*)(actor+0xACB);
            if(item==0x8C)*(volatile u32*)(actor+0x8F4)=0x2857;
            else if(item==0x78)*(volatile u32*)(actor+0x8F4)=0x288F;
            else if(item==init_reward_zunari[0])*(volatile u32*)(actor+0x8F4)=0x2860;
            *(volatile u8*)(actor+0xACB)=0xFF;randomizer_zunari_action(actor,0x022DA748);
        }
    }
    randomizer_zunari_actor=previous;return 1;
}
/* Complete public CC0 d_a_npc_rsh1_msg.cpp::getMsg, preserving the existing
 * first Magic Armor/source-6940 query owner. Upstream sail jump retains item78. */
static s32 randomizer_zunari_event(u32 flag) {
    return randomizer_zunari_event_query(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,flag);
}
static u32 randomizer_zunari_message_body(u32 actor) {
    u32 message=*(volatile u32*)(actor+0x8F4);
    if(message){*(volatile u32*)(actor+0x8F4)=0;return message;}
    if(!randomizer_zunari_received_query(0x2A,1) && randomizer_zunari_shop_count()>=3)return 0x285D;
    (void)randomizer_zunari_owned(0x78);
    if(!randomizer_zunari_owned(0x30))return 0x2890;
    if(*(volatile s32*)(actor+0x8FC)==-1) {
        if(randomizer_zunari_event(0x2D01) && !randomizer_zunari_event(0xE08) && randomizer_zunari_event(0x1108))return 0x2859;
        const s32 count=randomizer_zunari_shop_count();
        if(count>=8)return 0x283E;
        if(count>1)return 0x2840;
        if(randomizer_zunari_event(0x1108))return 0x2842;
        message=!randomizer_zunari_event(0x1110)?0x2845:0x284B;
    } else message=!randomizer_zunari_event(0x1108)?0x2865:0x2862;
    return message;
}
WWHD_REPLACE(0x022D94B8,u32,randomizer_zunari_message,(u32 actor)) {
    const u32 previous=randomizer_zunari_query;
    randomizer_zunari_query=*(volatile u32*)(actor+0x8F4)==0;
    const u32 message=randomizer_zunari_message_body(actor);
    randomizer_zunari_query=previous;return message;
}
