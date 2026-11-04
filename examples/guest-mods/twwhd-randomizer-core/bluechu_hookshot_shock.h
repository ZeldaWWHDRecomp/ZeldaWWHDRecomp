/* Complete CC0 a874e1a9 d_a_cc_act.cpp::body_atari_check0210CE9C.
 * MIT fix_vanilla_bugs.asm0210D8C8 omits only the hookshot At-bit clear;
 * electric collision stays consistent with the particles on blue/yellow ChuChu.
 * All other damage/light/elemental/grapple branches and native callbacks remain. */
#pragma once
#include "bluechu_hit_helpers.h"
static inline s32 rbc_critical(u8 t) {
    switch (t) {
    case 0x6:
    case 0x7:
    case 0x8:
    case 0x9:
    case 0xa:
    case 0xc:
    case 0xe:
    case 0x5:
    case 0xf:
    case 0x10:
    case 0x15:
    case 0x17:
    case 0x19:
    case 0x1a:
    case 0x1b:
    case 0x1e:
    case 0x1f:
        return 1;
    }
    return 0;
}

WWHD_REPLACE(0x0210CE9C,s32,randomizer_bluechu_body_hit,(u32 actor)) {
    u32 player = rbc_player();

    rbc_status_move(actor+0x7A4);

    if (rbc_hit(actor+0x7C4)) {
        u32 pcVar6=rbc_hit_object(actor+0x7C4);
        u32 pdVar7=rbc_hit_actor(actor+0x858);
        if (pcVar6 == 0) {
            return 0;
        }

        _Alignas(4) unsigned int hit_storage[3];const u32 sp30=(u32)hit_storage;
        /* Native copies hit X/Z to scratch before loading/storing hit Y. */
        const unsigned int hx=RBC32(actor,0x890),hz=RBC32(actor,0x898);
        RBC32(sp30,0)=hx;RBC32(sp30,8)=hz;
        RBC32(sp30,4)=RBC32(actor,0x894);

        if ((RBC8(actor,0x40C) != 0) && (pdVar7 != 0) && (rbc_player() != pdVar7)) {
            RBC8(actor,0x40C) = 2;
            RBCS8(actor,0x3A1) = 0;
            rbc_particle(0x10,sp30,0,0);
            rbc_vector scale={2.0f,2.0f,2.0f};const u32 sp24=(u32)&scale;
            rbc_particle(0xF,sp30,player+0x328,sp24);
            RBC8(actor,0x3FC) = 3;
            RBC8(actor,0x3FD) = 0x28;
            return 1;
        }

        RBCF(actor,0x3C0) = 1000.0f; /* setBtNowFrame */

        _Alignas(4) unsigned int attack_storage[7];const u32 sp3C=(u32)attack_storage;
        RBCPTR(sp3C,0x14) = 0;
        s32 bVar4 = 0;
        RBC8(actor,0x3FE) = 0;

        u32 atType = RBC32(pcVar6,0x10);
        if (atType & 0x800000) {
            if (RBC8(actor,0x3F9) == 3) {
                if (RBC8(actor,0x3FC) != 6) {
                    RBC8(actor,0x79C)=0xFE;
                    RBC8(actor,0x3FC) = 6;
                    RBC8(actor,0x3FD) = 0x50;
                    return 1;
                }
            } else {
                return 0;
            }
        }

        switch (atType) {
        case 0x8000000:
            if (RBCS8(actor,0x3A9) > 0) {
                s8 sVar9 = RBCS8(actor,0x3A1);
                u8 bVar1 = RBC8(actor,0x3A8);
                RBCS8(actor,0x3A1) = 10;
                RBCPTR(sp3C,0) = rbc_hit_object(actor+0x7C4);
                rbc_attack_check(actor, sp3C);
                if (RBC8(actor,0x3F9) == 2) {
                    RBC8(actor,0x3A8) = bVar1;
                }
                RBCS8(actor,0x3A1) = sVar9;
            }

            if (RBC8(actor,0x3FD) == 0x15) {
                rbc_sound_nn(actor, 0x2834, 0x36);
                RBCF(actor,0x374) = -(rbc_reg_f(12, 19) + 3.0f);
                RBCF(actor,0x42C) = 0.0f;
                RBC8(actor,0x400) = 0;
                return 0;
            }
            bVar4 = 1;
            RBC8(actor,0x3FE) = 9;
            break;

        case 0x400:
        case 0x2:
        case 0x800:
        case 0x4000000:
        case 0x10000000:
            if (RBC8(actor,0x3FF) != 0) {
                return 0;
            }

            if (rbc_critical(RBC8(player,0x3AC))) {
                RBC8(actor,0x3FE) = 1;
            }

            if (RBC8(actor,0x3F9) == 3) {
                rbc_sound(actor, 0x2803, 0x36);
                bVar4 = 1;
                RBC8(actor,0x3FE) = 0;
            }
            break;

        case 0x200000:
            bVar4 = 1;
            RBC8(actor,0x3FE) = 3;
            RBCA(actor,0x320).y =
                rbc_atan(RBCV(actor,0x314).x - RBCF(sp30,0), RBCV(actor,0x314).z - RBCF(sp30,8));
            if ((pdVar7 != 0) && (pdVar7 == player)) {
                RBCA(actor,0x320).y = RBCA(player,0x328).y;
                if (RBC8(actor,0x3FD) == 0x15) {
                    RBCF(actor,0x370) = rbc_reg_f(12, 18) + 20.0f;
                    return 0;
                }
            }
            break;

        case 0x40:
            RBC8(actor,0x3FE) = 4;
            bVar4 = 1;
            if (RBC8(actor,0x3FD) == 0x15) {
                rbc_sound(actor, 0x2833, 0x36);
                RBCF(actor,0x374) = -(rbc_reg_f(12, 19) + 3.0f);
                RBCF(actor,0x42C) = 0.0f;
                RBC8(actor,0x400) = 0;
                return 0;
            }
            break;

        case 0x80:
        case 0x2000:
        case 0x1000000:
            if (RBC8(actor,0x3F9) == 3) {
                rbc_sound(actor, 0x2833, 0x36);
                bVar4 = 1;
                RBC8(actor,0x3FE) = 0;
            }
            break;

        case 0x8000:
            if (RBC8(actor,0x3F9) == 3) {
                rbc_sound(actor, 0x2834, 0x36);
            }
            RBC8(actor,0x3FE) = 7;
            bVar4 = 1;
            if (RBC8(actor,0x3FD) == 0x15) {
                RBCV(actor,0x33C).z = 0.0f;
                RBCV(actor,0x33C).x = 0.0f;
                RBCV(actor,0x33C).y = 0.0f;
                RBCF(actor,0x370) = 0.0f;
                RBCF(actor,0x374) = -(rbc_reg_f(12, 19) + 3.0f);
                RBCF(actor,0x42C) = 0.0f;
                RBC8(actor,0x400) = 0;
                return 0;
            }
            break;

        case 0x10000:
            if (RBC8(actor,0x3F9) == 3) {
                rbc_sound(actor, 0x2855, 0x36);
            }

            RBC8(actor,0x3FE) = 1;

            {
                u8 cut = RBC8(player,0x3AC);
                if (cut == 0x12 || cut == 0x13) {
                    RBC8(actor,0x3FE) = 6;
                }
            }

            if (RBC8(actor,0x3F9) == 3) {
                bVar4 = 1;
                RBC8(actor,0x3FE) = 0;
            }
            break;

        case 0x200:
        case 0x40000:
            if (RBC8(actor,0x3F9) == 3) {
                bVar4 = 1;
                RBC8(actor,0x3FE) = 0;
                rbc_sound(actor, 0x2834, 0x36);
            } else {
                RBC16(actor,0xCE0) = 100;
                RBC8(actor,0x3FE) = 8;
            }
            break;

        case 0x100000:
            RBC8(actor,0x92A) = 1;
            RBCF(actor,0xAD0) = 0.5f;
            RBCF(actor,0x92C) = 80.0f;
            rbc_sound(actor, 0x5865, 0);
            rbc_monster(actor, 0x4863, 0);
            RBC32(actor,0x39C) = 0;
            rbc_electric_end(actor);
            RBC8(actor,0x3FE) = 8;
            break;

        case 0x80000:
            if (RBC8(actor,0x3F9) == 3) {
                bVar4 = 1;
                RBC8(actor,0x3FE) = 0;
                rbc_sound(actor, 0x2834, 0x36);
            } else {
                RBC16(actor,0x928) = rbc_reg_s(8, 2) + 200;
                if (rbc_reg_s(8, 2) != 0) {
                    /* HD: after the store GHS treats the actor as non-null */
                    RBCS8(actor,0x3A1) = 100;
                    rbc_sound_nn(actor, 0x5865, 0);
                    rbc_monster_nn(actor, 0x4863, 0);
                } else {
                    rbc_sound(actor, 0x5865, 0);
                    rbc_monster(actor, 0x4863, 0);
                }
                rbc_fire_remove((actor+0xCDC));
                rbc_electric_end(actor);
                bVar4 = 1;
                RBC8(actor,0x3FE) = 8;
            }
            break;

        default:
            if (RBC8(actor,0x3F9) == 3) {
                bVar4 = 1;
                RBC8(actor,0x3FE) = 0;
                rbc_sound(actor, 0x2834, 0x36);
            }
            break;
        }

        if (!bVar4) {
            RBCPTR(sp3C,0) = rbc_hit_object(actor+0x7C4);
            rbc_attack_check(actor, sp3C);

            if ((RBC8(actor,0x3FE) == 1) || (RBC8(actor,0x3FE) == 6) || (RBCS8(actor,0x3A1) <= 0)) {
                rbc_particle(0x10,sp30,0,0);
                rbc_vector scale={2.0f,2.0f,2.0f};const u32 sp18=(u32)&scale;
                rbc_particle(0xF,sp30,player+0x328,sp18);
            } else {
                rbc_particle(0xD,sp30,player+0x328,0);
            }
        }

        if ((RBC8(actor,0x3FE) == 7) && (RBC8(actor,0x3FC) != 3)) {
            const unsigned int ignored=RBC32(actor,0x7C4)&~1u;
            (void)ignored; /* Upstream0210D8C8 NOP preserves the preceding load. */
            RBC16(actor,0x418)=0;
            RBC16(actor,0x450)=0;
            RBCF(actor,0x438)=0.0f;
            RBC16(actor,0x41A)=0;
            RBCF(actor,0x434)=0.0f;
            RBC8(actor,0x3FC)=0;
            RBC16(actor,0x41C)=0;
            RBC8(actor,0x3FD) = 10;
            return 1;
        }

        if ((RBC8(actor,0x3F9) == 3) && (RBC8(actor,0x3FE) != 4 && (RBC8(actor,0x3FE) != 3)) &&
            (RBC8(actor,0x3FE) != 9)) {
            RBC8(actor,0x3FE) = 0;
        }

        if (RBC8(actor,0x3FE) == 6) {
            RBC8(actor,0x3FC) = 4;
            RBC8(actor,0x3FD) = 0x34;
        } else {
            RBC8(actor,0x3FC) = 3;
            RBC8(actor,0x3FD) = 0x28;
        }
        return 1;
    }
    return 0;
}
