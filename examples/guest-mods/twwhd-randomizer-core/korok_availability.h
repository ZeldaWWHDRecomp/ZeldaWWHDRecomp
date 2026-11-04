/* Tree/Korok pearl gates: TWWHD Randomizer 1.2.0 make_game_nonlinear.asm.
 * Copyright (c) 2024 SuperDude88. MIT: LICENSE-TWWHD-Randomizer.txt.
 * Native control flow: public CC0 d_a_npc_bj1.cpp and d_a_obj_ftree.cpp. */
#pragma once
static u32 randomizer_korok_first_symbol;
static u32 randomizer_mailbox_first_symbol;

/* Each targeted native method has one Farore query. Keep its other story
 * checks, action setup, messages and callbacks; restore nested query scopes. */
#define RANDOMIZER_KOROK_GATE(address,name) \
    WWHD_GAME_ORIGINAL(address,u32,randomizer_native_##name,(u32 actor)); \
    WWHD_REPLACE(address,u32,randomizer_##name,(u32 actor)) { \
        const u32 previous=randomizer_korok_first_symbol; \
        randomizer_korok_first_symbol=1; \
        const u32 result=randomizer_native_##name(actor); \
        randomizer_korok_first_symbol=previous; \
        return result; \
    }
RANDOMIZER_KOROK_GATE(0x021F3EF0,korok_bj4_init)
RANDOMIZER_KOROK_GATE(0x021F3F8C,korok_bj6_init)
RANDOMIZER_KOROK_GATE(0x021F4038,korok_bjx0_init)
RANDOMIZER_KOROK_GATE(0x021F40D4,korok_bjx1_init)
RANDOMIZER_KOROK_GATE(0x021F8AF8,korok_bj7_message)
#undef RANDOMIZER_KOROK_GATE

WWHD_GAME_ORIGINAL(0x023465A0,void,randomizer_native_tree_first_state,(u32 actor));
WWHD_REPLACE(0x023465A0,void,randomizer_tree_first_state,(u32 actor)) {
    const u32 previous=randomizer_korok_first_symbol;
    randomizer_korok_first_symbol=1;
    randomizer_native_tree_first_state(actor);
    randomizer_korok_first_symbol=previous;
}
/* The mailbox's first Din query only guards getReadableLetterNum. Consume
 * the override before that call so letter-specific callbacks see real items. */
WWHD_GAME_ORIGINAL(0x023A4A60,s32,randomizer_native_mailbox_execute,(u32 actor));
WWHD_REPLACE(0x023A4A60,s32,randomizer_mailbox_execute,(u32 actor)) {
    const u32 previous=randomizer_mailbox_first_symbol;
    randomizer_mailbox_first_symbol=1;
    const s32 result=randomizer_native_mailbox_execute(actor);
    randomizer_mailbox_first_symbol=previous;
    return result;
}
WWHD_GAME_ORIGINAL(0x025B7D90,s32,randomizer_native_korok_symbol,(u32 collect,u32 index));
WWHD_REPLACE(0x025B7D90,s32,randomizer_korok_symbol,(u32 collect,u32 index)) {
    if(randomizer_mailbox_first_symbol && index==1 &&
       collect==WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0xD4) {
        randomizer_mailbox_first_symbol=0;
        return 1;
    }
    if(randomizer_korok_first_symbol && index==2 &&
       collect==WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0xD4) {
        randomizer_korok_first_symbol=0;
        return 1;
    }
    return randomizer_native_korok_symbol(collect,index);
}
