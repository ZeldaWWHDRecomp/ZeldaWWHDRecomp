/* Reward assignment sites verified in public CC0 d_a_npc_ho.cpp (0223FAB8).
 * Keep native dialogue, pendant accounting and event flags; replace only seed gifts. */
#pragma once
WWHD_GAME_ORIGINAL(0x0223FAB8,u32,randomizer_native_marie_message,(u32 actor,u32 message));
WWHD_REPLACE(0x0223FAB8,u32,randomizer_marie_message,(u32 actor,u32 message)) {
    const u32 incoming=*(volatile u32*)message;
    const u32 status=randomizer_native_marie_message(actor,message);
    switch(incoming) {
    case 0x271E: *(volatile u8*)(actor+0x806)=init_reward_marie[0];break;
    case 0x275C: *(volatile u8*)(actor+0x806)=init_reward_marie[1];break;
    case 0x2743: *(volatile u8*)(actor+0x806)=init_reward_marie[2];break;
    case 0x2751: *(volatile u8*)(actor+0x806)=init_reward_marie[3];break;
    default: break;
    }
    return status;
}
