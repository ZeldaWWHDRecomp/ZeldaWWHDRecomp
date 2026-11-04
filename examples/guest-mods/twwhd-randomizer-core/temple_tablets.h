/* Temple tablet branches: upstream make_game_nonlinear.asm (MIT).
 * Source consumers: public CC0 d_a_obj_mknjd.cpp a874e1a9. Only the three
 * ownership-dependent branch decisions change; actual queries/callbacks remain. */
#pragma once
#include "reward_zunari_state.h"
static u32 randomizer_leaf_return_scope;
static u32 randomizer_leaf_return_actor;
#ifndef RANDOMIZER_LEAF_CONSTRUCTED
#define RANDOMIZER_LEAF_CONSTRUCTED(a) ((*(volatile u32*)((a)+0x2E4)&8)!=0)
#endif
#ifndef RANDOMIZER_TABLET_EVENT_NAME
#define RANDOMIZER_TABLET_EVENT_NAME(i) (*(volatile u32*)(0x101CAF8C+4*(i)))
#endif
#ifndef R_TABLET_U8
#define R_TABLET_U8(a,o) (*(volatile u8*)((a)+(o)))
#endif
#ifndef R_TABLET_U16
#define R_TABLET_U16(a,o) (*(volatile u16*)((a)+(o)))
#endif
#ifndef R_TABLET_U32
#define R_TABLET_U32(a,o) (*(volatile u32*)((a)+(o)))
#endif
WWHD_GAME_FUNC(0x02372D7C,void,randomizer_tablet_init_matrix,(u32 actor));
WWHD_GAME_FUNC(0x02372C44,void,randomizer_tablet_set_matrix,(u32 actor));
WWHD_GAME_FUNC(0x025D674C,void,randomizer_tablet_cull_box,(u32 actor,f32 x0,f32 y0,f32 z0,f32 x1,f32 y1,f32 z1));
WWHD_GAME_FUNC(0x025200D4,u32,randomizer_tablet_play,(void));
WWHD_GAME_FUNC(0x02543F10,s32,randomizer_tablet_event_index,(u32 manager,u32 name,s32 map));
WWHD_GAME_FUNC(0x0253E9B0,void,randomizer_tablet_event_name,(u32 info,u32 name));
WWHD_GAME_FUNC(0x0254DA50,s32,randomizer_tablet_query,(u8 item,s32 flag));
WWHD_GAME_FUNC(0x02373A30,void,randomizer_tablet_private_cut,(u32 actor));
WWHD_GAME_FUNC(0x02373360,u16,randomizer_tablet_talk,(u32 actor,s32 mode));
WWHD_GAME_FUNC(0x025E18EC,void,randomizer_tablet_bgm,(u32 music));
WWHD_GAME_FUNC(0x023730B4,void,randomizer_tablet_friends,(s32 visible));
WWHD_GAME_ORIGINAL(0x02373EA8,s32,randomizer_native_tablet_execute,(u32 actor,u32 matrix));

/* Original model initialization and event lookup order. The game's query still
 * runs, but an unowned song cannot select the first-tablet/lesson branch. */
WWHD_REPLACE(0x02372DA8,s32,randomizer_tablet_create,(u32 actor)) {
    u32 model=R_TABLET_U32(actor,0x540);
    R_TABLET_U32(actor,0x348)=model?model+0xC8:0;
    randomizer_tablet_init_matrix(actor);
    model=R_TABLET_U32(actor,0x544);
    R_TABLET_U32(actor,0x348)=model?model+0xC8:0;
    randomizer_tablet_init_matrix(actor);
    randomizer_tablet_cull_box(actor,-400.0f,-1.0f,-400.0f,400.0f,405.0f,400.0f);
    for(u32 offset=0x3E0;offset<0x538;offset+=4)R_TABLET_U32(actor,offset)=0;
    R_TABLET_U8(actor,0x555)=0;
    R_TABLET_U32(actor,0x550)=0;
    for(u32 i=0;i<4;i++)R_TABLET_U32(actor,0x558+4*i)=0;
    const u32 wind=R_TABLET_U8(actor,0x556)==1;
    const u32 indices[4]={2,0,4,6};
    for(u32 i=0;i<4;i++) {
        const u32 name=RANDOMIZER_TABLET_EVENT_NAME(indices[i]+wind);
        const u32 play=randomizer_tablet_play();
        R_TABLET_U16(actor,0x5F4+2*i)=(u16)randomizer_tablet_event_index(play+0x52C4,name,0xFF);
    }
    R_TABLET_U8(actor,0x5FD)=wind?4:3;
    R_TABLET_U8(actor,0x5FE)=wind?0x71:0x70;
    randomizer_tablet_event_name(actor+0xF8,wind?0x1002CB04:0x1002CB14);
    /* Native event-name observers can update fields; reload after that call. */
    R_TABLET_U16(actor,0x548)=wind?0x2910:0x2920;
    R_TABLET_U32(actor,0x39C)|=0x20000008;
    R_TABLET_U8(actor,0x389)=0x3D;R_TABLET_U8(actor,0x38B)=0x3D;
    (void)randomizer_tablet_query(R_TABLET_U8(actor,0x5FE),1);
    R_TABLET_U8(actor,0x557)=0;
    R_TABLET_U8(actor,0x614)=0;
    R_TABLET_U32(actor,0x610)=0xFFFFFFFF;
    R_TABLET_U8(actor,0x61C)=0;
    return 1;
}

/* Only the two native song-query states need a source consumer. Other states,
 * including demo cuts and ordinary talk initiation, retain the native method. */
WWHD_REPLACE(0x02373EA8,s32,randomizer_tablet_execute,(u32 actor,u32 matrix)) {
    const u32 state=R_TABLET_U8(actor,0x557);
    if(state!=10 && state!=12)return randomizer_native_tablet_execute(actor,matrix);
    const u32 player=R_TABLET_U32(randomizer_tablet_play(),0x5B2C);
    (void)R_TABLET_U32(randomizer_tablet_play(),0x5B38);
    if(state==10) {
        randomizer_tablet_private_cut(actor);
        const u32 play=randomizer_tablet_play();
        if(R_TABLET_U8(play,0x5292)==0) {
            (void)randomizer_tablet_query(R_TABLET_U8(actor,0x5FE),1);
            R_TABLET_U8(actor,0x557)=0;
        }
    } else if(randomizer_tablet_talk(actor,1)==0x12) {
        R_TABLET_U32(player,0x3B8)&=~0x08000000u;
        const u32 play=randomizer_tablet_play();
        R_TABLET_U16(play,0x52B8)|=8;
        (void)randomizer_tablet_query(R_TABLET_U8(actor,0x5FE),1);
        R_TABLET_U8(actor,0x557)=0;
    }
    randomizer_tablet_set_matrix(actor);
    R_TABLET_U32(matrix,0)=0x1046A618;
    if(R_TABLET_U16(actor,0x54A)>1)--R_TABLET_U16(actor,0x54A);
    if(R_TABLET_U16(actor,0x54A)==1) {
        randomizer_tablet_bgm(R_TABLET_U8(actor,0x556)==1?0x80000021:0x80000022);
        R_TABLET_U16(actor,0x54A)=0;
    }
    if(R_TABLET_U16(actor,0x54C)>1)--R_TABLET_U16(actor,0x54C);
    if(R_TABLET_U16(actor,0x54C)==1) {
        randomizer_tablet_friends(1);
        R_TABLET_U16(actor,0x54C)=0;
    }
    return 1;
}
#undef R_TABLET_U8
#undef R_TABLET_U16
#undef R_TABLET_U32

/* Keep the leaf-return collision actor alive after Farore's Pearl. Its native
 * constructor and complete status/cylinder setup still execute in order. Its
 * native status bit8 is set only after constructor calls, immediately before
 * the Farore query. Constructor callbacks must see actual ownership. */
WWHD_GAME_ORIGINAL(0x024B26CC,s32,randomizer_native_leaf_return_create,(u32 actor));
WWHD_REPLACE(0x024B26CC,s32,randomizer_leaf_return_create,(u32 actor)) {
    const u32 previous=randomizer_leaf_return_scope;
    const u32 previous_actor=randomizer_leaf_return_actor;
    randomizer_leaf_return_scope=1;
    randomizer_leaf_return_actor=actor;
    const s32 result=randomizer_native_leaf_return_create(actor);
    randomizer_leaf_return_scope=previous;
    randomizer_leaf_return_actor=previous_actor;
    return result;
}
WWHD_GAME_ORIGINAL(0x0254DA50,s32,randomizer_native_tablet_item,(u8 item,s32 flag));
WWHD_REPLACE(0x0254DA50,s32,randomizer_tablet_item,(u8 item,s32 flag)) {
    if(randomizer_zunari_item_query(item,flag))return wwhd_save_event_test(0x6940)==1;
    if(randomizer_leaf_return_scope && randomizer_leaf_return_actor &&
       RANDOMIZER_LEAF_CONSTRUCTED(randomizer_leaf_return_actor) && item==0x6B && flag==1) {
        randomizer_leaf_return_scope=0;
        return 0;
    }
    return randomizer_native_tablet_item(item,flag);
}
