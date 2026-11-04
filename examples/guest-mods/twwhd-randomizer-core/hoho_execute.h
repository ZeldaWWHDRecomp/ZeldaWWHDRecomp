/* CC0 d_a_npc_ah.cpp _execute; pinned HD 8-byte member-call descriptors.
 * Omit only the three switch calls and their impossible delete path. */
#pragma once
WWHD_GAME_FUNC(0x021E5BE0,void,randomizer_hoho_attention,(u32 actor));
WWHD_GAME_FUNC(0x021E5F38,void,randomizer_hoho_order_check,(u32 actor));
WWHD_GAME_FUNC(0x021E6464,void,randomizer_hoho_event,(u32 actor));
WWHD_GAME_FUNC(0x021E64E0,void,randomizer_hoho_order,(u32 actor));
WWHD_GAME_FUNC(0x021E6510,void,randomizer_hoho_texture,(u32 actor));
WWHD_GAME_FUNC(0x021E65B0,void,randomizer_hoho_animation,(u32 actor));
WWHD_GAME_FUNC(0x025D6870,void,randomizer_hoho_move,(u32 actor,u32 vector));
WWHD_GAME_FUNC(0x024F08A8,void,randomizer_hoho_correct,(u32 acch,u32 background));
WWHD_GAME_FUNC(0x021E5890,void,randomizer_hoho_collision,(u32 actor,u32 cylinder,u32 center,f32 radius,f32 height));
WWHD_GAME_FUNC(0x025F1884,void,randomizer_hoho_rotate,(u32 matrix,s32 angle));
WWHD_GAME_FUNC(0x028E8F64,void,randomizer_hoho_transform,(u32 matrix,u32 source,u32 target));
WWHD_GAME_FUNC(0x028E8D88,void,randomizer_hoho_add,(u32 a,u32 b,u32 target));
WWHD_GAME_FUNC(0x021E6680,void,randomizer_hoho_look,(u32 actor));
WWHD_GAME_FUNC(0x021E57B0,void,randomizer_hoho_matrix,(u32 actor));
#ifndef RANDOMIZER_HOHO_CALL
#define RANDOMIZER_HOHO_CALL(target,actor) ((void(*)(u32))(target))((actor))
#endif
WWHD_REPLACE(0x021E688C,s32,randomizer_hoho_execute,(u32 actor)) {
    const u32 arg=randomizer_hoho_argument(actor);
    if(arg==2 || arg==5 || arg==9) {
        if(arg==9)(void)randomizer_hoho_switch_bit(actor);
        (void)*(volatile u8*)(actor+0x2FE);
        (void)WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    }
    randomizer_hoho_attention(actor);randomizer_hoho_order_check(actor);
    const u32 game=wwhd_dComIfG_getGameInfo();
    if(!*(volatile u8*)(game+0x5292) || *(volatile u16*)(actor+0xF8)==1) {
        const u32 index=*(volatile u8*)(actor+0x8BA);
        const u32 entry=(u32)&WWHD_GAME_DATA(0x101BB348,u8)+index*8;
        const s32 slot=*(volatile s16*)(entry+2),adjust=*(volatile s16*)entry;
        const u32 object=actor+adjust;
        u32 target;
        if(slot<0)target=*(volatile u32*)(entry+4);
        else {
            const s32 offset=*(volatile s16*)(entry+6);
            const u32 table=*(volatile u32*)(object+offset);
            target=*(volatile u32*)(table+(u32)slot*8+4);
        }
        RANDOMIZER_HOHO_CALL(target,object);
    } else randomizer_hoho_event(actor);
    randomizer_hoho_order(actor);randomizer_hoho_texture(actor);randomizer_hoho_animation(actor);
    randomizer_hoho_move(actor,actor+0x654);
    randomizer_hoho_correct(actor+0x450,wwhd_dComIfG_getGameInfo()+0x12A0);
    unsigned int center[3];
    center[2]=*(volatile unsigned int*)(actor+0x31C);
    center[1]=*(volatile unsigned int*)(actor+0x318);
    const f32 height=WWHD_GAME_DATA(0x10015FB0,f32);
    const f32 radius=WWHD_GAME_DATA(0x101BB3B0,f32);
    center[0]=*(volatile unsigned int*)(actor+0x314);
    randomizer_hoho_collision(actor,actor+0x690,(u32)center,radius,height);
    unsigned int attention[3];
    attention[2]=WWHD_GAME_DATA(0x101BB398,unsigned int);
    attention[1]=WWHD_GAME_DATA(0x101BB394,unsigned int);
    attention[0]=WWHD_GAME_DATA(0x101BB390,unsigned int);
    const s32 angle=*(volatile s16*)(actor+0x322);
    const u32 matrix=(u32)&WWHD_GAME_DATA(0x1048D0CC,u8);
    randomizer_hoho_rotate(matrix,angle);
    randomizer_hoho_transform(matrix,(u32)attention,(u32)attention);
    randomizer_hoho_add((u32)attention,actor+0x314,(u32)attention);
    *(volatile unsigned int*)(actor+0x394)=attention[1];
    *(volatile unsigned int*)(actor+0x398)=attention[2];
    *(volatile unsigned int*)(actor+0x390)=attention[0];
    const f32 offset=WWHD_GAME_DATA(0x101BB39C,f32),y=*(volatile f32*)(actor+0x318);
    const unsigned int z=*(volatile unsigned int*)(actor+0x31C),x=*(volatile unsigned int*)(actor+0x314);
    const f32 eye=y+offset;
    *(volatile unsigned int*)(actor+0x384)=z;*(volatile unsigned int*)(actor+0x37C)=x;
    *(volatile f32*)(actor+0x380)=eye;
    randomizer_hoho_look(actor);randomizer_hoho_matrix(actor);return 0;
}
