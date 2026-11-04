/* Source: public CC0 a874e1a9, d/actor/d_a_bgn_exec.cpp.
 * Complete 0207E7B4 consumer; upstream MIT remove_cutscenes.asm changes only
 * the final stage layer (9 -> -1). Native move remains an out-of-line call. */
#pragma once
#include "puppet_helpers.h"
// HD: Link's virtual slots are guest addresses, not host member pointers.
static void rb_bgn_setPlayerPosAndAngle(u32 player,volatile rb_vector* pos,s16 angle){((void(*)(u32,u32,s16))(RB_PTR(RB_PTR(player,0xB4),0x114)))(player,(u32)pos,angle);}

/* the camera block shared by every cut scene end (GameCube: label block_57) */
/* inlined into daBgn_Execute */
static inline void rb_bgn_demo_end(u32 i_this, u32 cam) {
    rb_camera_trim(cam, 0);
    rb_camera_start(cam);
    rb_meter_show();
    rb_event_reset();
    RB_S16(i_this,0x151CC) = 1;
    RB_S8(i_this,0x151CE) = 0;
}

// Defeat cutscene rope; keep the explicit guest word copies and scratch-vector order.
/* inlined into daBgn_Execute */
static inline void rb_last_himo_control(u32 i_this) {
    RB_VEC(i_this,0x149AC+12*(0)).x = -15.0f;
    RB_VEC(i_this,0x149AC+12*(0)).y = rb_add(rb_reg_f(8, 4), 4441.46f);
    RB_VEC(i_this,0x149AC+12*(0)).z = rb_add(rb_reg_f(8, 3), 375.17f);
    const u32 lines=RB_PTR(i_this,0x149A8);
    u32 pcVar8 = RB_PTR(lines,0); // HD: guest word view retains bit-exact vector copies.
    u32 puVar8 = RB_PTR(lines,4);
    RB_U8(puVar8,0) = 10;
    volatile rb_words *c0 = RB_WORD_PTR(&RB_VEC(i_this,0x149AC+12*(0)));
    RB_WORD_PTR(pcVar8)->x = (u32)c0->x;
    RB_WORD_PTR(pcVar8)->y = (u32)c0->y;
    RB_WORD_PTR(pcVar8)->z = (u32)c0->z;
    rb_vector local_a8;
    rb_vector local_b4;
    f32 c724;
    if (RB_S8(i_this,0x14C7C) == 1) {
        RB_S8(i_this,0x14C7C) = 2;
        for (s32 i = 0; i < 59; i++) {
            volatile rb_words *d = RB_WORD_PTR(&RB_VEC(i_this,0x149AC+12*(1 + i)));
            d->x = (u32)c0->x;
            d->y = (u32)c0->y;
            d->z = (u32)c0->z;
        }
        c724 = rb_add(rb_reg_f(0,19), 5000.0f);
        RB_F32(i_this,0x14C80) = c724;
    } else {
        rb_zero(&RB_F32(i_this,0x14C80), 1.0f, rb_add(rb_reg_f(0,18), 50.0f));
        c724 = RB_F32(i_this,0x14C80);
    }
    local_a8.x = 0.0f;
    local_a8.y = 0.0f;
    local_a8.z = rb_add(rb_reg_f(0,12), 45.0f);
    s16 k = 0;
    s16 j = 0;
    volatile rb_vector *pcVar5 = &RB_VEC(i_this,0x149AC+12*(1));
    volatile rb_vector *out = ((volatile rb_vector*)(pcVar8+12));
    volatile u8 *size = ((volatile u8*)(puVar8+1));
    for (s32 i = 1; i < 60; i++, pcVar5++, out++, size++) {
        f32 y = rb_add(rb_sub(rb_sub(pcVar5->y, pcVar5[-1].y), 35.0f),
                                rb_reg_f(0,13));
        f32 z = rb_sub(pcVar5->z, pcVar5[-1].z);
        f32 x = rb_sub(pcVar5->x, pcVar5[-1].x);
        if (c724 > 0.01f) {
            s16 c746 = RB_S16(i_this,0x14EB2);
            k = (s16)rb_truncate(
                rb_mul(rb_sine(c746 * 2000 + i * (rb_reg_s(0,3) + 2000)), c724));
            j = (s16)rb_truncate(
                rb_mul(rb_cosine(c746 * 0x8FC + i * (rb_reg_s(0,4) + 0x9C4)), c724));
        }
        u32 m = rb_matrix();
        s16 a = rb_atan(y, z);
        rb_rotate_x_start(m, (s16)(k - a));
        f32 d = rb_fma(y, y, rb_mul(z, z));
        m = rb_matrix();
        f64 sq = rb_sqrt(d);
        s16 b = rb_atan(x, sq);
        rb_rotate_y(m, (s16)(b + j));
        rb_position(&local_a8, &local_b4);
        f32 nx = rb_add(pcVar5[-1].x, local_b4.x);
        pcVar5->x = nx;
        pcVar5->y = rb_add(pcVar5[-1].y, local_b4.y);
        f32 nz = rb_add(pcVar5[-1].z, local_b4.z);
        pcVar5->z = nz;
        out->x = nx;
        out->y = pcVar5->y;
        out->z = nz;
        *size = 10;
        c724 = RB_F32(i_this,0x14C80);
    }
}
