/* Complete CC0 a874e1a9 d_a_mt_exec.cpp source expansion. */
#pragma once
#include "magtail_actions.h"
/* 021D87A0 */
WWHD_REPLACE(0x021D87A0,s32,randomizer_magtail_execute,(u32 actor)) {
    u32 player = rmt_player();

    if (RMT8(actor,0x3D5) != 0 && RMTV(actor,0x314).y < RMTV(actor,0x2EC).y - 5000.0f) {
        /* fell out of the room: counts as defeated */
        rmt_on_switch_wrapper(RMT8(actor,0x3D5), RMTS8(actor,0x326));
        rmt_on_actor_wrapper(RMTU16(actor,0x2D8), RMTS8(actor,0x2FE));
        rmt_delete(actor);
        return 1;
    }
    if (rmt_ice((actor+0x1A1C))) {
        return 1;
    }
    if (RMT8(actor,0x3D7) != 0) {
        if (!rmt_switch_wrapper(RMT8(actor,0x3D7) - 1, RMTS8(actor,0x326))) {
            return 1;
        }
        RMT8(actor,0x3D7) = 0;
    }
    RMT32(actor,0x2E0) |= 0x20;

    /* lava */
    _Alignas(4) u8 ground_storage[0x54];
    const u32 gndChk=(u32)ground_storage;
    rmt_ground_init(gndChk);
    f32 y = RMTV(actor,0x314).y;
    f32 z = RMTV(actor,0x314).z;
    f32 x = RMTV(actor,0x314).x;
    RMTV(gndChk,0x24).x = x;
    RMTV(gndChk,0x24).y = y + 200.0f;
    RMTV(gndChk,0x24).z = z;
    f32 lava = rmt_ground_cross(rmt_background(), gndChk);
    if (lava != -1000000000.0f && RMTV(actor,0x314).y - 30.0f + rmt_reg_f(0,13) < lava) {
        if (RMT8(actor,0x1A16) == 0) {
            RMTF(actor,0x370) = RMTF(actor,0x370) * 0.1f;
            RMTV(actor,0x33C).y = 0.0f;
            rmt_vector pos;
            pos.x = x;
            pos.z = z;
            pos.y = lava;
            rmt_pillar((u32)&pos, 0.5f);
        }
        RMT8(actor,0x1A16) = 1;
        RMTF(actor,0x374) = -0.5f;
        if (RMTV(actor,0x33C).y < -5.0f) {
            RMTV(actor,0x33C).y = -5.0f;
        }
    } else {
        RMT8(actor,0x1A16) = 0;
        RMTF(actor,0x374) = -3.0f;
    }
    rmt_attention_flags(actor, 4);

    rmt_eye_tex_anm(actor);

    if (RMTS8(rmt_hio(),0x04) == 0) {
        if (RMT8(actor,0x19F0) != 0) {
            u8 n = (u8)(RMT8(actor,0x19F0) + 1);
            if (n == 11) {
                RMT8(actor,0x19F0) = 0;
            } else {
                RMT8(actor,0x19F0) = n;
            }
        }
        RMTF(actor,0x3B8) = 100.0f;
        RMT8(actor,0x3C4) = 0;
        RMTF(actor,0x3BC) = 100.0f;
        RMTF(actor,0x3B4) = 10000.0f;
        RMTF(actor,0x3C0) = 0.0f;
        for (int i = 0; i < 5; i++) {
            volatile s16* t=(volatile s16*)(actor+0x572+2*i);
            if (*t != 0) {
                *t = *t - 1;
            }
        }
        if (RMT16(actor,0x1A18) != 0) RMT16(actor,0x1A18) = RMT16(actor,0x1A18) - 1;
        if (RMT16(actor,0x57C) != 0) RMT16(actor,0x57C) = RMT16(actor,0x57C) - 1;
        if (RMT16(actor,0x57E) != 0) RMT16(actor,0x57E) = RMT16(actor,0x57E) - 1;
        if (RMT16(actor,0x466) != 0) RMT16(actor,0x466) = RMT16(actor,0x466) - 1;
        if (RMT16(actor,0x5AA) != 0) {
            RMT16(actor,0x5AA) = RMT16(actor,0x5AA) - 1;
            RMT16(actor,0x5AC) = RMT16(actor,0x5AC) + 1;
        } else if (RMT16(actor,0x5AC) != 0) {
            s16 n = RMT16(actor,0x5AC);
            if (n > 15) n = 15;
            RMT16(actor,0x5AC) = n - 1;
        }
        RMT16(actor,0x586) = RMT16(actor,0x586) + 1;
        if (RMT16(actor,0x582) != 0) RMT16(actor,0x582) = RMT16(actor,0x582) - 1;

        if (RMT8(actor,0x570) < 3) {
            rmt_damage_check(actor);
        }
        rmt_action(actor, player);
    }

    rmt_bg_line_check(actor);
    rmt_copy_vector(actor+0x390,actor+0x37C);

    s32 step = 1, lo, hi, bhi;
    switch ((u8)RMT8(actor,0x400)) {
    default:
        rmt_assert(0x10015740,0x108B,0x1001573C);
        /* fall through */
    case 0:
        lo = 0;
        hi = 40;
        bhi = 30;
        break;
    case 1:
        step = 2;
        lo = 40;
        hi = 100;
        bhi = 90;
        break;
    case 2:
        lo = 100;
        hi = 130;
        bhi = 120;
        break;
    }
    RMTS32(actor,0x404) = (s32)((unsigned int)RMTS32(actor,0x404) + (unsigned int)step);
    if (RMTS32(actor,0x404) > hi) {
        RMTS32(actor,0x404) = RMT8(actor,0x400) != 0 ? hi : lo;
    }
    RMTS32(actor,0x408) = (s32)((unsigned int)RMTS32(actor,0x408) + (unsigned int)step);
    if (RMTS32(actor,0x408) > bhi) {
        RMTS32(actor,0x408) = RMT8(actor,0x400) != 0 ? bhi : 0;
    }

    /* joint waves */
    u32 jnt=actor+0x75C; /* mJointAnglesLeft[15] and mJointAnglesRight[15] */
    if (RMT8(actor,0x570) >= 2 && RMT16(actor,0x582) > 120) {
        RMT16(actor,0x450) = 0;
        for (int i = 0; i < 30; i++) {
            RMTA(jnt,6*(i)).x = 0;
            rmt_angle_calc((u32)&RMTA(jnt,6*(i)).z, 10000, 10, 300);
        }
    } else {
        if (RMT16(actor,0x582) == 120) {
            RMT16(actor,0x5AC) = 15;
        }
        rmt_morf_play(RMTPTR(actor,0x3E0),(u32)&RMTV(actor,0x314), 0, 0);
        RMT16(actor,0x58A) = RMT16(actor,0x58A) + RMT16(rmt_hio(),0x08);
        for (int i = 0; i < 30; i++) {
            int j = i >= 15 ? i - 15 : i;
            s32 still = RMT16(actor,0x5AA) != 0 ? j < RMT16(actor,0x5AC) : 14 - j < RMT16(actor,0x5AC);
            if (still) {
                RMTA(jnt,6*(i)).x = (s16)rmt_truncate(-(rmt_sin(i * (rmt_reg_s(6,2) + 0x32C8)) * (rmt_reg_f(6,11) + 5000.0f)));
                f32 c = rmt_cos(i * (rmt_reg_s(6,3) + 0x32C8));
                s16 t = (s16)rmt_truncate(rmt_fma(c, rmt_reg_f(6,12) + 5000.0f, RMTF(rmt_hio(),0x0C)));
                rmt_angle_calc((u32)&RMTA(jnt,6*(i)).z, t, 1, RMT16(actor,0x450));
            } else {
                s16 ph = RMT16(actor,0x58A);
                RMTA(jnt,6*(i)).x = (s16)rmt_truncate(-(rmt_sin(ph + i * (rmt_reg_s(6,2) + 0x32C8)) * (rmt_reg_f(6,11) + 5000.0f)));
                f32 c = rmt_cos(ph + i * (rmt_reg_s(6,3) + 0x32C8));
                s16 t = (s16)rmt_truncate(rmt_fma(c, rmt_reg_f(6,12) + 5000.0f, RMTF(rmt_hio(),0x0C)));
                rmt_angle_calc((u32)&RMTA(jnt,6*(i)).z, t, 1, RMT16(actor,0x450));
            }
        }
        rmt_angle_calc((u32)&RMT16(actor,0x450), 0x2000, 1, 0x100);
        for (int k = 0; k < 8; k++) {
            f32 s = rmt_sin(RMT16(actor,0x586) * (rmt_reg_s(0,5) + 0x9C4) + k * (rmt_reg_s(0,6) + 0x1D4C));
            RMTF(actor,0x73C+4*(k)) = rmt_fma(s, RMTF(actor,0x454), 1.0f);
        }
        rmt_calc((u32)&RMTF(actor,0x454), 0.1f, 1.0f, 0.002f);
    }

    if (!(RMT8(actor,0x3D0) & 1)) {
        /* lava drips and glow along the body */
        for (int i = 0; i < 8; i++) {
            u32 model=RMTPTR(RMTPTR(actor,0x3E0+4*(i)),0x90);
            rmt_vector v;
            v.y = rmt_reg_f(0,6);
            v.z = rmt_reg_f(0,7);
            v.x = rmt_reg_f(0,5);
            u32 pos=actor+0x46C+12*i;
            rmt_matrix_vector((model+0xC8), (u32)&v, pos);
            u8 st = RMT8(actor,0x570);
            if ((st >= 2 && (RMT16(actor,0x582) > 40 || st == 6)) || RMT8(rmt_hio(),0x07) != 0) {
                RMTF(pos,4) = RMTF(pos,4) + 10000.0f;
            }
            if (i < 7) {
                rmt_simple(0x8058,pos);
            }
            if ((u32)(i - 1) < 6) {
                rmt_simple(0x8059,pos);
            }
            if (RMT8(actor,0x468) == 0 && (i == 1 || i == 3 || i == 5)) {
                rmt_particle_native(rmt_particle_controller(), 4, 0xC06C, pos, 0, 0, 0xFF,
                                (actor+0x4CC+0x14*i), -1, 0, 0, 0);
            }
        }
        RMT8(actor,0x468) = 1;
    }

    if (RMT8(actor,0x570) < 2 && RMT8(actor,0x1A16) == 0 && RMT16(actor,0x5AA) == 0) {
        if (RMT16(actor,0x578) == 0) {
            RMT16(actor,0x578) = (s16)rmt_truncate(rmt_random_add(45.0f,45.0f));
            rmt_monster(actor, 0x4801);
        }
        if (RMT16(actor,0x57A) == 0) {
            RMT16(actor,0x57A) = (s16)rmt_truncate(rmt_random_add(3.0f,6.0f));
            rmt_monster(actor, 0x580F);
        }
    }
    if (RMTS8(actor,0x1A14) >= 2) {
        rmt_explode(actor);
        RMTS8(actor,0x1A14) = -1;
    }
    rmt_ground_finish(gndChk);
    return 1;
}
