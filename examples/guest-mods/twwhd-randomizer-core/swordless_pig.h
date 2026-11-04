/* MIT randomizer make_game_nonlinear.asm 02190EF4 / 0219264C.
 * Complete public CC0 daKb_Execute02190CE0: swordless hits retain damage,
 * swordless throws decrement health. All other movement branches remain. */
#pragma once
#include "pig_helpers.h"
#include "pig_attack.h"
#include "pig_normal.h"
#include "pig_carry.h"
#include "pig_swim.h"
/* fadds with the PowerPC NaN rule (frA's NaN wins) */
static inline f32 rp_kb_fadds_ppc(f32 a, f32 b) {
    if (a != a)
        return a;
    if (b != b)
        return b;
    return a + b;
}
static inline f32 rp_clamp10(f32 v) {
    if (v > 10.0f) {
        v = 10.0f;
    } else if (v < -10.0f) {
        v = -10.0f;
    }
    return v;
}

/* The classic family is still guarded at preparation while its other
 * consumers are staged. Missing metadata therefore retains stock caps. */
static inline f32 rp_collision_component(f32 value) {
#if defined(WWHD_RANDOMIZER_CLASSIC_MODE) && WWHD_RANDOMIZER_CLASSIC_MODE
    return value;
#else
    return rp_clamp10(value);
#endif
}

/* 02190CE0 */
WWHD_REPLACE(0x02190CE0,s32,randomizer_pig_execute,(u32 i_this)) {
    u32 actor = i_this;

    // HD: the esa demo cool-down (m426[4]) does not run while an event runs (play+0x5292)
    (void)rp_play();
    if (RP_U8(rp_play(),0x5292) != 0) {
        RP_S16(i_this,0x76E +2*(4)) += 1;
    }
    for (int i = 0; i < 8; i++) {
        if (RP_S16(i_this,0x76E +2*(i)) != 0) {
            RP_S16(i_this,0x76E +2*(i)) -= 1;
        }
    }
    RP_S16(i_this,0x78E) += 1;
    RP_U32(i_this,0xBD4) &= ~1u; /* HD: ClrAtSet every frame */

    switch (RP_S16(i_this,0x766)) {
    case 0:
        rp_pl_attack_hit_check(i_this);
        rp_normal_move(i_this);
        if (RP_S16(i_this,0x76E +2*(7)) == 0 && (RP_U32(i_this,0x9FC)&0x20)) {
            if (RP_U8(i_this,0x748) < 8 && rp_sea(0x100129C4) &&
                RP_S8(actor,0x326) == 44) {
                if (RP_F32(i_this,0xA68) != -1000000000.0f &&
                    rp_special((rp_play()+0x12A0), (i_this+0xABCu)) == 4) {
                    u8 temp = 1 << (RP_U8(i_this,0x748) & 3);
                    u8 temp2 = rp_event_reg(rp_event(), 0xBFFF);
                    rp_event_set_reg(rp_event(), 0xBFFF, temp2 | temp);
                    RP_U8(i_this,0x74D) = temp;
                } else {
                    RP_U8(i_this,0x74D) = 0;
                }
            }
            if (RP_U8(i_this,0x748) >= 8 && rp_sea(0x100129C4) &&
                RP_S8(actor,0x326) == 44 &&
                RP_F32(i_this,0xA68) != -1000000000.0f &&
                rp_special((rp_play()+0x12A0), (i_this+0xABCu)) != 4) {
                if (RP_U8(i_this,0x748) >= 8) {
                    RP_U8(i_this,0xBAC)=100;
                } else {
                    RP_U8(i_this,0xBAC)=0x32;
                }
                rp_event_on(rp_event(), 0x3402 /* UNK_3402 */);
            }
        }
        break;
    case 1:
        rp_carry_move(i_this);
        break;
    case 2:
        rp_swim_move(i_this);
        break;
    case 3:
        rp_attack(i_this);
        break;
    case 4:
        rp_food(i_this);
        break;
    }

    volatile rp_vector *ccMoveP = &RP_VEC(i_this,0xB98); /* GetCCMoveP: null-preserving */
    rp_eye(i_this);
    if ((u32)(ccMoveP) != 0) {
        // HD: only x/z; classic mode skips both caps without writing the push.
        f32 x = rp_collision_component(ccMoveP->x);
        f32 z = rp_collision_component(ccMoveP->z);
        /* with two NaNs the first operand's payload survives (the position is later copied with
         * integer moves, so the payload is compared) */
        RP_VEC(actor,0x314).x = rp_kb_fadds_ppc(RP_VEC(actor,0x314).x, x);
        RP_VEC(actor,0x314).z = rp_kb_fadds_ppc(RP_VEC(actor,0x314).z, z);
    }

    if (RP_S16(i_this,0x766) != 3) {
        f32 temp = RP_F32(i_this,0x81C) + 1.0f;
        if (RP_S16(i_this,0x78C)) {
            RP_S16(i_this,0x78C) -= 1;
            if (RP_S16(i_this,0x78C) >= 3) {
                temp = RP_F32(i_this,0x81C) + 0.5625f;
            } else {
                temp = RP_F32(i_this,0x81C) + 1.125f;
            }
        }
        rp_calc(&RP_VEC(i_this,0x830).y, temp, 0.5f, 0.5f);
    }

    volatile rp_vector *attnPos = &RP_VEC(actor,0x390); /* attention_info.position follows eight distance bytes */
    {
        f32 py = RP_VEC(actor,0x314).y, pz = RP_VEC(actor,0x314).z, px = RP_VEC(actor,0x314).x;
        attnPos->y = py;
        attnPos->z = pz;
        attnPos->x = px;
        attnPos->y = py + rp_fma(RP_F32(i_this,0x81C), 50.0f, 50.0f);
        // HD: the eye position has its own height
        RP_VEC(actor,0x37C).x = px;
        RP_VEC(actor,0x37C).y = py;
        RP_VEC(actor,0x37C).z = pz;
        f32 r = rp_reg_f(10, 2);
        RP_VEC(actor,0x37C).y = py + rp_fma(RP_F32(i_this,0x81C), r + 50.0f, r + 30.0f);
    }
    rp_vector temp;
    rp_copy_vector((u32)&temp,(u32)&(RP_VEC(actor,0x314)));
    rp_speed(i_this);
    rp_background(i_this);
    {
        rp_vector c;
        c.x = (f32)RP_VEC(actor,0x314).x;
        f32 y = RP_VEC(actor,0x314).y;
        c.y = y;
        c.z = (f32)RP_VEC(actor,0x314).z;
        c.y = y + (rp_fma(RP_F32(i_this,0x81C), 35.0f, 25.0f) + rp_reg_f(10, 4));
        rp_sphere_center(i_this+0xCEC,(u32)&c);
    }
    rp_sphere_radius(i_this+0xCEC,rp_fma(RP_F32(i_this,0x81C), 25.0f, 30.0f));
    rp_collision_set(rp_play()+0x26A4,i_this+0xBD4);
    rp_mass((rp_play()+0x4EF8), (i_this+0xBD4), 3);

    if (RP_F32(i_this,0xA68) != -1000000000.0f) {
        u32 mtrlSndId;
        if ((RP_U32(i_this,0x9FC)&0x20)) {
            mtrlSndId = rp_material((rp_play()+0x12A0), (i_this+0xABCu));
        } else {
            mtrlSndId = 0;
        }
        s8 roomNo = RP_S8(actor,0x326);
        s32 reverb = rp_reverb(roomNo);
        rp_morf_play(RP_PTR(i_this,0x744),actor+0x37C,mtrlSndId,(s8)reverb);
    } else {
        rp_morf_play(RP_PTR(i_this,0x744),0,0,0);
    }

    if ((RP_U32(i_this,0x9FC)&0x20)) {
        rp_ground_get(actor, &RP_ANGLE(i_this,0x83C));
        if (RP_VEC(i_this,0x798).x == 0.0f && RP_VEC(i_this,0x798).y == 0.0f && RP_VEC(i_this,0x798).z == 0.0f) {
            rp_copy_vector((u32)&RP_VEC(i_this,0x798),(u32)&(RP_VEC(actor,0x314)));
        }
        if (RP_S16(i_this,0x766) != 1 && RP_F32(i_this,0xA68) != -1000000000.0f) {
            RP_U8(i_this,0x74C) = 0;
            if (rp_attribute((rp_play()+0x12A0), (i_this+0xABCu)) ==
                0x13 /* dBgS_Attr_WATER_e */) {
                RP_U8(i_this,0x74C) = 1;
            }
        }
    }

    rp_draw(i_this);

    if ((RP_U32(actor,0x2E0)&0x2000u)) {
        rp_copy_vector((u32)&RP_VEC(actor,0x314),(u32)&(temp));
    }

    if (RP_S16(i_this,0x766) == 3 && RP_S16(i_this,0x768) >= 0x24) {
        // HD: no speed.y update here (rp_attack applies gravity)
        rp_vector temp3;
        rp_vec_sub(&RP_VEC(actor,0x300), &temp3, &RP_VEC(actor,0x314));
        f64 temp2 = rp_sqrt(rp_magnitude(&temp3));
        if (temp2 < 1.0f) {
            RP_S16(i_this,0x790) += 1;
        }
        if (RP_S16(i_this,0x790) > 10) {
            RP_U8((u32)&WWHD_GAME_DATA(0x101B7FA0,u8),0) = 0;
            rp_off_bit(actor+0x39C,4);
            // HD: no mSph.OffAtSetBit()
            RP_S16(i_this,0x766) = 0;
            RP_S16(i_this,0x768) = 2;
            rp_smoke_end((i_this+0x8F0));
        }
    }

    f32 temp2 = 0.0f;
    if (RP_S16(i_this,0x76E +2*(5)) || (0x21 <= RP_S16(i_this,0x768) && RP_S16(i_this,0x768) <= 0x24)) {
        s16 a = RP_S16(i_this,0x794) + (s16)rp_truncate(rp_reg_f(8, 10) + 1000.0f);
        RP_S16(i_this,0x794) = a;
        f32 s = rp_sine_native((u32)&WWHD_GAME_DATA(0x104A44F8,u8), a);
        temp2 = rp_fnmsub(__builtin_fabsf(s), rp_reg_f(8, 0xB) + 0.5f, 1.0f);
    }
    rp_calc(&RP_F32(i_this,0x7F8), temp2, 1.0f, 0.1f);
    return 1;
}
