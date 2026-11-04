/* CC0 d_a_npc_bms1_talk.cpp shop_talk; MIT price/pearl patch0220B5CC.
 * Constant true removes the head-swing path; other message behavior stays. */
#pragma once
WWHD_GAME_FUNC(0x025BB698,s32,randomizer_bomb_trigger,(u32 stick,u32 items,u32 message,u32 flag,u32 argument));
WWHD_GAME_FUNC(0x025F795C,u32,randomizer_bomb_message_status,(u32 manager));
WWHD_GAME_FUNC(0x025F74D0,void,randomizer_bomb_set_status,(u32 manager,u32 status));
WWHD_GAME_FUNC(0x0220AFB4,u32,randomizer_bomb_next_message,(u32 actor,u32 current,u32 next));
WWHD_GAME_FUNC(0x025F7DB0,void,randomizer_bomb_message_set,(u32 manager,u32 message,u32 position));
WWHD_GAME_FUNC(0x025DB58C,void,randomizer_bomb_demo_flag,(void));
WWHD_REPLACE(0x0220B554,u32,randomizer_bombshop_talk,(u32 actor)) {
    const u32 cursor=*(volatile u32*)(actor+0x9FC);
    const u32 manager=WWHD_GAME_DATA(0x101F4B5C,u32);
    *(volatile u8*)(cursor+0xB4)=1;
    if(randomizer_bomb_trigger(actor+0x8F0,actor+0x9B8,actor+0x94C,0,0)) {
        const s16 selected=*(volatile s16*)(actor+0x9B8);(void)selected;
        *(volatile u8*)(actor+0x918)=1;*(volatile unsigned int*)(actor+0x950)=0;
    }
    const u32 status=randomizer_bomb_message_status(manager)&0xFFFF;
    if(status==14 || status==15) {
        if(*(volatile u8*)(actor+0x918) || *(volatile u8*)(manager+0x922))
            *(volatile u8*)(actor+0x918)=0;
        else {
            const unsigned int message=*(volatile unsigned int*)(actor+0x94C);
            *(volatile unsigned int*)(actor+0x950)=message;
            const u32 next=randomizer_bomb_next_message(actor,actor+0x950,actor+0x94C);
            randomizer_bomb_set_status(manager,next);
            /* Native recheck compares its full return, not the captured u16. */
            if(randomizer_bomb_message_status(manager)==15)
                randomizer_bomb_message_set(manager,*(volatile unsigned int*)(actor+0x950),0);
        }
    } else if(status==18) {
        randomizer_bomb_set_status(manager,19);*(volatile u16*)(actor+0x9B8)=0xFFFF;
    } else if(status==1)randomizer_bomb_demo_flag();
    return status;
}
