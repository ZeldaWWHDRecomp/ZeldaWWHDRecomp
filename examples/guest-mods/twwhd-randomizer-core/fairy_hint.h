/* Public CC0 d_a_bigelf.cpp/getMsg; MIT flexible_hint_locations.asm02095BE8.
 * Preserve the bow-byte read, but let every bow tier reach the hint message. */
#pragma once
WWHD_REPLACE(0x02095BC8,u32,randomizer_fairy_hint_message,(u32 actor)) {
    u32 message=*(volatile unsigned int*)(actor+0x584);
    if(message==12014) {
        const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
        (void)*(volatile u8*)(save+0x68);
        message=12015;
        *(volatile unsigned int*)(actor+0x584)=(unsigned int)message;
    }
    return message;
}
