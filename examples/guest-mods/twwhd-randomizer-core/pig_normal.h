/* Public CC0 d_a_kb_exec.cpp, Execute helper expanded from the native consumer. */
#pragma once
static inline s32 rp_kb_player_near(u32 i_this) {
    return rp_player_distance(i_this) < 200.0f;
}
static inline s32 rp_kb_player_crawl() {
    return (RP_U32(rp_play(),0x5CD8) & 0x08000000) != 0;
}

/* inlined into daKb_Execute */
static inline void rp_normal_move(u32 i_this) {
    if (rp_swim_change(i_this)) {
        return;
    }
    f32 diffX = RP_VEC(i_this,0x7A4).x - RP_VEC(i_this,0x314).x;
    f32 diffZ = RP_VEC(i_this,0x7A4).z - RP_VEC(i_this,0x314).z;
    rp_sqrt(rp_fma(diffZ, diffZ, diffX + diffX)); /* unused (GameCube: temp1) */
    s16 ang = rp_atan(diffX, diffZ);
    s16 mode = RP_S16(i_this,0x768);
    RP_S16(i_this,0x76A) = ang;

    switch (mode) {
    case 0:
        RP_S16(i_this,0x768) = mode + 1;
        RP_S16(i_this,0x76E +2*(0)) = (s16)rp_truncate(rp_random_f(50.0f) + 50.0f);
        RP_ANGLE(i_this,0x328).z = 0;
        if (RP_S32(i_this,0x8BC) != 0x17) {
            rp_animation(i_this, 0x17, 5.0f, 2, 1.0f,
                     0xC);
        }
        RP_F32(i_this,0x7FC) = 0.0f;
        RP_F32(i_this,0x800) = 2.5f;
        /* fallthrough */
    case 1:
        if (rp_food_check(i_this)) {
            return;
        }
        if (RP_S16(i_this,0x76E +2*(0)) == 0) {
            RP_S16(i_this,0x768) = 2;
        }
        // HD: GameCube m408 == 0 became mbCanBeBigPig != 4
        if (rp_kb_player_near(i_this) && RP_U8(i_this,0x74A) != 4) {
            if (rp_kb_player_crawl()) {
                RP_S16(i_this,0x76E +2*(1)) = (s16)rp_truncate(rp_random_f(60.0f) + 60.0f);
            } else if (RP_S16(i_this,0x76E +2*(1)) == 0 && RP_U8(i_this,0x74D) == 0) {
                RP_S16(i_this,0x768) = 4;
            }
        }
        break;
    case 2:
        RP_S16(i_this,0x768) = mode + 1;
        RP_S16(i_this,0x76E +2*(0)) = (s16)rp_truncate(rp_random_f(50.0f) + 50.0f);
        RP_F32(i_this,0x7FC) = 3.0f;
        if (RP_S32(i_this,0x8BC) != 0x18) {
            rp_animation(i_this, 0x18, 5.0f, 2, 1.0f,
                     0xD);
        }
        RP_F32(i_this,0x800) = 2.0f;
        rp_target(i_this, 0);
        RP_S16(i_this,0x76C) = 0x800;
        /* fallthrough */
    case 3: {
        if (rp_food_check(i_this)) {
            return;
        }
        if (RP_S16(i_this,0x76E +2*(0)) == 0) {
            RP_S16(i_this,0x768) = 0;
            break;
        }
        if (rp_kb_player_near(i_this)) {
            if (RP_U8(i_this,0x74A) == 4) {
                break;
            }
            if (rp_kb_player_crawl()) {
                RP_S16(i_this,0x76E +2*(1)) = (s16)rp_truncate(rp_random_f(30.0f) + 30.0f);
                break;
            } else if (RP_S16(i_this,0x76E +2*(1)) == 0 && RP_U8(i_this,0x74D) == 0) {
                RP_S16(i_this,0x768) = 4;
                break;
            }
        }
        if (RP_F32(i_this,0x814) != 0.0f) {
            if (RP_VEC(i_this,0x798).x != 0.0f || RP_VEC(i_this,0x798).y != 0.0f || RP_VEC(i_this,0x798).z != 0.0f) {
                f32 dx = RP_VEC(i_this,0x798).x - RP_VEC(i_this,0x314).x;
                f32 dz = RP_VEC(i_this,0x798).z - RP_VEC(i_this,0x314).z;
                f64 d = rp_sqrt(rp_fma(dx, dx, dz * dz));
                f32 half = RP_F32(i_this,0x814) * 0.5f;
                if (d > half) {
                    s16 a = rp_atan(dx, dz);
                    RP_S16(i_this,0x76A) = a;
                    rp_rotate_y(WWHD_GAME_DATA(0x1018C7B0,u32), a);
                    rp_vector temp;
                    rp_set_vector(&temp,0.0f, 0.0f, 1000.0f);
                    rp_mtx_position(&temp, &RP_VEC(i_this,0x7A4));
                    rp_vec_add(&RP_VEC(i_this,0x7A4), &RP_VEC(i_this,0x314), &RP_VEC(i_this,0x7A4));
                }
            }
        }
        break;
    }
    case 4:
        if (RP_U8(i_this,0x74A) != 2 && RP_U8(i_this,0x74A) != 3 &&
            RP_U8(i_this,0x74A) != 4) {
            RP_F32(i_this,0x814) = 0.0f;
        }
        RP_S16(i_this,0x768) += 1;
        RP_F32(i_this,0x7FC) = 12.0f;
        if (RP_U8(i_this,0x748) >= 8) {
            RP_F32(i_this,0x7FC) = 6.0f; /* 12 * 0.5 */
        }
        if (RP_S32(i_this,0x8BC) != 0x16) {
            rp_animation(i_this, 0x16, 5.0f, 2, 1.5f,
                     0xB);
        }
        RP_F32(i_this,0x800) = 2.0f;
        RP_S16(i_this,0x76E +2*(0)) = 0;
        RP_S16(i_this,0x76C) = 0x2000;
        /* fallthrough */
    case 5:
        if (RP_S16(i_this,0x76E +2*(0)) == 0) {
            rp_target(i_this, 1);
            RP_S16(i_this,0x76E +2*(0)) = (s16)rp_truncate(rp_random_f(10.0f) + 10.0f);
        }
        if (rp_checkPass(i_this, 0.0f) || rp_checkPass(i_this, 5.0f)) {
            if (RP_U8(i_this,0x748) >= 8 && rp_checkPass(i_this, 0.0f)) {
                rp_start_shock(i_this);
                rp_sound(i_this, 0x593E);
            }
            if (RP_S16(i_this,0x76E +2*(2)) == 0) {
                if (RP_U8(i_this,0x748) >= 8) {
                    rp_sound(i_this, 0x4915);
                } else {
                    rp_sound(i_this, 0x4820);
                }
            }
        }
        if (rp_player_distance(i_this) > 400.0f) {
            RP_S16(i_this,0x768) = 0;
        }
        break;
    case 6:
        if (rp_stopped(i_this)) {
            RP_S16(i_this,0x768) = 2;
            RP_F32(i_this,0x80C) = 30.0f;
            RP_S16(i_this,0x76E +2*(1)) = (s16)rp_truncate(rp_random_f(30.0f) + 30.0f);
        }
        break;
    }

    if (RP_S16(i_this,0x768) > 1 && RP_S16(i_this,0x768) != 6) {
        if (RP_U8(i_this,0x753) == 0) {
            rp_angle_calc(&RP_ANGLE(i_this,0x320).y, RP_S16(i_this,0x76A), 4, RP_S16(i_this,0x76C));
        }
        if (RP_S16(i_this,0x768) == 5) {
            if (RP_S16(i_this,0x76E +2*(2)) == 0 &&
                (s16)rp_distance_angle(RP_ANGLE(i_this,0x320).y, RP_S16(i_this,0x76A)) > 0x4000) {
                if (RP_U8(i_this,0x748) >= 8) {
                    rp_sound(i_this, 0x4916);
                } else {
                    rp_sound(i_this, 0x4821);
                }
                RP_S16(i_this,0x76E +2*(2)) = 0x18;
            }
            if (RP_U8(i_this,0x753) == 0) {
                if (__builtin_fabsf(RP_F32(i_this,0x370) - RP_F32(i_this,0x7FC)) < 0.2f) {
                    s16 z = (s16)rp_truncate((f32)(RP_ANGLE(i_this,0x320).y - RP_S16(i_this,0x76A)) * 0.5f);
                    if (z < 0) {
                        if ((int)(u16)z < (u16)-0x2000) {
                            z = -0x2000;
                        }
                    } else if (z > 0x2000) {
                        z = 0x2000;
                    }
                    RP_ANGLE(i_this,0x328).z = z;
                } else {
                    RP_ANGLE(i_this,0x328).z = 0;
                }
            }
        }
    }

    rp_calc(&RP_F32(i_this,0x370), RP_F32(i_this,0x7FC), 1.0f, RP_F32(i_this,0x800));
    RP_U32(i_this,0x39C) |= 0x00000010;
    rp_carry(i_this);
    rp_angle_calc(&RP_ANGLE(i_this,0x328).y, RP_ANGLE(i_this,0x320).y, 2, 0x1000);
}

/* inlined into daKb_Execute */
