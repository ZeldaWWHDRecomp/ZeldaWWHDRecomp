/* Complete CC0 d_a_warpdm20.cpp normal_execute (024D5354).
 * MIT remove_cutscenes.asm replaces only its skipped-demo gate with >=8 shards.
 * Geometry, captured-emitter lifetime, animation and native warp test stay in order. */
#pragma once
#ifndef R_HYRULE_EXEC
#define R_HYRULE_EXEC(type,address) (*(volatile type*)(address))
#endif
WWHD_GAME_FUNC(0x0201ADE0,void,randomizer_hyrule_subtract,(u32 source,f32* output,u32 other));
/* Physical ABI: magnitude leaves F1 for native double sqrt. The native caller
 * retains that double in F31 across the shard/animation callbacks. */
WWHD_GAME_FUNC(0x028E8DD0,f64,randomizer_hyrule_square_magnitude,(const f32* vector));
WWHD_GAME_FUNC(0x028F4384,f64,randomizer_hyrule_sqrt,(f64 square));
WWHD_GAME_FUNC(0x024D5208,void,randomizer_hyrule_animate,(u32 actor));
WWHD_GAME_FUNC(0x024D5298,s32,randomizer_hyrule_check_warp,(u32 actor));
WWHD_REPLACE(0x024D5354,void,randomizer_hyrule_warp_execute,(u32 actor)) {
    const u32 emitter=R_HYRULE_EXEC(u32,actor+0x3CC);
    const u32 game=wwhd_dComIfG_getGameInfo();
    const u32 player=R_HYRULE_EXEC(u32,game+0x5B2C);
    union {u32 words[3];f32 values[3];} delta,horizontal;
    randomizer_hyrule_subtract(player+0x314,delta.values,actor+0x314);
    /* Preserve vector copy bits; y uses the native zero operand. */
    horizontal.words[0]=delta.words[0];
    horizontal.words[1]=WWHD_GAME_DATA(0x1004214C,u32);
    horizontal.words[2]=delta.words[2];
    const f64 square=randomizer_hyrule_square_magnitude(horizontal.values);
    const f64 distance=randomizer_hyrule_sqrt(square);
    if(wwhd_dSv_player_collect_c_getTriforceNum(randomizer_daily_save()+0xD4)<8)return;
    R_HYRULE_EXEC(u8,actor+0x3E8)=1;
    randomizer_hyrule_animate(actor);
    if(emitter) {
        const f64 threshold=(f64)WWHD_GAME_DATA(0x100421B4,f32);
        if(distance<threshold)R_HYRULE_EXEC(u32,emitter+0x254)=R_HYRULE_EXEC(u32,emitter+0x254)&~1u;
        else R_HYRULE_EXEC(u32,emitter+0x254)=R_HYRULE_EXEC(u32,emitter+0x254)|1u;
    }
    if(randomizer_hyrule_check_warp(actor))R_HYRULE_EXEC(s32,actor+0x3E4)=1;
}
