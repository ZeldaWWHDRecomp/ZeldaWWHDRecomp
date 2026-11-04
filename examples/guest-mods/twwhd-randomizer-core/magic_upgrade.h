/* Native fairy control flow: public CC0 wwhd_src/d/actor/d_a_bigelf.cpp.
 * Reward changes: TWWHD Randomizer 1.2.0 flexible_item_locations.asm,
 * Copyright (c) 2024 SuperDude88. MIT: LICENSE-TWWHD-Randomizer.txt. */
#pragma once
WWHD_GAME_FUNC(0x02096ECC,void,randomizer_fairy_light_end,(u32 actor));
WWHD_GAME_FUNC(0x02543280,void,randomizer_fairy_cut_end,(u32 manager,s32 staff));
WWHD_GAME_FUNC(0x025A847C,u32,randomizer_fairy_particle,
    (u32 manager,u32 group,u32 effect,u32 position,u32 angle,u32 scale,
     u32 alpha,u32 callback,s32 room,u32 primary,u32 environment,u32 extra));

/* Upstream replaces the native load/add with a fixed 32, then stores that
 * value to both pending current and maximum MP. It is not an additive grant. */
WWHD_REPLACE(0x02550128,void,randomizer_magic_upgrade,(void)) {
    const u32 info=wwhd_dComIfG_getGameInfo();
    *(volatile u16*)(info+0x5B60)=32;
    *(volatile u16*)(info+0x5B64)=32;
}

/* Keep the fairy's effects and cut completion, but let the awarded item alone
 * decide whether magic increases. The vanilla type-6 tail added another 32. */
WWHD_REPLACE(0x02096EEC,u32,randomizer_fairy_delete,(u32 actor)) {
    volatile s16* counter=(volatile s16*)(actor+0x604);
    const s16 count=(s16)(u16)((s32)*counter+1);
    *counter=count;
    if(count<27)return 1;
    if(count==27) {
        const u32 info=wwhd_dComIfG_getGameInfo();
        const u32 player=*(volatile u32*)(info+0x5B34);
        const u32 color=(u32)(s32)*(volatile s8*)(actor+0x63D);
        const u32 effects=(u32)&WWHD_GAME_DATA(0x10009120,u8);
        const u16 effect=*(volatile u16*)(effects+color*2);
        randomizer_fairy_particle(*(volatile u32*)(info+0x5AB0),0,effect,
            player+0x314,0,0,255,0,-1,0,0,0);
        *(volatile f32*)(actor+0x5E8)=1.0f;
        if(*counter<27)return 1; /* Preserve changes made by native callbacks. */
    }
    const u32 emitter=*(volatile u32*)(actor+0x628);
    if(emitter) {
        const u8 alpha=*(volatile u8*)(actor+0x620);
        if(alpha>10) {
            *(volatile u8*)(actor+0x620)=(u8)(alpha-10);
            *(volatile u8*)(emitter+0x247)=(u8)(alpha-10);
            return 1;
        }
        *(volatile u8*)(actor+0x620)=0;
        *(volatile u8*)(emitter+0x247)=0;
        *(volatile s32*)(emitter+0x5C)=-1;
        *(volatile u32*)(emitter+0x254)|=1;
        *(volatile u32*)(actor+0x628)=0;
        *(volatile f32*)(actor+0x638)=0.0f;
        return 1;
    }
    if(*(volatile f32*)(actor+0x5A4)>0.0f)return 1;
    randomizer_fairy_light_end(actor);
    randomizer_fairy_cut_end(wwhd_dComIfG_getGameInfo()+0x52C4,
        *(volatile s32*)(actor+0x608));
    const u16 flags=*(volatile u16*)(actor+0x57C);
    if(flags&1)*(volatile u16*)(actor+0x57C)=flags&(u16)~1u;
    return 1;
}
