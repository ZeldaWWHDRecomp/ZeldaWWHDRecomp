/* Message flow follows public CC0 d_a_bigelf.cpp (02095A90).
 * flexible_item_locations.asm selects by fairy type, not current upgrade capacity. */
#pragma once
WWHD_REPLACE(0x02095A90,u32,randomizer_great_fairy_message,(u32 actor,u32 message)) {
    volatile u32* id=(volatile u32*)message;
    if(*id==12008) {
        *id=12009;
        const u8 type=*(volatile u8*)(actor+0xB3);
        const u8 item=type<6?init_reward_great_fairy[type]:4;
        *(volatile u8*)(actor+0x582)=item;
        *(volatile u8*)(wwhd_dComIfG_getGameInfo()+0x52A4)=item;
        return 15;
    }
    if(*id==12011 || *id==12012 || *id==12015 || *id==12016) {
        ++*id;return 15;
    }
    if(*id==12017) {*id=12014;return 15;}
    return 16;
}
