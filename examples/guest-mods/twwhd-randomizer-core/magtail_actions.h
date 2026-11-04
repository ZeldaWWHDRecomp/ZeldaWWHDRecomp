/* Complete CC0 a874e1a9 d_a_mt_exec.cpp source expansion. */
#pragma once
#include "magtail_damage.h"
/* rmt_body_control1 (inlined into rmt_move): the trail, the segment models and colliders */
/* inlined into daMt_Execute */
static inline void rmt_action(u32 actor, u32 player) {
    RMT8(actor,0xD20) = 0;
    _Alignas(4) u8 linChk_storage[0x6C];
    const u32 linChk=(u32)linChk_storage;
    rmt_line_init(linChk,0);
    rmt_vector plPos;
    plPos.x = RMTV(player,0x314).x;
    plPos.y = RMTV(player,0x314).y + 20.0f;
    plPos.z = RMTV(player,0x314).z;
    rmt_vector myPos;
    myPos.x = RMTV(actor,0x314).x;
    myPos.y = RMTV(actor,0x314).y + 30.0f;
    myPos.z = RMTV(actor,0x314).z;
    rmt_line_set(linChk, (u32)&myPos, (u32)&plPos, actor);
    RMT8(actor,0x1A15) = (u8)rmt_line_cross(rmt_background(), linChk);

    switch ((u8)RMT8(actor,0x570)) {
    case 0:
        rmt_move(actor);
        break;
    case 1:
        rmt_fight(actor);
        break;
    case 2:
        /* rolled up */
        rmt_roll(actor);
        rmt_body3(actor);
        if (RMT16(actor,0x582) > 100) {
            if (RMT8(actor,0x1A16) != 0) {
                RMT16(actor,0x582) = 0x47;
            }
        } else {
            rmt_wall_check(actor);
        }
        rmt_cc_move(actor);
        if (RMT16(actor,0x582) == 0x32) {
            /* unroll */
            RMT8(actor,0x400) = 0;
            RMT8(actor,0x1A17) = 2;
            RMT8(actor,0x570) = 1;
            RMT8(actor,0x571) = 0;
            RMTS8(actor,0x3A1) = 8;
            RMT32((actor+0xF64+0x12C*(0)),0x18) |= 1;
            RMT16(actor,0x572) = RMT16(rmt_hio(),0x10);
            RMT16(actor,0x5AA) = 0;
            rmt_animation(actor, 10, 20.0f, 2, 1.0f, 0);
            rmt_min(actor, -200.0f, -200.0f, -200.0f);
            rmt_max(actor, 200.0f, 200.0f, 200.0f);
            RMT32((actor+0xF64+0x12C*(0)),0) = (RMT32((actor+0xF64+0x12C*(0)),0) & ~0xAu) | 4;
        }
        break;
    case 3:
        /* destroyed */
        RMT16(actor,0x57C) = 5;
        rmt_attention_flags(actor, 0);
        rmt_body4(actor);
        if (RMT16(actor,0x576) == 1) {
            RMTS8(actor,0x1DD4) = 1;
            rmt_delete(actor);
            rmt_on_actor_wrapper(RMTU16(actor,0x2D8), RMTS8(actor,0x2FE));
        }
        break;
    case 6:
        /* carried by the player */
        if (RMT8(player,0x3AC) == 0x10) {
            rmt_rotate_y_start(rmt_matrix(), RMTA(actor,0x328).y);
            rmt_vector v;
            v.x = 0.0f;
            v.y = 0.0f;
            v.z = rmt_reg_f(0,14) + -50.0f;
            rmt_vector t;
            rmt_position((u32)&v, (u32)&t);
            rmt_calc((u32)&RMTV(actor,0x314).x, RMTV(player,0x314).x + t.x, 0.5f, 50.0f);
            rmt_calc((u32)&RMTV(actor,0x314).z, RMTV(player,0x314).z + t.z, 0.5f, 50.0f);
        }
        RMT8(actor,0x400) = 1;
        RMT16(actor,0x580) = 3;
        RMT16(actor,0x57C) = 5;
        rmt_body5(actor);
        {
            f32 vy = RMTV(actor,0x33C).y;
            RMTV(actor,0x314).y = RMTV(actor,0x314).y + vy;
            RMTV(actor,0x33C).y = vy + RMTF(actor,0x374);
        }
        rmt_bg_check(actor);
        if (RMT16(actor,0x572) != 0) {
            RMT16(actor,0x58A) = 0;
            rmt_zero((u32)&RMTF(actor,0x590), 0.05f, rmt_reg_f(0,12) + 0.02f);
            if (RMT16(actor,0x572) == 1) {
                rmt_explode(actor);
            }
        } else if (RMT8(player,0x3AC) != 0x10) {
            /* released */
            RMT8(actor,0x400) = 0;
            RMT8(actor,0x570) = 1;
            RMT8(actor,0x571) = 0;
            RMT32((actor+0xF64+0x12C*(0)),0x18) |= 1;
            RMT16(actor,0x572) = RMT16(rmt_hio(),0x10);
            RMT16(actor,0x5AA) = 0;
            rmt_animation(actor, 10, 20.0f, 2, 1.0f, 0);
            RMT32((actor+0xF64+0x12C*(0)),0) = (RMT32((actor+0xF64+0x12C*(0)),0) & ~0xAu) | 4;
        } else {
            RMTF(actor,0x590) = rmt_reg_f(0,13) + 0.2f;
            if (rmt_hit((actor+0x18C4))) {
                RMTS8(actor,0x3A1) = 0;
                RMT16(actor,0x572) = rmt_reg_s(0,3) + 0x28;
                RMTF(actor,0x590) = rmt_reg_f(0,13) + 1.5f;
                rmt_sound(actor, 0x2828, 0);
                rmt_monster_out(actor, 0x4803, 0);
                _Alignas(4) unsigned int info_storage[7];
    const u32 atInfo=(u32)info_storage;
                RMTS32(atInfo,0x18) = 0;
                RMTPTR(atInfo,0x0) = (u32)(rmt_hit_object((actor+0x18C4)));
                rmt_attack_power(atInfo);
                rmt_enemy_damage();
                rmt_bgm(RMTS32(atInfo,0x18));
                RMT8(rmt_static(0x101EACB7),0) = (u8)(rmt_reg_s(0,7) + 6);
                rmt_particle(0x10,actor+0x314,0,0);
                rmt_angle ang;
                rmt_vector scale;
                ang.x = 0;
                ang.z = 0;
                scale.z = 2.0f;
                scale.x = 2.0f;
                scale.y = 2.0f;
                ang.y = rmt_actor_angle(actor, rmt_player());
                rmt_particle(0xD,actor+0x314,(u32)&ang,(u32)&scale);
                rmt_vector pos;
                pos.x = RMTV(actor,0x314).x;
                pos.y = RMTV(actor,0x314).y;
                pos.z = RMTV(actor,0x314).z;
                rmt_flush((u32)&pos, 1);
                rmt_animation(actor, 8, 5.0f, 0, 1.0f, 0);
            }
        }
        break;
    }

    if (RMT16(actor,0x582) > 60) {
        rmt_zero((u32)&RMTF(actor,0x1A0C), 1.0f, 0.05f);
    } else {
        rmt_calc((u32)&RMTF(actor,0x1A0C), 1.0f, 1.0f, 0.1f);
    }
    rmt_line_finish(linChk);
}

/* keep the head out of the background between the old and the new position */
/* inlined into daMt_Execute */
static inline void rmt_bg_line_check(u32 actor) {
    f32 h = 40.0f;
    _Alignas(4) u8 chkUp_storage[0x6C];
    const u32 chkUp=(u32)chkUp_storage;
    rmt_line_init(chkUp,1);
    rmt_vector start;
    rmt_copy_vector((u32)&start,(u32)&RMTV(actor,0x300));
    rmt_vector end;
    end.x = RMTV(actor,0x300).x;
    end.y = RMTV(actor,0x300).y + 40.0f;
    end.z = RMTV(actor,0x300).z;
    rmt_line_set(chkUp, (u32)&start, (u32)&end, actor);
    s32 roof = rmt_line_cross(rmt_background(), chkUp);
    f32 oy = RMTV(actor,0x300).y;
    if (roof) {
        f32 t = RMTF(chkUp,0x34) - 1.0f - oy;
        h = t >= 0.0f ? t : 0.0f;
    }
    rmt_vector oldPos;
    oldPos.x = RMTV(actor,0x300).x;
    oldPos.y = oy + h;
    oldPos.z = RMTV(actor,0x300).z;
    rmt_vector newPos;
    newPos.x = RMTV(actor,0x314).x;
    newPos.y = RMTV(actor,0x314).y + h;
    newPos.z = RMTV(actor,0x314).z;

    _Alignas(4) u8 chkFwd_storage[0x6C];
    const u32 chkFwd=(u32)chkFwd_storage;
    rmt_line_init(chkFwd,1);
    rmt_line_set(chkFwd, (u32)&oldPos, (u32)&newPos, actor);
    s32 fwd = rmt_line_cross(rmt_background(), chkFwd);
    _Alignas(4) u8 chkBack_storage[0x6C];
    const u32 chkBack=(u32)chkBack_storage;
    rmt_line_init(chkBack,1);
    rmt_line_set(chkBack, (u32)&newPos, (u32)&oldPos, actor);
    s32 back = rmt_line_cross(rmt_background(), chkBack);
    if (fwd && !back) {
        /* went through a wall: back to the wall, pushed out along its normal */
        rmt_copy_vector((u32)&RMTV(actor,0x314),(u32)&RMTV(chkFwd,0x30));
        u32 poly = (chkFwd+0x14);
        u32 pla = rmt_plane(rmt_background(),RMTU16(poly,2),RMTU16(poly,0));
        if (pla != 0) {
            RMTV(actor,0x314).x = RMTV(actor,0x314).x + RMTV(pla,0).x;
            RMTV(actor,0x314).y = RMTV(actor,0x314).y + RMTV(pla,0).y;
            RMTV(actor,0x314).z = RMTV(actor,0x314).z + RMTV(pla,0).z;
        }
    }
    rmt_line_finish(chkBack);
    rmt_line_finish(chkFwd);
    rmt_line_finish(chkUp);
}
