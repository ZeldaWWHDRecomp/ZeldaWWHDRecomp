/* Talk/reward flow follows public CC0 d_a_npc_photo_exec.cpp (022CFA00).
 * Preserve presentation gates, animation paths and flag priority; substitute two seed gifts. */
#pragma once
WWHD_GAME_FUNC(0x02544950,u32,randomizer_lenzo_present_end,(u32 manager));
WWHD_REPLACE(0x022CFA00,void,randomizer_lenzo_talk,(u32 actor)) {
    wwhd_daNpcPhoto_c__executeCommon((void*)actor);
    const u32 info=wwhd_dComIfG_getGameInfo();
    const u32 button=*(volatile u8*)(info+0x52B0);
    if(button>=1 && button<=4 && !randomizer_lenzo_present_end(info+0x52C4)) return;
    if(wwhd_daNpcPhoto_c__talk2((void*)actor,1)!=0x12) {
        wwhd_daNpcPhoto_c__setAnmFromMsgTag((void*)actor);
        return;
    }
    *(volatile u8*)(actor+0xB30)=0;
    wwhd_daNpcPhoto_c__executeSetMode((void*)actor,0);
    const u8 flags=*(volatile u8*)(actor+0xB3A);
    if(flags&1) {
        *(volatile u8*)(actor+0xB3A)=flags&~1u;
        *(volatile u8*)(actor+0xB39)=init_reward_lenzo[0];
        wwhd_save_event_set(0x6920,1);
        *(volatile u8*)(actor+0xB32)=4;
    } else if(flags&2) {
        *(volatile u8*)(actor+0xB3A)=flags&~2u;
        *(volatile u8*)(actor+0xB39)=init_reward_lenzo[1];
        *(volatile u8*)(actor+0xB32)=4;
    } else if(flags&4) {
        *(volatile u8*)(actor+0xB3A)=flags&~4u;
        *(volatile u8*)(actor+0xB32)=3;
        wwhd_daNpcPhoto_c__setAnmTbl((void*)actor,(void*)&WWHD_GAME_DATA(0x101C4F58,u8));
        *(volatile u8*)(actor+0xB3D)|=4;
    } else *(volatile u8*)(actor+0xB3A)=flags|0x40;
}
