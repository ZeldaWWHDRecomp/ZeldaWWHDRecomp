/* Complete CC0 a874e1a9 d_a_mt_exec.cpp source expansion. */
#pragma once
#include "magtail_movement.h"
/* rmt_fight (inlined): circle the player, then lunge */
/* inlined into daMt_Execute */
static inline void rmt_fight(u32 actor) {
    u32 player = rmt_player();
    s16 pitchOfs = 0;

    switch ((u8)RMT8(actor,0x571)) {
    case 0: {
        RMTS32(rmt_static(0x10465848),0) = 0;
        RMTS32(rmt_static(0x1046584C),0) = 0;
        rmt_search(0x021D7480, actor);
        s16 slotAngle[5];
        s32 cnt = RMTS32(rmt_static(0x1046584C),0);
        if (cnt <= 1) {
            for (int k = 0; k < 5; k++) slotAngle[k] = 0;
        } else if (cnt == 2) {
            slotAngle[0] = 0x2000;
            slotAngle[1] = 0x2000;
            slotAngle[2] = 0;
            slotAngle[3] = 0;
            slotAngle[4] = 0;
        } else if (cnt == 3) {
            slotAngle[0] = 0;
            slotAngle[1] = -0x3000;
            slotAngle[2] = 0x3000;
            slotAngle[3] = 0;
            slotAngle[4] = 0;
        } else if (cnt == 4) {
            slotAngle[0] = 0x2000;
            slotAngle[1] = -0x2000;
            slotAngle[2] = 0x5000;
            slotAngle[3] = -0x5000;
            slotAngle[4] = 0;
        } else {
            slotAngle[0] = 0;
            slotAngle[1] = 0x3000;
            slotAngle[2] = -0x3000;
            slotAngle[3] = 0x6000;
            slotAngle[4] = -0x6000;
        }
        u32 att = rmt_attention();
        if ((rmt_lockon(att) || ((u32)RMT32(att,0x20) & 0x20000000)) &&
            rmt_action_target(att, 0) == actor) {
            RMT8(rmt_static(0x101BADD0),0) = 0;
        } else {
            u8 s = (u8)(RMT8(rmt_static(0x101BADD0),0) + 1);
            if (s < 5) {
                RMT8(rmt_static(0x101BADD0),0) = s;
            } else {
                RMT8(rmt_static(0x101BADD0),0) = (u8)(s - 5);
            }
        }

        rmt_vector d;
        rmt_vector_subtract((u32)&RMTV(player,0x314), (u32)&d, (u32)&RMTV(actor,0x314));
        rmt_vector v;
        f32 dx = d.x;
        v.x = dx;
        v.z = d.z;
        v.y = d.y + (rmt_reg_f(6,5) + 90.0f);
        RMT16(actor,0x5B2) = rmt_atan(dx, d.z);
        f32 vz = v.z, vx = v.x;
        RMT16(actor,0x5B0) = (s16)-rmt_atan(v.y, rmt_sqrt(rmt_fma(vx, vx, vz * vz)));

        s16 ph = RMT16(actor,0x586);
        rmt_vector wob;
        wob.x = rmt_sin(ph * 500) * (rmt_reg_f(0,10) + 30.0f);
        f32 s1400 = rmt_sin(ph * 1400);
        wob.z = rmt_fma(rmt_sin(ph * 600), rmt_reg_f(0,12) + 30.0f, RMTF(rmt_hio(),0x28));
        wob.y = rmt_fma(s1400, rmt_reg_f(0,11) + 20.0f, 60.0f);
        pitchOfs = (s16)rmt_truncate(s1400 * (rmt_reg_f(0,16) + -2000.0f));
        rmt_rotate_y_start(rmt_matrix(), (s16)(RMTA(player,0x328).y + slotAngle[RMT8(rmt_static(0x101BADD0),0)]));
        rmt_position((u32)&wob, (u32)&v);
        rmt_calc((u32)&RMTV(actor,0x314).x, RMTV(player,0x314).x + v.x, 0.1f, rmt_reg_f(0,10) + 4.0f);
        rmt_calc((u32)&RMTV(actor,0x314).z, RMTV(player,0x314).z + v.z, 0.1f, rmt_reg_f(0,10) + 4.0f);

        f64 dist = rmt_distance(actor, rmt_player());
        if ((dist > RMTF(rmt_hio(),0x24) + 100.0f || RMT8(actor,0x1A15) != 0) && RMT16(actor,0x582) == 0) {
            /* out of reach: back to the floor, then crawl again */
            rmt_calc((u32)&RMTV(actor,0x314).y, RMTF(actor,0xDF8) + 40.0f, 0.5f, 3.0f);
            f32 y = RMTV(actor,0x314).y - (RMTF(actor,0xDF8) + 40.0f);
            RMT16(actor,0x5B0) = 0;
            pitchOfs = 0;
            if (__builtin_fabsf(y) < 2.0f) {
                RMT8(actor,0xD1D) = 1;
            }
        } else {
            rmt_calc((u32)&RMTV(actor,0x314).y, RMTF(actor,0xDF8) + RMTF(rmt_hio(),0x18) + v.y, 0.1f, 5.0f);
            if (RMT16(actor,0x572) == 0) {
                RMT16(actor,0x572) = RMT16(rmt_hio(),0x10);
                if (rmt_random(1.0f) < RMTF(rmt_hio(),0x14) && RMT16(actor,0x582) == 0) {
                    RMT16(actor,0xD1E) = 0;
                    RMT8(actor,0x571) = 1;
                    rmt_set_back_pos(actor, RMTA(actor,0x328).y, 30.0f, -30.0f);
                    rmt_animation(actor, 8, 5.0f, 2, 1.0f, 0);
                    if ((u32)((u32)&RMTV(actor,0x37C)) != 0) {
                        rmt_monster(actor, 0x4802);
                    }
                }
            }
        }
        break;
    }
    case 1: {
        /* the lunge */
        if (RMT8(player,0x3AC) == 0x10) {
            u32 att = rmt_attention();
            if ((rmt_lockon(att) || ((u32)RMT32(att,0x20) & 0x20000000)) &&
                rmt_action_target(att, 0) == actor) {
                /* grabbed by the player */
                RMT8(actor,0x570) = 6;
                RMT16(actor,0x572) = 0;
                rmt_animation(actor, 7, 2.0f, 0, 1.0f, 0);
                break;
            }
        }
        RMT16(actor,0xD1E) = RMT16(actor,0xD1E) + 1;
        if (RMT16(actor,0xD1E) >= RMT16(rmt_hio(),0x30) && RMT16(actor,0xD1E) <= RMT16(rmt_hio(),0x32)) {
            RMTF(actor,0x3B8) = 0.0f;
            RMTF(actor,0x3BC) = 20.0f;
            RMTF(actor,0x3B4) = 10000.0f;
            RMT8(actor,0x3C4) = 3;
            RMTF(actor,0x3B4) = RMTF(rmt_hio(),0x34);
            RMTF(actor,0x3C0) = 10.0f;
        }
        if (RMT16(actor,0xD1E) == RMT16(rmt_hio(),0x3C)) {
            rmt_animation(actor, 9, 2.0f, 0, 1.0f, 0);
        }
        if (RMT16(actor,0xD1E) == RMT16(rmt_hio(),0x38)) {
            rmt_rotate_y_start(rmt_matrix(), RMT16(actor,0x5B2));
            rmt_rotate_x(rmt_matrix(), RMT16(actor,0x5B0));
            rmt_vector v;
            v.x = 0.0f;
            v.y = rmt_reg_f(6,6) + -100.0f;
            v.z = rmt_reg_f(6,7) + 200.0f;
            rmt_position((u32)&v, (u32)&RMTV(actor,0x598));
            rmt_vector_add_native((u32)&RMTV(actor,0x598), (u32)&RMTV(actor,0x314), (u32)&RMTV(actor,0x598));
        }
        f32 rate, step;
        if (RMT16(actor,0xD1E) >= RMT16(rmt_hio(),0x38)) {
            step = RMTF(rmt_hio(),0x2C);
            rate = 0.5f;
            if (RMT16(actor,0xD1E) == RMT16(rmt_hio(),0x3E)) {
                rmt_animation(actor, 7, 2.0f, 0, 1.0f, 0);
            }
        } else {
            step = rmt_reg_f(0,11) + 4.0f;
            rate = 0.1f;
        }
        if (RMT16(actor,0xD1E) == (s16)(RMT16(rmt_hio(),0x3E) + rmt_reg_s(0,3))) {
            rmt_monster(actor, 0x5810);
        }
        if (RMT16(actor,0xD1E) < (s16)(RMT16(rmt_hio(),0x3E) + rmt_reg_s(6,7) + 3)) {
            RMT8(actor,0xD20) = 1;
        }
        if (RMT16(actor,0xD1E) == (s16)(RMT16(rmt_hio(),0x3E) + 2)) {
            RMT8(actor,0x19F0) = 1;
        }
        if (RMT16(actor,0xD1E) >= (s16)(RMT16(rmt_hio(),0x3E) + 2) && RMT16(actor,0xD1E) <= (s16)(RMT16(rmt_hio(),0x3E) + 15)) {
            RMT8(actor,0xD20) = 2;
        }
        rmt_calc((u32)&RMTV(actor,0x314).x, RMTV(actor,0x598).x, rate, step);
        rmt_calc((u32)&RMTV(actor,0x314).y, RMTV(actor,0x598).y + 20.0f, rate, step);
        rmt_calc((u32)&RMTV(actor,0x314).z, RMTV(actor,0x598).z, rate, step);
        RMTV(actor,0x33C).x = RMTV(actor,0x598).x - RMTV(actor,0x314).x;
        RMTV(actor,0x33C).y = -1.0f;
        RMTV(actor,0x33C).z = RMTV(actor,0x598).z - RMTV(actor,0x314).z;
        if (rmt_player_guard(player) && rmt_attack_hit((actor+0xF64+0x12C*(0)))) {
            /* bounced off the shield */
            RMT8(actor,0x571) = 15;
            rmt_set_back_pos(actor, RMTA(actor,0x328).y, 80.0f, -120.0f);
            RMT16(actor,0x57E) = 10;
            RMT16(actor,0x572) = 10;
            rmt_animation(actor, 10, 2.0f, 2, 1.0f, 0);
            RMT8(actor,0x19F0) = 0;
        }
        if (RMT16(actor,0xD1E) == RMT16(rmt_hio(),0x3A)) {
            RMT8(actor,0x571) = 0;
            RMT16(actor,0x572) = RMT16(rmt_hio(),0x10);
            rmt_animation(actor, 10, 5.0f, 2, 1.0f, 0);
        }
        break;
    }
    case 10: {
        f32 vy = RMTV(actor,0x33C).y;
        f32 ny = vy + RMTF(actor,0x374);
        RMTV(actor,0x314).y = RMTV(actor,0x314).y + vy;
        if (ny < -100.0f) {
            ny = -100.0f;
        }
        RMTV(actor,0x33C).y = ny;
        if (RMT32(actor,0xD8C) & 0x20 /* GROUND_HIT */) {
            RMT8(actor,0x571) = 0;
        }
        break;
    }
    case 15:
        rmt_calc((u32)&RMTV(actor,0x314).x, RMTV(actor,0x598).x, 0.5f, 20.0f);
        rmt_calc((u32)&RMTV(actor,0x314).y, RMTV(actor,0x598).y + 20.0f, 0.5f, 20.0f);
        rmt_calc((u32)&RMTV(actor,0x314).z, RMTV(actor,0x598).z, 0.5f, 20.0f);
        RMTV(actor,0x33C).x = RMTV(actor,0x598).x - RMTV(actor,0x314).x;
        RMTV(actor,0x33C).y = -1.0f;
        RMTV(actor,0x33C).z = RMTV(actor,0x598).z - RMTV(actor,0x314).z;
        if (RMT16(actor,0x572) == 0) {
            RMT8(actor,0x571) = 0;
            RMT16(actor,0x572) = RMT16(rmt_hio(),0x10);
            rmt_animation(actor, 10, 5.0f, 2, 1.0f, 0);
        }
        break;
    case 0x11:
        if (RMT32(actor,0xD8C) & 0x20 /* GROUND_HIT */) {
            RMT8(actor,0x571) = 0x17;
            s16 t = (s16)rmt_truncate(rmt_random_add(50.0f,50.0f));
            RMT16(actor,0x572) = t;
            RMT16(actor,0x5AA) = t;
        }
        rmt_fall(actor);
        break;
    case 0x14:
        /* knocked back */
        RMTA(actor,0x320).x = (s16)(RMTA(actor,0x320).x - 0x500);
        rmt_angle_calc((u32)&RMTA(actor,0x320).y, (s16)(rmt_atan(RMTV(actor,0x33C).x, RMTV(actor,0x33C).z) + 0x8000), 2, 0x400);
        rmt_calc((u32)&RMTF(actor,0x1A10), 0.9f, 1.0f, 0.1f);
        if (RMTV(actor,0x33C).y > 1.0f) {
            rmt_fall(actor);
        } else if (RMTS8(actor,0x1A14) != 0) {
            if (RMT32(actor,0xD8C) & 0x20 /* GROUND_HIT */) {
                RMTS8(actor,0x1A14) = 2;
            }
            rmt_fall(actor);
        } else {
            /* roll up */
            RMT32((actor+0xF64+0x12C*(0)),0x18) &= ~1u;
            rmt_texture(actor, 1);
            f32 sy = RMTV(actor,0x33C).y;
            f32 sz = RMTV(actor,0x33C).z;
            f32 sx = RMTV(actor,0x33C).x;
            RMT8(actor,0x570) = 2;
            f32 t = rmt_fma(sx, sx, sy * sy);
            RMT16(actor,0x582) = RMT16(rmt_hio(),0x54);
            RMTF(actor,0x594) = 0.0f;
            t = rmt_fma(sz, sz, t);
            RMT16(actor,0x5A6) = 0;
            RMTF(actor,0x44C) = 0.0f;
            RMTF(actor,0x370) = -rmt_sqrt(t);
            RMT8(actor,0x571) = 0;
            rmt_min(actor, -100.0f, -100.0f, -100.0f);
            rmt_max(actor, 100.0f, 100.0f, 100.0f);
            rmt_fall(actor);
        }
        break;
    case 0x17:
        RMTV(actor,0x33C).y = 0.0f;
        rmt_angle_calc((u32)&RMTA(actor,0x320).x, 0, 1, 0x400);
        RMTV(actor,0x33C).x = RMTV(actor,0x33C).x * 0.2f;
        RMTV(actor,0x33C).z = RMTV(actor,0x33C).z * 0.2f;
        if (RMT16(actor,0x572) == 0) {
            RMT8(actor,0x571) = 10;
        }
        rmt_fall(actor);
        break;
    }

    if (RMT8(actor,0x570) < 2) {
        if (RMT8(actor,0x571) < 10) {
            rmt_angle_calc((u32)&RMTA(actor,0x320).y, RMT16(actor,0x5B2), 2, 0x400);
            f32 h = RMTV(actor,0x314).y - RMTF(actor,0xDF8);
            s16 pitch = RMT16(actor,0x5B0);
            if (h > 250.0f) {
                RMT8(actor,0x571) = 10;
            }
            rmt_angle_calc((u32)&RMTA(actor,0x320).x, (s16)(pitch + pitchOfs), 4, 0x800);
            rmt_angle_calc((u32)&RMT16(actor,0x584), 0, 1, 0x100);
        } else {
            rmt_angle_calc((u32)&RMTA(actor,0x320).x, 0x3800, 4, 0x800);
            rmt_angle_calc((u32)&RMT16(actor,0x584), 0, 1, 0x100);
        }
    }

    /* shake */
    f32 amp = (f32)(s32)RMT16(actor,0x57E) * (rmt_reg_f(0,14) + 500.0f);
    RMTA(actor,0x5B6).y = (s16)rmt_truncate(rmt_sin_short((s16)(RMT16(actor,0x586) * 0x2100)) * amp);
    RMTA(actor,0x5B6).x = (s16)rmt_truncate(rmt_cos_short((s16)(RMT16(actor,0x586) * 0x2300)) * amp);
    rmt_angles_add((u32)&RMTA(actor,0x328), (u32)&RMTA(actor,0x320), (u32)&RMTA(actor,0x5B6));
    rmt_body2(actor);
    rmt_wall_check(actor);
    rmt_bg_check(actor);
    rmt_cc_move(actor);
}
