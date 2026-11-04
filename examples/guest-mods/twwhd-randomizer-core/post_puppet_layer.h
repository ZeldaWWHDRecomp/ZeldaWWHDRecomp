/* Source: public CC0 a874e1a9, d/actor/d_a_bgn_exec.cpp.
 * Complete 0207E7B4 consumer; upstream MIT remove_cutscenes.asm changes only
 * the final stage layer (9 -> -1). Native move remains an out-of-line call. */
#pragma once
#include "puppet_camera_rope.h"
/* 0207E7B4 */
// HD: demo_camera and bomb_splash_check are inlined; move is an out-of-line call.
// The transformation and defeat no longer go through
// l_HIO.m024 (m02B5 is set directly; the final event bit is set when the explosion starts); the
// first cut scene shows thunder particles around Link at five fixed frames and zooms in from frame
// REG18_S(6) + 450, Link looks up at frame 313 (GameCube 320); the cut scenes count mCA60 down; two
// debug reports.
WWHD_REPLACE(0x0207E7B4,s32,randomizer_puppet_execute,(u32 i_this)) {
    u32 actor = i_this;
    if (RB_U8(i_this,0x3D4) == 0xFF)
        return 1;
    (void)wwhd_dComIfG_getGameInfo();
    rb_lighting(rb_env(), 3, ((u32)&WWHD_GAME_DATA(0x10461CD0,u8)),
                     ((u32)&WWHD_GAME_DATA(0x10461E58,u8)));
    rb_search(0x0207D3AC, i_this);
    WWHD_GAME_DATA(0x10461A38,u32) = (u32)(rb_search(0x0207B22C, i_this));
    WWHD_GAME_DATA(0x10461A3C,u32) = (u32)(rb_search(0x0207B27C, i_this));
    if (RB_U8((u32)&WWHD_GAME_DATA(0x10461CDC,u8),0x23) != 0)
        RB_S8(actor,0x3A1) = RB_U8((u32)&WWHD_GAME_DATA(0x10461CDC,u8),0x23);
    if (RB_S8(i_this,0x15764) == 1) {
        RB_S8(i_this,0x15764) = 0;
        RB_S8(i_this,0x3D5) = 2;
    }
    RB_S16(i_this,0x14EB2) = (s16)(RB_S16(i_this,0x14EB2) + 1);
    for (s32 i = 0; i < 5; i++) {
        if (RB_S16(i_this,0x14F18+2*(i)) != 0)
            RB_S16(i_this,0x14F18+2*(i)) = (s16)(RB_S16(i_this,0x14F18+2*(i)) - 1);
    }
    if (RB_S16(i_this,0x14F22) != 0)
        RB_S16(i_this,0x14F22) = (s16)(RB_S16(i_this,0x14F22) - 1);
    if (RB_S16(i_this,0x14F24) != 0)
        RB_S16(i_this,0x14F24) = (s16)(RB_S16(i_this,0x14F24) - 1);
    if (RB_S16(i_this,0x4BC) != 0)
        RB_S16(i_this,0x4BC) = (s16)(RB_S16(i_this,0x4BC) - 1);
    if (RB_S16(i_this,0x4C0) != 0)
        RB_S16(i_this,0x4C0) = (s16)(RB_S16(i_this,0x4C0) - 1);
    if (RB_S16(i_this,0x4BE) != 0) {
        s16 t = (s16)(RB_S16(i_this,0x4BE) + 1);
        if (t > 100)
            RB_S16(i_this,0x4BE) = 0;
        else
            RB_S16(i_this,0x4BE) = t;
    }
    rb_move(i_this);

    /* demo_camera (inline) */
    u32 player=RB_PTR(wwhd_dComIfG_getGameInfo(),0x5B2C);
    s8 camId=RB_S8(wwhd_dComIfG_getGameInfo(),0x5B30);
    u32 cam=RB_PTR(wwhd_dComIfG_getGameInfo(),0x5AF8+0x34*(s32)camId)+0x248;
    u32 pl=player;
    rb_vector local_44;
    rb_vector local_50;
    rb_vector local_5c;
    s32 accepted = 1;
    /* The native signed-byte mode cannot reach public-source case 150. */
    switch ((s32)RB_S8(i_this,0x151CE)) {
    case 0:
        break;
    case 1:
    case 2:
        if (RB_S8(i_this,0x151CE) == 1) {
            if (!(RB_U16(actor,0xF8)==2)) {
                accepted = 0;
                break;
            }
            RB_S8(i_this,0x151CE) = (s8)(RB_S8(i_this,0x151CE) + 1);
            rb_camera_stop(cam);
            rb_camera_trim(cam, 2);
            RB_S16(i_this,0x151D0) = 0;
            volatile rb_words *src = RB_WORD_PTR(&RB_VEC(i_this,0x14480));
            volatile rb_words *dst = RB_WORD_PTR(&RB_VEC(i_this,0x151EC));
            u32 cy = (u32)src->y;
            u32 cx = (u32)src->x;
            RB_F32(i_this,0x1520C) = 55.0f;
            dst->x = cx;
            u32 cz = (u32)src->z;
            dst->y = cy;
            dst->z = cz;
            RB_F32(i_this,0x15204) = 0.0f;
            RB_U32(player,0x428) = 0; /* changeOriginalDemo */
            RB_U16(player,0x420) = 3;
            rb_search(0x0207CF04, i_this);
            RB_S8(i_this,0x15750) = 0;
            RB_S16(i_this,0x151CC) = 0xB3;
        } else if (RB_S16(i_this,0x151CC) > 1) {
            RB_S16(i_this,0x151CC) = (s16)(RB_S16(i_this,0x151CC) - 1);
        }
        {
            rb_rotate_y_start(rb_matrix(), RB_ANGLE(i_this,0x1448C).y);
            local_44.y = rb_add(rb_reg_f(0,1), 100.0f);
            local_44.x = rb_add(rb_reg_f(0,0), 200.0f);
            local_44.z = rb_add(rb_reg_f(0,2), 2000.0f);
            rb_position(&local_44, &local_50);
            volatile rb_words *e = RB_WORD_PTR(&RB_VEC(i_this,0x151D4));
            e->x = (u32)RB_WORD_PTR(&local_50)->x;
            e->y = (u32)RB_WORD_PTR(&local_50)->y;
            e->z = (u32)RB_WORD_PTR(&local_50)->z;
            f32 step = RB_S16(i_this,0x151D0) > 300 ? 0.1f : 0.5f;
            rb_calc(&RB_VEC(i_this,0x151EC).x, RB_VEC(i_this,0x14480).x, 0.1f, 300.0f);
            rb_calc(&RB_VEC(i_this,0x151EC).y,
                          rb_add(rb_add(RB_VEC(i_this,0x14480).y, 100.0f), rb_reg_f(0,3)),
                          step, 400.0f);
            rb_calc(&RB_VEC(i_this,0x151EC).z, RB_VEC(i_this,0x14480).z, 0.1f, 300.0f);
            local_44.x = rb_reg_f(0,4);
            local_44.y = RB_VEC(pl,0x314).y;
            local_44.z = rb_add(rb_reg_f(0,5), 1500.0f);
            rb_position(&local_44, &local_50);
            rb_bgn_setPlayerPosAndAngle(player, &local_50, (s16)(RB_ANGLE(i_this,0x1448C).y + 0x8000));
            if (RB_S16(i_this,0x151D0) == (s16)(rb_reg_s(0,5) + 220))
                RB_U32(player,0x430) = 0x1D;
            if (RB_S16(i_this,0x151D0) == (s16)(rb_reg_s(0,6) + 313))
                RB_U32(player,0x430) = 0x1A;
            s16 n = RB_S16(i_this,0x151D0);
            if (n == 0xEB || n == 0xFA || n == 0x109 || n == 0x119 || n == 0x129) {
                /* HD: thunder around Link (a function-local static scale, initialised once) */
                if (WWHD_GAME_DATA(0x10461A54,u32) == 0) {
                    WWHD_GAME_DATA(0x10461A54,u32) = 1;
                    RB_VEC((u32)&WWHD_GAME_DATA(0x10461CC4,u8),0).x = 0.4f;
                    RB_VEC((u32)&WWHD_GAME_DATA(0x10461CC4,u8),0).y = 0.4f;
                    RB_VEC((u32)&WWHD_GAME_DATA(0x10461CC4,u8),0).z = 0.4f;
                }
                rb_particle(0x40, &RB_VEC(pl,0x314), 0, &RB_VEC((u32)&WWHD_GAME_DATA(0x10461CC4,u8),0));
            }
            if (RB_S16(i_this,0x151D0) >= (s16)(rb_reg_s(18, 6) + 450)) {
                rb_calc(&RB_F32(i_this,0x1520C), rb_add(rb_reg_f(18, 11), 35.0f), 0.02f,
                              rb_add(rb_reg_f(18, 12), 0.1f));
            }
        }
        break;
    case 3: {
        rb_calc(&RB_VEC(i_this,0x151EC).x, RB_VEC(WWHD_GAME_DATA(0x10461A38,u32),0x314).x, 0.1f, 100.0f);
        rb_calc(&RB_VEC(i_this,0x151EC).y,
                      rb_add(RB_VEC(WWHD_GAME_DATA(0x10461A38,u32),0x314).y, rb_reg_f(0,13)), 0.1f, 200.0f);
        rb_calc(&RB_VEC(i_this,0x151EC).z, RB_VEC(WWHD_GAME_DATA(0x10461A38,u32),0x314).z, 0.1f, 100.0f);
        if (RB_S16(i_this,0x151D0) == (s16)(rb_reg_s(0,7) + 90))
            RB_F32(i_this,0x15208) = rb_add(rb_reg_f(0,18), 30.0f);
        if (RB_S16(i_this,0x151D0) == (s16)(rb_reg_s(0,7) + 100)) {
            const u32 vt=RB_PTR(player,0xB4);
            RB_U32(player,0x430) =
                0x18;                                   /* changeDemoMode(DEMO_SURPRISED_e) */
            ((void(*)(u32,u32))RB_PTR(vt,0xE4))(player,29); /* voiceStart(29) */
        }
        if (RB_S16(i_this,0x151D0) > (s16)(rb_reg_s(0,8) + 110)) {
            rb_calc(&RB_F32(i_this,0x1520C), rb_add(rb_reg_f(0,11), 35.0f), 0.1f,
                          rb_add(rb_reg_f(0,12), 0.5f));
            if (RB_S16(i_this,0x151D0) == 130 || RB_S16(i_this,0x151D0) == 180)
                rb_sound(0x5978, 0, 0,
                               rb_reverb(RB_S8(actor,0x326)));
            if (RB_S16(i_this,0x151D0) == 152)
                rb_sound(0x5979, 0, 0,
                               rb_reverb(RB_S8(actor,0x326)));
        }
        rb_angle_calc(&RB_S16(i_this,0x151CC), 1, 1, 1);
        if (RB_S16(i_this,0x151D0) > (s16)(rb_reg_s(0,8) + 200)) {
            rb_bgm(0x8000005A);
            rb_bgn_demo_end(i_this, cam);
        }
        break;
    }
    case 10:
    case 11:
    case 12: {
        if (RB_S8(i_this,0x151CE) == 10) {
            if (!(RB_U16(actor,0xF8)==2)) {
                accepted = 0;
                break;
            }
            RB_S8(i_this,0x151CE) = (s8)(RB_S8(i_this,0x151CE) + 1);
            rb_camera_stop(cam);
            rb_camera_trim(cam, 2);
            RB_S16(i_this,0x151D0) = 0;
            RB_F32(i_this,0x1520C) = 55.0f;
            RB_F32(i_this,0x15204) = 0.0f;
            volatile rb_words *src = RB_WORD_PTR(&RB_VEC(WWHD_GAME_DATA(0x10461A38,u32),0x314));
            volatile rb_words *dst = RB_WORD_PTR(&RB_VEC(i_this,0x151EC));
            dst->x = (u32)src->x;
            dst->y = (u32)src->y;
            dst->z = (u32)src->z;
            RB_U16(player,0x420) =
                3; /* changeOriginalDemo */
            RB_U32(player,0x428) = 0;
            rb_search(0x0207CF04, i_this);
            RB_U8(WWHD_GAME_DATA(0x10461A38,u32),0x33BC) = 0;
            RB_VEC(i_this,0x151E0).x = 200.0f;
            RB_VEC(i_this,0x151E0).y = 2100.0f;
            RB_S16(i_this,0x151CC) = 0xB3;
            RB_VEC(i_this,0x151E0).z = 2500.0f;
        } else if (RB_S16(i_this,0x151CC) > 1) {
            RB_S16(i_this,0x151CC) = (s16)(RB_S16(i_this,0x151CC) - 1);
        }
        rb_calc(&RB_VEC(i_this,0x151E0).x, -1800.0f, 0.05f, rb_mul(2000.0f, RB_F32(i_this,0x15204)));
        rb_calc(&RB_VEC(i_this,0x151E0).y, 1100.0f, 0.05f, rb_mul(1000.0f, RB_F32(i_this,0x15204)));
        rb_calc(&RB_VEC(i_this,0x151E0).z, 1000.0f, 0.05f, rb_mul(1500.0f, RB_F32(i_this,0x15204)));
        rb_calc(&RB_F32(i_this,0x15204), 0.01f, 1.0f, 0.0001f);
        rb_rotate_y_start(rb_matrix(), RB_S16(WWHD_GAME_DATA(0x10461A38,u32),0x32A));
        rb_position(&RB_VEC(i_this,0x151E0), &local_50);
        volatile rb_words *e = RB_WORD_PTR(&RB_VEC(i_this,0x151D4));
        e->x = (u32)RB_WORD_PTR(&local_50)->x;
        e->y = (u32)RB_WORD_PTR(&local_50)->y;
        e->z = (u32)RB_WORD_PTR(&local_50)->z;
        if (RB_S8(i_this,0x151CE) == 11) {
            rb_calc(&RB_VEC(i_this,0x151EC).x, RB_VEC(WWHD_GAME_DATA(0x10461A38,u32),0x314).x, 0.1f, 300.0f);
            rb_calc(&RB_VEC(i_this,0x151EC).y,
                          rb_add(RB_VEC(WWHD_GAME_DATA(0x10461A38,u32),0x314).y, rb_reg_f(0,13)), 0.1f,
                          300.0f);
            rb_calc(&RB_VEC(i_this,0x151EC).z, RB_VEC(WWHD_GAME_DATA(0x10461A38,u32),0x314).z, 0.1f, 300.0f);
            local_44.x = rb_reg_f(0,4);
            local_44.y = RB_VEC(pl,0x314).y;
            local_44.z = rb_add(rb_reg_f(0,5), -2000.0f);
            rb_position(&local_44, &local_50);
            const u32 vt=RB_PTR(player,0xB4);
            ((void(*)(u32,u32,s16))RB_PTR(vt,0x114))(player,(u32)&local_50,
                           (s16)RB_S16(WWHD_GAME_DATA(0x10461A38,u32),0x32A));
        } else {
            rb_calc(&RB_VEC(i_this,0x151EC).x, RB_VEC(WWHD_GAME_DATA(0x10461A3C,u32),0x314).x, 0.1f, 300.0f);
            rb_calc(&RB_VEC(i_this,0x151EC).y,
                          rb_add(rb_add(RB_VEC(WWHD_GAME_DATA(0x10461A3C,u32),0x314).y, 50.0f),
                                          rb_reg_f(8, 16)),
                          0.1f, 300.0f);
            rb_calc(&RB_VEC(i_this,0x151EC).z, RB_VEC(WWHD_GAME_DATA(0x10461A3C,u32),0x314).z, 0.1f, 300.0f);
            rb_calc(&RB_F32(i_this,0x1520C), rb_add(rb_reg_f(0,17), 30.0f),
                          rb_add(rb_reg_f(0,18), 0.05f), rb_add(rb_reg_f(0,19), 0.2f));
            rb_angle_calc(&RB_S16(i_this,0x151CC), 1, 1, 2);
        }
        if (RB_S16(i_this,0x151D0) > (s16)(rb_reg_s(0,9) + 370))
            rb_bgn_demo_end(i_this, cam);
        break;
    }
    case 20:
    case 21: {
        if (RB_S8(i_this,0x151CE) == 20) {
            if (!(RB_U16(actor,0xF8)==2)) {
                accepted = 0;
                break;
            }
            RB_S8(i_this,0x151CE) = (s8)(RB_S8(i_this,0x151CE) + 1);
            rb_camera_stop(cam);
            rb_camera_trim(cam, 2);
            RB_S16(i_this,0x151D0) = 0;
            RB_F32(i_this,0x1520C) = 55.0f;
            RB_F32(i_this,0x15204) = 0.0f;
            volatile rb_words *src = RB_WORD_PTR(&RB_VEC(WWHD_GAME_DATA(0x10461A3C,u32),0x314));
            volatile rb_words *dst = RB_WORD_PTR(&RB_VEC(i_this,0x151EC));
            dst->x = (u32)src->x;
            dst->y = (u32)src->y;
            dst->z = (u32)src->z;
            RB_U16(player,0x420) =
                3; /* changeOriginalDemo */
            RB_U32(player,0x428) = 0;
            RB_S16(i_this,0x151CC) = 0xB4;
            rb_search(0x0207CF04, i_this);
            rb_search(0x0207CF58, i_this);
        }
        rb_angle_calc(&RB_S16(i_this,0x151CC), 1, 1, 1);
        RB_VEC(i_this,0x151D4).x = 500.0f;
        RB_VEC(i_this,0x151D4).y = rb_add(rb_reg_f(0,3), 500.0f);
        RB_VEC(i_this,0x151D4).z = rb_add(rb_reg_f(0,4), 2000.0f);
        rb_calc(&RB_VEC(i_this,0x151EC).x, RB_VEC(WWHD_GAME_DATA(0x10461A3C,u32),0x314).x, 0.1f, 300.0f);
        rb_calc(&RB_VEC(i_this,0x151EC).y,
                      rb_add(RB_VEC(WWHD_GAME_DATA(0x10461A3C,u32),0x314).y, rb_reg_f(0,13)), 0.1f, 300.0f);
        rb_calc(&RB_VEC(i_this,0x151EC).z, RB_VEC(WWHD_GAME_DATA(0x10461A3C,u32),0x314).z, 0.1f, 300.0f);
        local_44.x = rb_reg_f(0,4);
        local_44.y = RB_VEC(pl,0x314).y;
        local_44.z = rb_add(rb_reg_f(0,5), -1000.0f);
        {
            const u32 vt=RB_PTR(player,0xB4);
            ((void(*)(u32,u32,s16))RB_PTR(vt,0x114))(player,(u32)&local_44,
                           (s16)RB_S16(WWHD_GAME_DATA(0x10461A38,u32),0x32A));
        }
        if (RB_S16(i_this,0x151D0) == 260)
            rb_monster_sound(0x4972, 0, 0,
                            rb_reverb(RB_S8(actor,0x326)));
        if (RB_S16(i_this,0x151D0) >= 370) {
            f32 s = rb_add(rb_reg_f(0,4), 10.0f);
            local_5c.x = s;
            local_5c.z = s;
            local_5c.y = s;
            rb_particle(0x13, &RB_VEC(WWHD_GAME_DATA(0x10461A3C,u32),0x314),
                                  0, &local_5c);
            rb_particle(0x16, &RB_VEC(WWHD_GAME_DATA(0x10461A3C,u32),0x314),
                                  0, &local_5c);
            rb_sound(0x5984, 0, 0,
                           rb_reverb(RB_S8(actor,0x326)));
            rb_vector up;
            u32 vib = (wwhd_dComIfG_getGameInfo()+0x599C);
            up.x = 0.0f;
            up.y = 1.0f;
            up.z = 0.0f;
            rb_shock(vib, rb_reg_s(0,2) + 8, -0x21, &up);
            RB_S8(i_this,0x3D5) = 3;
            rb_event_on(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,0x3F10);
            s8 mode = (s8)(RB_S8(i_this,0x151CE) + 1);
            RB_S8(i_this,0x14C7C) = 1;
            RB_S16(i_this,0x151CC) = 0xB4;
            RB_S8(i_this,0x151CE) = mode;
        }
        break;
    }
    case 22:
        if (RB_S16(i_this,0x151D0) >= 390)
            rb_last_himo_control(i_this);
        if (RB_S16(i_this,0x151D0) >= (s16)(rb_reg_s(8, 7) + 500))
            RB_S8(i_this,0x15764) = 2;
        break;
    case 150:
        rb_bgn_demo_end(i_this, cam);
        break;
    default:
        break;
    }
    if (accepted) {
        if (RB_S8(i_this,0x151CE) != 0) {
            s16 n = RB_S16(i_this,0x151D0);
            f32 ca9c = RB_F32(i_this,0x15208);
            f32 fx = rb_mul(rb_sine(n * 0x3300), ca9c);
            f32 fy = rb_mul(rb_cosine(n * 0x3000), ca9c);
            rb_vector camera_center;
            rb_vector camera_eye;
            camera_center.x = rb_add(RB_VEC(i_this,0x151EC).x, fx);
            camera_eye.x = rb_add(RB_VEC(i_this,0x151D4).x, fx);
            camera_eye.y = rb_add(RB_VEC(i_this,0x151D4).y, fy);
            camera_center.y = rb_add(RB_VEC(i_this,0x151EC).y, fy);
            s16 bank = (s16)rb_truncate(
                rb_mul(rb_mul(rb_cosine(RB_S16(i_this,0x14EB2) * 0x1C00), ca9c), 7.5f));
            f32 fovy = RB_F32(i_this,0x1520C);
            camera_eye.z = RB_VEC(i_this,0x151D4).z;
            camera_center.z = RB_VEC(i_this,0x151EC).z;
            rb_camera(cam, &camera_center, &camera_eye, bank, fovy);
            rb_zero(&RB_F32(i_this,0x15208), 1.0f, rb_add(rb_reg_f(0,16), 1.0f));
            rb_report(30, 420, 0x10008AF4, RB_S16(i_this,0x151D0));
            rb_report(30, 440, 0x10008B08, RB_S8(i_this,0x151CE));
            RB_S16(i_this,0x151D0) = (s16)(RB_S16(i_this,0x151D0) + 1);
        }
    } else {
        rb_order(actor, 2, 0xFFFF, 0);
        RB_U16(actor,0xFA)|=2;
    }

    s8 spawn = RB_S8(i_this,0x15750);
    if (spawn != 0) {
        RB_S8(i_this,0x15750) = (s8)(spawn - 1);
        if (rb_keese_count(i_this) < RB_S16((u32)&WWHD_GAME_DATA(0x10461CDC,u8),0x176)) {
            /* Native bug: x is written twice; the third raw scratch word is untouched.
             * Pass storage by address only, never read an uninitialized C float. */
            unsigned int local_28[3];
            RB_F32((u32)local_28,0) = rb_random_fx(2500.0f);
            RB_F32((u32)local_28,4) = rb_add_wide(rb_random_f(500.0f),3500.0f);
            RB_F32((u32)local_28,0) = rb_random_fx(2500.0f); /* GameCube bug kept: z is never set */
            rb_create(0xD7, 0xFFFF0003, (u32)local_28, RB_S8(actor,0x326),
                          0, 0, -1, 0);
        }
    }
    if (RB_S8(i_this,0x15764) == 2) {
        RB_S8(i_this,0x15764) = 0;
        rb_next_stage(0x10008C40, 4, 0, -1, 0.0f, 0, 1, 0);
    }
    return 1;
}
