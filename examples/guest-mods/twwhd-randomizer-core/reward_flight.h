/* Flight reward control flow follows public CC0 d_a_npc_bmcon1.cpp (022065C4).
 * The first prize is private seed data; repeat prizes retain the native rupee. */
#pragma once
WWHD_REPLACE(0x022065C4,u8,randomizer_flight_prize,(u32 actor)) {
    const u16 status=wwhd_daNpcBmcon_c__talk2((void*)actor,0);
    if(status==0x12) {
        const u8 flags=*(volatile u8*)(actor+0x92C);
        if(flags&1) {
            *(volatile u8*)(actor+0x92C)=flags&~1u;
            *(volatile u32*)(actor+0x900)=init_reward_flight_first;
            *(volatile u8*)(actor+0x922)=6;
        } else if(flags&2) {
            *(volatile u8*)(actor+0x92C)=flags&~2u;
            *(volatile u32*)(actor+0x900)=5;
            *(volatile u8*)(actor+0x922)=6;
        }
    }
    return status==0x12;
}
