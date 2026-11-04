/* Complete CC0 a874e1a9 d_a_mt_exec.cpp source expansion. */
#pragma once
#include "magtail_helpers.h"
/* the collision push of this frame (dCcD_Stts::GetCCMoveP), applied to the position */
static inline void rmt_cc_move(u32 actor) {
    if ((u32)((actor+0xF28)) != 0) {
        RMTV(actor,0x314).x = RMTV(actor,0x314).x + RMTV(actor,0xF28).x;
        RMTV(actor,0x314).z = RMTV(actor,0x314).z + RMTV(actor,0xF28).z;
    }
}

/* the knock-back: speed from the hit angle */
/* inlined into daMt_Execute */
static inline void rmt_knockback(u32 actor, s16 angleY) {
    rmt_rotate_y_start(rmt_matrix(), angleY);
    rmt_vector v;
    f32 k = RMTF(rmt_hio(),0x4C);
    v.x = 0.0f;
    v.y = 40.0f * k;
    v.z = -20.0f * k;
    rmt_position((u32)&v, (u32)&RMTV(actor,0x33C));
}

/* the point mTargetPos behind the head: mTargetPos = current.pos + rotY(shape_angle.y) * (0, y, z) */
/* inlined into daMt_Execute */
static inline void rmt_set_back_pos(u32 actor, s16 angleY, f32 y, f32 z) {
    rmt_rotate_y_start(rmt_matrix(), angleY);
    rmt_vector v;
    v.x = 0.0f;
    v.y = y;
    v.z = z;
    rmt_position((u32)&v, (u32)&RMTV(actor,0x598));
    rmt_vector_add_native((u32)&RMTV(actor,0x598), (u32)&RMTV(actor,0x314), (u32)&RMTV(actor,0x598));
}

/* rmt_eye_tex_anm (inlined) */
static inline void rmt_eye_tex_anm(u32 actor) {
    if (RMT16(actor,0x580) != 0) {
        RMT16(actor,0x580) = RMT16(actor,0x580) - 1;
    } else {
        RMT16(actor,0x580) = (s16)rmt_truncate(rmt_random_add(100.0f,50.0f));
        if (RMT8(actor,0x461) == 0) {
            rmt_texture(actor, 0);
        }
    }
    if (RMT8(actor,0x461) != 0) {
        if (RMT8(actor,0x460) < RMT8(actor,0x462)) {
            RMT8(actor,0x460) = RMT8(actor,0x460) + 1;
        } else {
            RMT8(actor,0x461) = 0;
        }
    }
}

/* inlined into daMt_Execute */
static inline void rmt_body_control1(u32 actor) {
    rmt_angle_calc((u32)&RMTA(actor,0x328).x, RMTA(actor,0x320).x, 2, 0x800);
    rmt_angle_calc((u32)&RMTA(actor,0x328).y, RMTA(actor,0x320).y, 2, 0x800);
    rmt_angle_calc((u32)&RMTA(actor,0x328).z, RMTA(actor,0x320).z, 4, 0x400);

    rmt_copy_vector((u32)&RMTV(actor,0x810+12*(RMTS32(actor,0xD10))),(u32)&RMTV(actor,0x314));
    u32 tr=(u32)&RMTA(actor,0xB10+6*(RMTS32(actor,0xD10)));
    RMTA(tr,0).x = RMTA(actor,0x328).x;
    RMTA(tr,0).y = RMTA(actor,0x328).y;
    RMTA(tr,0).z = RMTA(actor,0x328).z;
    RMT16(actor,0xC90+2*(RMTS32(actor,0xD10))) = RMT16(actor,0x584);

    for (int i = 0; i < 8; i++) {
        s32 ofs = RMT8(actor,0xD1C) != 0 ? rmt_fast_trail(i) : rmt_trail(i);
        u32 k = (u32)(RMTS32(actor,0xD10) + ofs) & 0x3F;
        u32 model=RMTPTR(RMTPTR(actor,0x3E0+4*(i)),0x90);
        /* J3DModel::setBaseScale(scale) */

        const unsigned int sx=RMT32(actor,0x330),sz=RMT32(actor,0x338),sy=RMT32(actor,0x334);
        RMT32(model,0xC0)=sy;
        RMT32(model,0xBC)=sx;
        RMT32(model,0xC4)=sz;
        u32 pos=(u32)&RMTV(actor,0x810+12*(k));
        rmt_translate(rmt_stack(), RMTV(pos,0).x, RMTV(pos,0).y, RMTV(pos,0).z);
        u32 ang=(u32)&RMTA(actor,0xB10+6*(k));
        rmt_rotate_y(rmt_stack(), RMTA(ang,0).y);
        rmt_rotate_x(rmt_stack(), RMTA(ang,0).x);
        rmt_rotate_z(rmt_stack(), RMTA(ang,0).z);
        rmt_rotate_y(rmt_stack(), RMT16(actor,0xC90+2*(k)));
        if (i == 0) {
            f32 s = RMTF(rmt_hio(),0x1C);
            rmt_stack_scale(s, s, s);
        } else {
            f32 s = RMTF(actor,0x71C+4*(i));
            rmt_stack_scale(s, s * RMTF(actor,0x73C+4*(i)), 1.0f);
        }
        rmt_stack_translate(0.0f, 0.0f, RMTF(actor,0x58C));
        rmt_copy_model(model, rmt_stack());
        if (i == 0) {
            rmt_vector v;
            v.x = 0.0f;
            v.y = 0.0f;
            v.z = rmt_reg_f(0,9) + 30.0f;
            rmt_matrix_vector(rmt_stack(), (u32)&v, (u32)&RMTV(actor,0x37C));
            rmt_sphere_center((actor+0x18C4)+0x118,(u32)&RMTV(actor,0x37C));
            rmt_sphere_radius((actor+0x18C4)+0x118,RMTF(rmt_hio(),0x44));
            rmt_collision_wrapper((actor+0x18C4));
            rmt_vector v2;
            v2.x = 0.0f;
            v2.y = 0.0f;
            v2.z = rmt_reg_f(6,9) + 100.0f;
            rmt_vector headPos;
            rmt_matrix_vector(rmt_stack(), (u32)&v2, (u32)&headPos);
            rmt_sphere_center((actor+0xF64+0x12C*(0))+0x118,(u32)&headPos);
            rmt_sphere_radius((actor+0xF64+0x12C*(0))+0x118,50.0f);
            rmt_collision_wrapper((actor+0xF64+0x12C*(0)));
        } else {
            rmt_sphere_center((actor+0xF64+0x12C*(i))+0x118,pos);
            if (RMT16(actor,0x57C) != 0) {
                rmt_sphere_radius((actor+0xF64+0x12C*(i))+0x118,-200.0f);
            } else {
                rmt_sphere_radius((actor+0xF64+0x12C*(i))+0x118,RMTF(rmt_hio(),0x48));
            }
            rmt_collision_wrapper((actor+0xF64+0x12C*(i)));
        }
        if (RMT8(actor,0xD1D) != 0) {
            rmt_copy_vector((u32)&RMTV(actor,0x5BC+12*(i)),pos);
            u32 dst=(u32)&RMTA(actor,0x67C+6*(i));
            RMTA(dst,0).x = RMTA(ang,0).x;
            RMTA(dst,0).y = RMTA(ang,0).y;
            RMTA(dst,0).z = RMTA(ang,0).z;
        }
    }
    if (RMT16(actor,0x5AA) == 0) {
        RMTS32(actor,0xD10) = (RMTS32(actor,0xD10) + 1) & 0x3F;
    } else {
        RMTS32(actor,0xD10) = RMTS32(actor,0xD10) & 0x3F;
    }
}

/* rmt_move (inlined): crawl along walls/ceilings, steered by eight line checks */
/* inlined into daMt_Execute */
static inline void rmt_move(u32 actor) {
    wwhd_dComIfG_getGameInfo(); /* HD: result unused */
    u32 hit = 0;
    {
        _Alignas(4) u8 linChk_storage[0x6C];
    const u32 linChk=(u32)linChk_storage;
        rmt_line_init(linChk,0);
        rmt_rotate_y_start(rmt_matrix(), RMTA(actor,0x320).y);
        rmt_rotate_x(rmt_matrix(), RMTA(actor,0x320).x);
        rmt_rotate_z(rmt_matrix(), RMTA(actor,0x320).z);
        rmt_vector offs;
        offs.x = 0.0f;
        offs.z = 0.0f;
        offs.y = 50.0f;
        rmt_vector start;
        rmt_position((u32)&offs, (u32)&start);
        rmt_vector sum;
        rmt_vector_add((u32)&start, (u32)&sum, (u32)&RMTV(actor,0x314));
        rmt_copy_vector((u32)&start,(u32)&sum);

        int n = rmt_abs(RMTA(actor,0x320).x) < 0x1000 ? 6 : 4;
        rmt_vector cross[6];
        rmt_vector ends[6];
        for (int i = 0; i < n; i++) {
            offs.z = rmt_check_z(i);
            offs.y = rmt_check_y(i);
            offs.x = rmt_check_x(i);
            rmt_position((u32)&offs, (u32)&ends[i]);
            rmt_vector_add_native((u32)&ends[i], (u32)&RMTV(actor,0x314), (u32)&ends[i]);
            rmt_line_set(linChk, (u32)&start, (u32)&ends[i], actor);
            if (rmt_line_cross(rmt_background(), linChk)) {
                rmt_copy_vector((u32)&cross[i],linChk+0x30);
                hit |= rmt_check_bit(i);
            }
        }

        if ((hit & 3) == 3) {
            /* on a wall: follow it */
            rmt_vector d;
            rmt_vector_subtract((u32)&cross[0], (u32)&d, (u32)&cross[1]);
            offs.y = RMTF(rmt_hio(),0x18);
            rmt_vector dd;
            rmt_copy_vector((u32)&dd,(u32)&d);
            offs.z = 0.0f;
            offs.x = 0.0f;
            rmt_vector up;
            rmt_position((u32)&offs, (u32)&up);
            if (RMT16(actor,0x5AA) == 0) {
                rmt_calc((u32)&RMTV(actor,0x314).x, rmt_fma(dd.x, 0.5f, cross[1].x) + up.x, 1.0f, 1.0f);
                rmt_calc((u32)&RMTV(actor,0x314).y, rmt_fma(dd.y, 0.5f, cross[1].y) + up.y, 1.0f, 1.0f);
                rmt_calc((u32)&RMTV(actor,0x314).z, rmt_fma(dd.z, 0.5f, cross[1].z) + up.z, 1.0f, 1.0f);
            }
            f32 dz = dd.z;
            f32 dx = dd.x;
            RMTA(actor,0x320).x = (s16)-rmt_atan(dd.y, rmt_sqrt(rmt_fma(dx, dx, dz * dz)));
            dx = dd.x;
            dz = dd.z;
            if (__builtin_fabsf(dx) > 0.1f || __builtin_fabsf(dz) > 0.1f) {
                RMT16(actor,0x5A8) = rmt_atan(dx, dz);
            }
            s16 diff = (s16)(RMT16(actor,0x5A8) - RMTA(actor,0x320).y);
            if (diff < 0) diff = (s16)-diff;
            if ((u16)diff > 0x4000) {
                RMTA(actor,0x320).x = (s16)(-0x8000 - RMTA(actor,0x320).x);
            }
            RMT16(actor,0x5AE) = 0x17;
        } else if (hit & 1) {
            /* keep the pitch */
        } else if (RMT16(actor,0x5AE) != 0) {
            RMT16(actor,0x5AE) = RMT16(actor,0x5AE) - 1;
        } else {
            RMTA(actor,0x320).x = (s16)(RMTA(actor,0x320).x + (rmt_reg_s(0,2) + 0x800));
        }

        if (rmt_abs(RMTA(actor,0x320).x) < 0x1000) {
            /* on the floor */
            if ((hit & 0x30) == 0x30) {
                rmt_vector d;
                rmt_vector_subtract((u32)&cross[4], (u32)&d, (u32)&cross[5]);
                f32 dx = d.x, dz = d.z;
                rmt_vector dd;
                rmt_copy_vector((u32)&dd,(u32)&d);
                RMTA(actor,0x320).y = (s16)(rmt_atan(dx, dz) + 0x4000);
            } else if (RMT8(actor,0x3D0) >= 10) {
                RMTA(actor,0x320).y = (s16)(RMTA(actor,0x320).y + RMT16(actor,0x5A4));
            } else {
                s16 target;
                s32 turn = 1;
                if ((s8)RMT8(actor,0x3D8) != 0) {
                    /* follow the path */
                    rmt_vector d;
                    rmt_vector_subtract((u32)&RMTV(actor,0x598), (u32)&d, (u32)&RMTV(actor,0x314));
                    f32 dx = d.x, dz = d.z;
                    rmt_vector dd;
                    rmt_copy_vector((u32)&dd,(u32)&d);
                    target = rmt_atan(dx, dz);
                    dz = dd.z;
                    dx = dd.x;
                    f32 dzz = dz * dz;
                    RMT16(actor,0x5A4) = 0x800;
                    if (rmt_sqrt(rmt_fma(dx, dx, dzz)) < 100.0f) {
                        u32 ppd = RMTPTR(actor,0x3DC);
                        s8 no = (s8)(RMT8(actor,0x3D9) + RMT8(actor,0x3DA));
                        RMT8(actor,0x3D9) = no;
                        if (no >= (s8)(u8)RMT8(ppd,1)) {
                            if ((u8)RMT8(ppd,5) & 1) {
                                no = 0;
                                RMT8(actor,0x3D9) = no;
                            } else {
                                RMT8(actor,0x3DA) = -1;
                                no = (s8)((u16)RMTU16(ppd,0) - 2);
                                RMT8(actor,0x3D9) = no;
                            }
                        } else if (no < 0) {
                            RMT8(actor,0x3DA) = 1;
                            no = 1;
                            RMT8(actor,0x3D9) = no;
                        }
                        u32 pnt=RMTPTR(ppd,8)+(s32)no*0x10+4;
                        RMTV(actor,0x598).x = RMTV(pnt,0).x;
                        RMTV(actor,0x598).y = RMTV(pnt,0).y;
                        RMTV(actor,0x598).z = RMTV(pnt,0).z;
                    }
                } else {
                    target = rmt_actor_angle(actor, rmt_player());
                    rmt_angle_calc((u32)&RMT16(actor,0x5A4), (s16)(rmt_reg_s(0,4) + 0x400), 1, 0x10);
                }
                if (RMT8(actor,0x3D1) == 1) {
                    RMTA(actor,0x320).y = target;
                } else {
                    rmt_angle_calc((u32)&RMTA(actor,0x320).y, target, 0x10, RMT16(actor,0x5A4));
                }
                (void)turn;
                if (RMT8(actor,0x3D0) < 10 && RMT8(actor,0x1A15) == 0 &&
                    rmt_distance(actor, rmt_player()) < RMTF(rmt_hio(),0x24)) {
                    /* the player is near: drop to the floor once the body is flat */
                    int k;
                    for (k = 0; k < 8; k++) {
                        if (rmt_abs(RMTA(actor,0xB10+6*(k * 8)).x) > 0x1000) break;
                    }
                    if (k == 8) {
                        RMT8(actor,0xD1D) = 1;
                    }
                }
            }
            rmt_angle_calc((u32)&RMTA(actor,0x320).z, 0, 2, 0x400);
        } else {
            /* on a wall or the ceiling */
            if ((hit & 0xC) == 0xC) {
                rmt_vector d;
                rmt_vector_subtract((u32)&cross[2], (u32)&d, (u32)&cross[3]);
                rmt_vector dd;
                rmt_copy_vector((u32)&dd,(u32)&d);
                rmt_rotate_x_start(rmt_matrix(), (s16)-RMTA(actor,0x320).x);
                rmt_rotate_y(rmt_matrix(), (s16)-RMTA(actor,0x320).y);
                rmt_vector t;
                rmt_position((u32)&dd, (u32)&t);
                f32 tz = t.z, tx = t.x;
                RMTA(actor,0x320).z = rmt_atan(t.y, rmt_sqrt(rmt_fma(tx, tx, tz * tz)));
            }
            if (RMT8(actor,0x3D0) < 10) {
                RMT16(actor,0x5A4) = 0;
            }
        }

        if (RMT16(actor,0x5AA) == 0) {
            s16 ph = RMT16(actor,0x586);
            s16 reg = rmt_reg_s(0,0);
            f32 amp = rmt_fma(rmt_sin(ph * 100), 1000.0f, 3500.0f);
            rmt_angle_calc((u32)&RMT16(actor,0x584), (s16)rmt_truncate(rmt_sin(ph * (reg + 2000)) * amp), 4, 0x400);
            if (RMT8(actor,0xD1C) != 0) {
                RMTF(actor,0x370) = 10.0f;
            } else {
                RMTF(actor,0x370) = 5.0f;
            }
        } else {
            RMTF(actor,0x370) = 0.0f;
            rmt_zero((u32)&RMTF(actor,0x370), 1.0f, 5.0f);
        }
        rmt_rotate_y_start(rmt_matrix(), RMTA(actor,0x320).y);
        rmt_rotate_x(rmt_matrix(), RMTA(actor,0x320).x);
        rmt_rotate_y(rmt_matrix(), RMT16(actor,0x584));
        offs.x = 0.0f;
        offs.y = 0.0f;
        offs.z = RMTF(actor,0x370);
        rmt_position((u32)&offs, (u32)&RMTV(actor,0x33C));
        rmt_pos_move(actor, 0);

        if ((hit & 0xF) == 0) {
            s16 c = (s16)(RMT16(actor,0x588) + 1);
            RMT16(actor,0x588) = c;
            if (c >= 10) {
                /* lost the wall: fall and fight */
                RMT8(actor,0x570) = 1;
                RMT8(actor,0x571) = 0x11;
            }
        } else {
            RMT16(actor,0x588) = 0;
        }
        rmt_line_finish(linChk);
    }

    rmt_body_control1(actor);

    if (RMT8(actor,0xD1D) != 0) {
        RMT8(actor,0xD1D) = 0;
        RMT8(actor,0x570) = 1;
        RMT8(actor,0x571) = 0;
        RMT16(actor,0x572) = RMT16(rmt_hio(),0x10);
        RMT16(actor,0x5AA) = 0;
        rmt_animation(actor, 10, 20.0f, 2, 1.0f, 0);
    }
    rmt_calc((u32)&RMTF(actor,0x58C), -10.0f, 1.0f, 1.0f);

    if ((u32)((actor+0xF28)) != 0) {
        RMTV(actor,0x314).x = RMTV(actor,0x314).x + RMTV(actor,0xF28).x;
        RMTV(actor,0x314).z = RMTV(actor,0x314).z + RMTV(actor,0xF28).z;
        for (int i = 0; i < 64; i++) {
            RMTV(actor,0x810+12*(i)).x = RMTV(actor,0x810+12*(i)).x + RMTV(actor,0xF28).x;
            RMTV(actor,0x810+12*(i)).z = RMTV(actor,0x810+12*(i)).z + RMTV(actor,0xF28).z;
        }
    }

    if (RMT16(actor,0x574) == 0) {
        RMT16(actor,0x574) = (s16)rmt_truncate(rmt_random_add(100.0f,150.0f));
        if (RMT8(rmt_hio(),0x05) == 0 && rmt_abs(RMTA(actor,0x320).x) < 0x1000) {
            RMT16(actor,0x5AA) = (s16)rmt_truncate(rmt_random_add(50.0f,50.0f) + rmt_reg_f(0,8));
        }
    }
}

/* the fall of rmt_fight: integrate the speed */
static inline void rmt_fall(u32 actor) {
    RMTV(actor,0x314).x = RMTV(actor,0x314).x + RMTV(actor,0x33C).x;
    RMTV(actor,0x314).y = RMTV(actor,0x314).y + RMTV(actor,0x33C).y;
    RMTV(actor,0x314).z = RMTV(actor,0x314).z + RMTV(actor,0x33C).z;
    RMT16(actor,0x57C) = 5;
    f32 vy = RMTV(actor,0x33C).y + RMTF(actor,0x374);
    if (vy < -100.0f) {
        RMTV(actor,0x33C).y = -100.0f;
    } else {
        RMTV(actor,0x33C).y = vy;
    }
}
