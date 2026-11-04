/* Hurricane ownership query from TWWHD Randomizer flexible_item_locations.asm.
 * Copyright (c) 2024 SuperDude88. MIT: see LICENSE-TWWHD-Randomizer.txt.
 * Source flow/layout: public CC0 d_a_player_main_08.cpp/.h (02442190).
 * Replace this initializer only; all other story-flag consumers stay native. */
#pragma once
WWHD_REPLACE(0x02442190,u32,randomizer_hurricane_spin_init,(u32 actor)) {
    wwhd_daPy_lk_c__commonProcInit((void*)actor,0x59);
    wwhd_daPy_lk_c__setSingleMoveAnime((void*)actor,0x19,0.0f,0.0f,-1,3.0f);
    /* Native inline Boko upper animation cleanup occurs before the query. */
    if(*(volatile u16*)(actor+0x69B0)==0x101) {
        const u32 animation=*(volatile u32*)(actor+0x581C);
        if(animation) {
            const f32 frame=(f32)*(volatile s16*)(actor+0x58C2)-0.001f;
            *(volatile f32*)(actor+0x58BC)=frame;
            *(volatile f32*)animation=frame;
        }
    }
    *(volatile u8*)(actor+0x68D4)=0;
    *(volatile f32*)(actor+0x69F0)=1.0f;
    const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    const u32 owned=wwhd_save_event_test(0x6901)==1 ||
        wwhd_dSv_event_c_isEventBit(save+0x1178,0x0402)!=0;
    if(owned && *(volatile u16*)(actor+0x69B0)==0x103 &&
       *(volatile u8*)(save+0x34)>=2 &&
       *(volatile u8*)(wwhd_dComIfG_getGameInfo()+0x5CEA)!=2 &&
       *(volatile u8*)(wwhd_dComIfG_getGameInfo()+0x5CEA)!=6) {
        const u32 scale=*(volatile u8*)(save+0x2E)==0x38 ||
            *(volatile u8*)(wwhd_dComIfG_getGameInfo()+0x5CEA)==2 ?
            0:(u32)&WWHD_GAME_DATA(0x101CEED8,u8);
        /* Re-read the model for each native effect; the first callback may alter it. */
        for(u32 i=0;i<2;i++) {
            const u32 model=*(volatile u32*)(actor+0x448);
            const u32 joint=*(volatile u32*)(model+0x2C);
            *(volatile u16*)(joint+4)|=0x10;
            const u32 matrix=*(volatile u32*)(joint+0x10)+0x180;
            wwhd_daPy_makeEmitter((void*)(actor+(i?0x6704:0x66F8)),
                i?0x2A:0x29,(void*)matrix,(void*)(actor+0x314),(void*)scale);
        }
        *(volatile s16*)(actor+0x6916)=47;
    } else {
        *(volatile s16*)(actor+0x6916)=-1;
    }
    const u32 play=wwhd_dComIfG_getGameInfo();
    *(volatile u32*)(play+0x5CD8)|=0x40000000u;
    return 1;
}
