/* Public CC0 d_a_kb_exec.cpp, Execute helper expanded from the native consumer. */
#pragma once
static inline void rp_swim_move(u32 i_this) {
    unsigned int linChk[27];
    rp_line_construct((u32)linChk);
    rp_angle temp;
    rp_copy_angle(&temp, &RP_ANGLE(i_this,0x328));
    int temp3 = 0;
    f32 temp2 = rp_water(i_this);

    switch (RP_S16(i_this,0x768)) {
    case 0x14:
        RP_U32(i_this,0xBEC) &= ~1u; /* OffTgSetBit */
        rp_clear_hit(i_this+0xBD4);
        RP_S8(i_this,0x3A1) = 10;
        RP_S16(i_this,0x77E) = 0;
        RP_S16(i_this,0x780) = 0;
        RP_S16(i_this,0x782) = 0;
        RP_ANGLE(i_this,0x328).z = 0;
        RP_U32(i_this,0x39C) &= ~(u32)0x00000004;
        RP_U32(i_this,0x39C) &= ~(u32)0x00000001;
        RP_U32(i_this,0xC00) |= 1u;
        rp_animation(i_this, 0x13, 5.0f, 2, 1.0f,
                 8);
        RP_F32(i_this,0x370) = 1.5f;
        RP_VEC(i_this,0x33C).x = 0.0f;
        RP_VEC(i_this,0x33C).z = 0.0f;
        RP_ANGLE(i_this,0x328).z = 0;
        RP_F32(i_this,0x374) = 0.0f;
        RP_VEC(i_this,0x33C).y = -20.0f;
        RP_S16(i_this,0x768) += 1;
        /* fallthrough */
    case 0x15:
        rp_angle_calc(&RP_ANGLE(i_this,0x328).x, 0, 1, 0x2000);
        rp_angle_calc(&RP_ANGLE(i_this,0x328).z, 0, 1, 0x2000);
        rp_zero(&RP_VEC(i_this,0x33C).y, 1.0f, 2.0f);
        if (RP_VEC(i_this,0x33C).y > -0.1f) {
            RP_ANGLE(i_this,0x328).x = 0;
            RP_ANGLE(i_this,0x328).z = 0;
            RP_VEC(i_this,0x33C).y = 0.0f;
            RP_S16(i_this,0x768) += 1;
        }
        break;
    case 0x16: {
        f32 t3 = rp_fma(RP_F32(i_this,0x81C), rp_reg_f(8, 6) + 10.0f, 10.0f);
        if (RP_S16(i_this,0x76E +2*(6)) == 0) {
            RP_S16(i_this,0x76E +2*(6)) = (s16)rp_truncate(rp_random_f(15.0f) + 15.0f);
            if (RP_U8(i_this,0x748) >= 8) {
                rp_sound(i_this, 0x4915);
            } else {
                rp_sound(i_this, 0x4820);
            }
        }
        RP_S16(i_this,0x77E) += 0x800;
        f32 t4 = rp_fma(rp_sine(RP_S16(i_this,0x77E)), rp_reg_f(8, 7) + 5.0f, 29.0f);
        t4 = rp_fma(RP_F32(i_this,0x81C), rp_reg_f(8, 8) + 29.0f, t4);
        rp_calc(&RP_VEC(i_this,0x314).y, temp2 - t4, 1.0f, 3.0f);
        // HD: also when the pig touches the ground
        if ((RP_U32(i_this,0x9FC)&0x20) ||
            __builtin_fabsf(rp_water(i_this) - RP_F32(i_this,0xA68)) < t3) {
            RP_F32(i_this,0x96C) = 0.5f;
            if (RP_PTR(i_this,0x8E0) != 0) {
                RP_F32(i_this,0x8EC) = 0.0f;
            }
            if (RP_S32(i_this,0x8BC) != 0x18) {
                rp_animation(i_this, 0x18, 5.0f, 2, 1.0f,
                         0xD);
            }
            RP_S16(i_this,0x768) += 1;
            RP_F32(i_this,0x374) = -3.0f;
        }
        rp_carry(i_this);
        break;
    }
    case 0x17:
        if (rp_swim_change(i_this)) {
            rp_line_destruct((u32)linChk);
            return;
        }
        if (RP_S16(i_this,0x788) == 0) {
            rp_ripple_end((i_this+0x8DC));
            RP_S16(i_this,0x788) = 0;
            RP_S16(i_this,0x768) = 0x18;
        }
        if (RP_S16(i_this,0x76E +2*(6)) == 0) {
            RP_S16(i_this,0x76E +2*(6)) = (s16)rp_truncate(rp_random_f(15.0f) + 15.0f);
            if (RP_U8(i_this,0x748) >= 8) {
                rp_sound(i_this, 0x4915);
            } else {
                rp_sound(i_this, 0x4820);
            }
        }
        rp_carry(i_this);
        break;
    case 0x18:
        rp_animation(i_this, 0x11, 5.0f, 0, 1.0f, -1);
        RP_S16(i_this,0x77E) = 0;
        RP_F32(i_this,0x370) = 0.0f;
        RP_S16(i_this,0x768) += 1;
        break;
    case 0x19:
        if (rp_checkPass(i_this, 10.0f) || rp_checkPass(i_this, 11.0f)) {
            if (rp_checkPass(i_this, 11.0f)) {
                temp.y += 0x8000;
                temp3 = 1;
            }
            if (RP_PTR(i_this,0x948+0x14*(temp3)) == 0) {
                s8 roomNo = RP_S8(i_this,0x326);
                rp_particle_set(RP_PTR(rp_play(),0x5AB0), 0, 0x80B6 /* ID_AK_SN_PIGDASSUI */,
                                &RP_VEC(i_this,0x314), &temp, 0, 0xFF,
                                (i_this+0x944+0x14*(temp3)), roomNo, 0,
                                0, 0);
            }
            u32 pEmtr = RP_PTR(i_this,0x948+0x14*(temp3));
            if (pEmtr != 0 && RP_F32(i_this,0x81C) != 0.0f) {
                rp_scale(RP_PTR(i_this,0x948+0x14*(temp3)), 2.0f, 2.0f, 2.0f);
            }
        }
        if (rp_stopped(i_this) || rp_carry(i_this) == 1) {
            for (int i = 0; i < 2; i++) {
                rp_follow_end((i_this+0x944+0x14*(i)));
            }
            RP_S16(i_this,0x77E) = 0;
            RP_S16(i_this,0x780) = 0;
            RP_S16(i_this,0x788) = 0;
            if (RP_S16(i_this,0x766) != 1) {
                RP_U32(i_this,0xBEC) |= 1u;
                RP_U32(i_this,0x39C) |= 0x00000001;
                if (RP_U8(i_this,0x74C) == 0) {
                    rp_copy_vector((u32)&RP_VEC(i_this,0x798),(u32)&(RP_VEC(i_this,0x314)));
                }
                RP_S16(i_this,0x766) = 0;
                RP_S16(i_this,0x768) = 2;
                RP_S16(i_this,0x76E +2*(1)) = (s16)rp_truncate(rp_random_f(20.0f) + 20.0f);
                RP_S16(i_this,0x76E +2*(3)) = (s16)rp_truncate(rp_random_f(20.0f) + 20.0f);
            }
        }
        break;
    }

    rp_copy_angle(&RP_ANGLE(i_this,0x320), &RP_ANGLE(i_this,0x328));
    if (RP_S16(i_this,0x768) >= 0x18) {
        rp_line_destruct((u32)linChk);
        return;
    }
    if (RP_VEC(i_this,0x33C).y == 0.0f) {
        rp_angle_calc(&RP_ANGLE(i_this,0x328).x, 0, 4, 0x800);
        if (RP_S16(i_this,0x782) == 0) {
            rp_target(i_this, 2);
            RP_S16(i_this,0x782) = 1;
            if ((RP_U32(i_this,0x9FC)&0x10)) {
                rp_vector t1;
                rp_vector t2;
                rp_vector t3;
                rp_triangle_get((rp_play()+0x12A0), (i_this+0x994), &t1, &t2, &t3);
                s16 a = rp_atan(t1.x - RP_VEC(i_this,0x314).x, t1.z - RP_VEC(i_this,0x314).z);
                if ((s16)rp_distance_angle(RP_ANGLE(i_this,0x320).y, a) < 0x2000) {
                    if (rp_random() < 0.5f) {
                        RP_S16(i_this,0x76A) = RP_ANGLE(i_this,0x328).y + 0x4000;
                    } else {
                        RP_S16(i_this,0x76A) = RP_ANGLE(i_this,0x328).y - 0x4000;
                    }
                    RP_S16(i_this,0x782) = (s16)rp_truncate(rp_random_f(20.0f) + 5.0f);
                }
            }
        }
        rp_angle_calc(&RP_ANGLE(i_this,0x328).y, RP_S16(i_this,0x76A), 2, 0x200);
        if ((s16)rp_distance_angle(RP_ANGLE(i_this,0x328).y, RP_S16(i_this,0x76A)) < 0x100 &&
            RP_S16(i_this,0x782) > 0) {
            RP_S16(i_this,0x782) -= 1;
        }
    }
    rp_line_destruct((u32)linChk);
}
