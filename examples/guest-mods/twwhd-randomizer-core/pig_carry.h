/* Public CC0 d_a_kb_exec.cpp, Execute helper expanded from the native consumer. */
#pragma once
static inline void rp_carry_move(u32 i_this) {
    u32 pPlayer = rp_player();

    switch (RP_S16(i_this,0x768)) {
    case 0xA:
        RP_U32(i_this,0xC00) &= ~1u; /* OffCoSetBit */
        RP_ANGLE(i_this,0x328).z = 0;
        rp_angle_calc(&RP_ANGLE(i_this,0x83C).x, 0, 2, 0x400);
        rp_angle_calc(&RP_ANGLE(i_this,0x83C).z, 0, 2, 0x400);
        if (RP_F32(i_this,0x804) != RP_VEC(i_this,0x314).y) {
            RP_S16(i_this,0x780) += 1;
            if (RP_S16(i_this,0x780) == 4) {
                rp_particle_tuba(i_this);
            }
            rp_angle_calc(&RP_ANGLE(i_this,0x328).x, -0x7FFF, 1, 0x1000);
            RP_F32(i_this,0x80C) = 20.0f;
        }
        if (!(__builtin_fabsf(RP_VEC(i_this,0x314).y - RP_VEC(pPlayer,0x314).y) < 110.0f)) {
            RP_ANGLE(i_this,0x328).x = -0x7FFF;
            if (RP_U8(i_this,0x74A) == 3) {
                rp_gas(i_this);
            }
            RP_S16(i_this,0x76E +2*(2)) = 0;
            RP_F32(i_this,0x810) = 1.8f;
            rp_particle_tuba(i_this);
            rp_animation(i_this, 0x14, 5.0f, 0,
                     RP_F32(i_this,0x810), 9);
            rp_catch_sound(i_this);
            RP_U32(i_this,0x9FC)|=0x2000;
            RP_S16(i_this,0x792) = 3;
            RP_S16(i_this,0x768) = 0xB;
        }
        RP_ANGLE(i_this,0x320).y = (s16)RP_ANGLE(i_this,0x328).y;
        break;
    case 0xB:
        if (RP_U8(i_this,0x74A) == 3) {
            rp_gas(i_this);
        }
        if (RP_S16(i_this,0x792) != 0) {
            if (rp_stopped(i_this)) {
                RP_S16(i_this,0x792) -= 1;
                if (RP_S16(i_this,0x792) <= 0) {
                    rp_animation(i_this, 0x13, 5.0f, 2,
                             1.0f, 8);
                    RP_S16(i_this,0x76E +2*(2)) = (s16)rp_truncate(rp_random_f(300.0f) + 600.0f);
                    RP_S16(i_this,0x792) = 0;
                } else {
                    f32 r = RP_F32(i_this,0x810) - 0.6f;
                    if (r < 0.0f) {
                        r = 0.0f;
                    }
                    RP_F32(i_this,0x810) = r;
                    if (RP_S16(i_this,0x792) > 1) {
                        if (RP_U8(i_this,0x748) >= 8) {
                            rp_sound_a(i_this, 0x4917);
                        } else {
                            rp_sound_a(i_this, 0x4822);
                        }
                    }
                    rp_animation(i_this, 0x14, 0.0f, 0,
                             RP_F32(i_this,0x810), 9);
                }
            }
        } else {
            if (RP_S16(i_this,0x76E +2*(2)) == 0) {
                RP_S16(i_this,0x792) = 3;
                RP_F32(i_this,0x810) = 1.8f;
                rp_animation(i_this, 0x14, 5.0f, 0, 1.8f,
                         9);
                if (RP_U8(i_this,0x748) >= 8) {
                    rp_sound_a(i_this, 0x4917);
                } else {
                    rp_sound_a(i_this, 0x4822);
                }
                rp_particle_tuba(i_this);
            } else if (RP_U32(pPlayer,0x3C0) & 0xC00 /* getFootOnGround */) {
                rp_sound_a(i_this, 0x4823);
            }
        }
        rp_angle_calc(&RP_ANGLE(i_this,0x328).x, -0x7FFF, 1, 0x1000);
        RP_ANGLE(i_this,0x320).y = (s16)RP_ANGLE(i_this,0x328).y;
        break;
    case 0xC:
        if ((RP_U32(i_this,0x9FC)&0x800) && rp_water(i_this) > RP_VEC(i_this,0x314).y &&
            RP_S16(i_this,0x788) == 0) {
            rp_splash(i_this);
        }
        if ((RP_U32(i_this,0x9FC)&0x10) && __builtin_fabsf(RP_F32(i_this,0x370)) > 3.0f) {
            RP_F32(i_this,0x370) = RP_F32(i_this,0x370) * -0.5f;
        }
        if (RP_S16(i_this,0x78C) > 3) {
            return;
        }
        {
            s16 m436 = RP_S16(i_this,0x77E);
            if (!(RP_U32(i_this,0x9FC)&0x20)) {
                RP_F32(i_this,0x370) = RP_F32(i_this,0x370) * 0.99f;
            }
            if (m436 == 0) {
                RP_ANGLE(i_this,0x328).x += 0x1000;
                rp_angle_calc(&RP_ANGLE(i_this,0x328).y, RP_ANGLE(i_this,0x320).y, 2, 0x800);
            } else {
                rp_angle_calc(&RP_ANGLE(i_this,0x328).x, 0, 1, 0x800);
            }
        }
        if ((RP_S16(i_this,0x788) == 0 && !(RP_U32(i_this,0x9FC)&0x20)) ||
            (RP_S16(i_this,0x788) && rp_water(i_this) < RP_VEC(i_this,0x314).y)) {
            return;
        }
        if (RP_U8(i_this,0x748) >= 8) {
            rp_start_shock(i_this);
            rp_sound_a(i_this, 0x593E);
            if (rp_sea(0x100127DC) && RP_S8(i_this,0x326) == 44 &&
                RP_F32(i_this,0xA68) != -1000000000.0f &&
                rp_special((rp_play()+0x12A0), (i_this+0xABCu)) != 4 &&
                !rp_daynight()) {
                rp_event_on(rp_event(), 0x3402 /* UNK_3402 */);
            }
        }
        if (RP_S16(i_this,0x788) != 0) {
            if (RP_U8(i_this,0x748) >= 8) {
                RP_U8(i_this,0xBAC)=0x64;
            } else {
                RP_U8(i_this,0xBAC)=0x32;
            }
            rp_force_unlock(i_this);
            RP_S16(i_this,0x766) = 2;
            RP_S16(i_this,0x768) = 0x14;
            return;
        }
        rp_zero(&RP_F32(i_this,0x370), 1.0f, 4.0f);
        rp_particle_tuba(i_this);
        if (RP_U8(i_this,0x748) >= 8) {
            rp_sound_a(i_this, 0x4917);
        } else {
            rp_sound_a(i_this, 0x4822);
        }
        RP_S16(i_this,0x78C) = 6;
        if (RP_S16(i_this,0x77E) == 0) {
            RP_VEC(i_this,0x33C).y = 20.0f;
            RP_S16(i_this,0x77E) = 1;
            if (RP_U8(i_this,0x74A) == 4) {
                RP_VEC(i_this,0x33C).y = 0.0f;
                RP_F32(i_this,0x370) = 0.0f;
                RP_S16(i_this,0x78C) = 0x1E;
            }
            rp_force_unlock(i_this);
            if (RP_S32(i_this,0x8BC) != 0x17) {
                rp_animation(i_this, 0x17, 5.0f, 2, 1.0f,
                         0xC);
            }
            RP_U32(i_this,0xC00) |= 1u; /* OnCoSetBit */
        } else {
            RP_S16(i_this,0x768) = 0xD;
            if (RP_U8(i_this,0x74A) == 2) {
                RP_S16(i_this,0x76E +2*(5)) = 0x1E;
            }
            if (RP_U8(i_this,0x74C) == 0) {
                rp_copy_vector((u32)&RP_VEC(i_this,0x798),(u32)&(RP_VEC(i_this,0x314)));
            }
            RP_U32(i_this,0x39C) |= 0x00000001;
            RP_S16(i_this,0x76E +2*(0)) = 0x10;
            RP_F32(i_this,0x7FC) = 0.0f;
            RP_F32(i_this,0x370) = 0.0f;
            rp_set_vector(&RP_VEC(i_this,0x33C),0.0f, 0.0f, 0.0f);
            RP_U8(i_this,0x3A1) = (u8)(RP_U8(i_this,0x3A1)-1u); /* Randomizer: swordless throws also count. */
        }
        break;
    case 0xD:
        rp_angle_calc(&RP_ANGLE(i_this,0x328).x, 0, 1, 0x2000);
        if (RP_S16(i_this,0x76E +2*(0)) == 0 && !(RP_U32(i_this,0x2E0)&0x2000u) && RP_S16(i_this,0x78C) == 0) {
            RP_ANGLE(i_this,0x328).x = 0;
            RP_U32(i_this,0x9FC)&=~0x2000u;
            RP_F32(i_this,0x80C) = 30.0f;
            RP_U32(i_this,0xC00) |= 1u;
            rp_animation(i_this, 0x15, 5.0f, 0, 1.0f,
                     0xA);
            RP_S16(i_this,0x76A) = (s16)RP_ANGLE(i_this,0x320).y;
            RP_F32(i_this,0x7FC) = 0.0f;
            RP_F32(i_this,0x370) = 0.0f;
            rp_set_vector(&RP_VEC(i_this,0x33C),0.0f, 0.0f, 0.0f);
            RP_S16(i_this,0x76E +2*(2)) = 0;
            rp_copy_vector((u32)&RP_VEC(i_this,0x7A4),(u32)&(RP_VEC(i_this,0x314)));
            RP_S16(i_this,0x766) = 0;
            RP_S16(i_this,0x768) = 6;
            if (RP_S8(i_this,0x3A1) <= 0) {
                RP_S8(i_this,0x3A1) = 10;
                RP_U8((u32)&WWHD_GAME_DATA(0x101B7FA0,u8),0) = 1;
                // HD: no OnAtSetBit / OnAtHitBit (mode 0x24 sets the collider)
                RP_F32(i_this,0x370) = rp_random_fx(2.0f) + 15.0f;
                RP_S16(i_this,0x76E +2*(0)) = 0x14;
                RP_ANGLE(i_this,0x320).y = (s16)RP_ANGLE(i_this,0x328).y;
                RP_S16(i_this,0x78A) = rp_search_angle(i_this);
                rp_copy_vector((u32)&RP_VEC(i_this,0x970+12*(0)),(u32)&(RP_VEC(i_this,0x314)));
                rp_copy_angle(&RP_ANGLE(i_this,0x988+6*(0)), &RP_ANGLE(i_this,0x320));
                rp_smoke(i_this);
                RP_S16(i_this,0x790) = 0;
                RP_S16(i_this,0x768) = 0x24;
            }
            if (rp_swim_change(i_this)) {
                return;
            }
        }
        break;
    }

    if ((RP_S16(i_this,0x768) == 0xA || RP_S16(i_this,0x768) == 0xB) && !(RP_U32(i_this,0x2E0)&0x2000u)) {
        RP_F32(i_this,0x374) = -3.0f;
        if (!(RP_F32(i_this,0x370) > 0.0f)) { /* GHS: <= is true for NaN */
            RP_F32(i_this,0x80C) = 30.0f;
            RP_S16(i_this,0x76E +2*(0)) = 0;
            if (RP_U8(i_this,0x74A) == 2) {
                RP_S16(i_this,0x76E +2*(5)) = 0x1E;
            }
            if (RP_U8(i_this,0x74A) == 3) {
                rp_follow_end((i_this+0x930));
            }
            RP_S16(i_this,0x768) = 0xD;
            RP_U32(i_this,0x39C) |= 0x00000001;
        } else {
            RP_S16(i_this,0x77E) = 0;
            RP_F32(i_this,0x80C) = 30.0f;
            if (RP_U8(i_this,0x74A) == 3) {
                rp_follow_end((i_this+0x930));
            }
            rp_animation(i_this, 0x14, 5.0f, 2, 1.0f,
                     9);
            RP_ANGLE(i_this,0x328).x = -0x8000;
            RP_F32(i_this,0x370) = 20.0f;
            RP_VEC(i_this,0x33C).y = 20.0f;
            RP_S16(i_this,0x768) = 0xC;
        }
    }
}

/* inlined into daKb_Execute */
