/* Complete CC0 a874e1a9 d/d_com_inf_game_2.cpp025250C0 consumer.
 * MIT randomizer make_game_nonlinear.asm025255F4 ->025252DC skips only
 * Hyrule's story/shard layer selection; every other branch is retained. */
#pragma once
WWHD_GAME_FUNC(0x02556C34,s32,rhl_hour,(void));
WWHD_GAME_FUNC(0x02556BC0,u32,rhl_night,(void));
WWHD_GAME_FUNC(0x025B8B94,u32,rhl_event_native,(u32 save,u32 flag));
WWHD_GAME_FUNC(0x025B7E00,u32,rhl_triforce_native,(u32 save));
#ifndef RHL_LITERAL
#define RHL_LITERAL(a) (a)
#endif
#ifndef RHL_PTR
#define RHL_PTR(a,o) (*(volatile u32*)((a)+(o)))
#endif
typedef struct { volatile u32 text, vtable; } rhl_string;
static void rhl_terminate(rhl_string* value) {
    const u32 callback=RHL_PTR(value->vtable,0x14);
    ((void(*)(u32))callback)((u32)value);
}
static s32 rhl_equal(u32 a,u32 b) {
    if(a==b)return 1;
    for(unsigned int i=0;i<0x40001;i++) {
        const u8 left=*(volatile u8*)(a+i);
        const u8 right=*(volatile u8*)(b+i);
        if(left!=right)return 0;
        if(left==0)return 1;
    }
    return 0;
}
static s32 rhl_stage_is(u32 literal) {
    rhl_string a={literal,0x1004B8C0};
    const u32 play=wwhd_dComIfG_getGameInfo();
    rhl_string b={play+0x5134,0x1004B8C0};
    rhl_terminate(&a);
    rhl_terminate(&a);
    /* Native captures a.text before b's callback, but reads b.text afterward. */
    const u32 captured=a.text;
    rhl_terminate(&b);
    return rhl_equal(captured,b.text);
}
static u32 rhl_event(u32 flag) {
    return rhl_event_native(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,flag);
}
static u32 rhl_triforce(void) {
    return rhl_triforce_native(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0xD4);
}
WWHD_REPLACE(0x025250C0,s32,randomizer_hyrule_enemy_layer,(s32 i_roomNo)) {
    if ((*(volatile s8*)(wwhd_dComIfG_getGameInfo()+0x513F)) >= 0)
        return (*(volatile s8*)(wwhd_dComIfG_getGameInfo()+0x513F));
    s32 hour = rhl_hour();
    s32 layer;
    if (rhl_night() != 0)
        layer = 1;
    else
        layer = ((unsigned int)hour - 6u < 12u) ? 0 : 1;
    if (rhl_stage_is(RHL_LITERAL(0x1004BC68))) {
        if (i_roomNo == 0x2C) {
            if (rhl_event(0x520))
                return layer | 4;
            if (rhl_event(0xE20))
                return layer | 2;
            if (rhl_event(0x101))
                return 9;
            return layer;
        }
        if (i_roomNo == 0xB) {
            if (rhl_event(0x2D01))
                return layer | 4;
            if (rhl_night() != 0)
                return layer | 2;
            return layer;
        }
        if (i_roomNo == 1) {
            if (rhl_event(0x1820))
                return 3;
            return 1;
        }
        return layer;
    }
    if (rhl_stage_is(RHL_LITERAL(0x1004BC74)))
        return rhl_event(0xF80) ? (layer | 2) : layer;
    if (rhl_stage_is(RHL_LITERAL(0x1004BC6C)))
        return rhl_event(0x520) ? (layer | 2) : layer;
    if (rhl_stage_is(RHL_LITERAL(0x1004BC7C))) {
        /* MIT upstream025255F4 branches to the common unmodified layer return.
         * No shard/event callback is performed for this stage after the patch. */
        return layer;
    }
    if (rhl_stage_is(RHL_LITERAL(0x1004BC84))) {
        if (rhl_triforce() == 8 && rhl_event(0x2C01) == 0)
            return layer | 4;
        if (rhl_event(0x3280))
            return layer | 2;
        return rhl_event(0x3B40) ? (layer | 6) : layer;
    }
    if (rhl_stage_is(RHL_LITERAL(0x1004BC58))) {
        if (rhl_event(0x2C01))
            return layer | 6;
        if (rhl_event(0x3802) && rhl_event(0x3280) == 0)
            return layer | 6;
        if (rhl_triforce() == 8)
            return layer | 4;
        return rhl_event(0x3802) ? (layer | 2) : layer;
    }
    if (rhl_stage_is(RHL_LITERAL(0x1004BC60)))
        return rhl_event(0x2D01) ? (layer | 2) : layer;
    if (rhl_stage_is(RHL_LITERAL(0x1004BC8C)))
        return rhl_event(0x3B02) == 0 ? 8 : layer;
    if (rhl_stage_is(RHL_LITERAL(0x1004BC94)))
        return rhl_event(0x4002) == 0 ? 8 : layer;
    return layer;
}
