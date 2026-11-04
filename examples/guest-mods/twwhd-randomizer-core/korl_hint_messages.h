/* MIT TWWHD Randomizer flexible_hint_locations.asm, pinned 1.2.0.
 * The new hint branch precedes even the native message-manager pointer load.
 * Other messages keep the complete native consumer and its callbacks. */
#pragma once
WWHD_GAME_ORIGINAL(0x02481038,s32,randomizer_native_ship_next_message,(u32 actor,u32 message));
WWHD_REPLACE(0x02481038,s32,randomizer_ship_next_hint_message,(u32 actor,u32 message)) {
    const s32 current=(s32)*(volatile u32*)(actor+0x648);
    const s32 last=(s32)init_last_korl_hint_message_number;
    if(current>=0xD73 && current<=last) {
        if(current==last)return 1;
        *(volatile u32*)(actor+0x648)=(u32)(current+1);
        return 0;
    }
    return randomizer_native_ship_next_message(actor,message);
}
