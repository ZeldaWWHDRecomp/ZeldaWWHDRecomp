/* Complete CC0 a874e1a9 d_a_mt_exec.cpp source expansion. */
#pragma once
#include "magtail_fight.h"
/* the line of sight to the player, then the action of the state */
/* rmt_damage_check (inlined) */
/* inlined into daMt_Execute */
static inline void rmt_damage_check(u32 actor) {
    wwhd_dComIfG_getGameInfo(); /* HD: result unused */
    _Alignas(4) unsigned int info_storage[7];
    const u32 atInfo=(u32)info_storage;
    int hit = 0;
    RMTPTR(atInfo,0x14) = 0;
    rmt_status_move(actor+0xF44);

    int i = RMT8(actor,0xD20) == 1 ? 2 : 0;
    for (; i < 8; i++) {
        if (!rmt_hit((actor+0xF64+0x12C*(i))) || RMT16(actor,0x57C) != 0) {
            continue;
        }
        RMTPTR(atInfo,0x0) = (u32)(rmt_hit_object((actor+0xF64+0x12C*(i))));
        if ((u32)RMT32(RMTPTR(atInfo,0),0x10) & 0x100000) {
            RMT8(actor,0x1A22) = 1;
            RMTS8(actor,0x1DD4) = 1;
            RMTF(actor,0x1A24) = rmt_reg_f(0,0) + -20.0f;
            RMTS8(actor,0x3A1) = 0;
            return;
        }
        rmt_attack_power(atInfo);
        u8 type = RMT8(atInfo,0xA);
        if (type == 4 || ((u32)RMT32(RMTPTR(atInfo,0),0x10) & 0x80000)) {
            RMT8(actor,0x1A17) = 0;
            RMT16(actor,0x57C) = 5;
            hit = 2;
            rmt_water_sound(actor);
            break;
        }
        u8 state = RMT8(actor,0x570);
        if (state == 2) {
            if (type == 2) {
                RMTS8(actor,0x1A14) = 3;
                RMT16(actor,0x57C) = 5;
                return;
            }
            if (type == 6) {
                RMTS8(actor,0x1A14) = 2;
                RMT16(actor,0x57C) = 5;
                return;
            }
        } else {
            if (type == 2) {
                RMT16(actor,0x57C) = 5;
                rmt_knockback(actor, RMTA(atInfo,0xC).y);
                RMT8(actor,0x570) = 1;
                RMT8(actor,0x400) = 1;
                RMT8(actor,0x571) = 0x14;
                return;
            }
            if (type == 6) {
                break;
            }
        }
        if (state == 1) {
            RMT8(actor,0x571) = 15;
            rmt_set_back_pos(actor, RMTA(actor,0x328).y, 60.0f, -120.0f);
            RMT16(actor,0x57E) = rmt_reg_s(6,7) + 6;
            RMT16(actor,0x572) = rmt_reg_s(6,8) + 6;
            RMT16(actor,0x57C) = 5;
            rmt_defence(actor, RMTPTR(atInfo,0x0), 0x40);
            return;
        }
        if (state == 2) {
            RMT16(actor,0x57C) = rmt_reg_s(10,0) + 3;
            rmt_attack_check(actor, atInfo);
            if (RMTS8(actor,0x3A1) <= 0) {
                RMTS8(actor,0x1A14) = 2;
            }
            RMT16(actor,0x1A18) = 12;
            rmt_particle(0x80CF,actor+0x314,0,0);
        }
        break;
    }

    if (RMT16(actor,0x57C) == 0 && rmt_hit((actor+0x18C4))) {
        RMT16(actor,0x57C) = 5;
        RMTPTR(atInfo,0x0) = (u32)(rmt_hit_object((actor+0x18C4)));
        RMTPTR(atInfo,0x14) = ((actor+0x18C4)+0xCC);
        if ((u32)RMT32(RMTPTR(atInfo,0),0x10) & 0x100000) {
            RMT8(actor,0x1A22) = 1;
            RMTS8(actor,0x1DD4) = 1;
            return;
        }
        rmt_attack_power(atInfo);
        RMT8(actor,0x1A17) = (u8)(RMT8(actor,0x1A17) - RMT8(atInfo,0x8));
        u8 type = RMT8(atInfo,0xA);
        if (!((type >= 2 && type <= 4) || type == 6) && RMT8(actor,0xD20) != 1) {
            return;
        }
        RMTPTR(atInfo,0x4) = rmt_attack_check(actor, atInfo);
        if (RMT8(atInfo,0xA) == 6) {
            RMTS8(actor,0x1A14) = 1;
            hit = 1;
            RMT8(actor,0x1A17) = 0;
            rmt_monster(actor, 0x4803 /* JA_SE_CV_MT_DAMAGE? */);
        } else {
            if (RMT8(atInfo,0xA) == 4 || ((u32)RMT32(RMTPTR(atInfo,0),0x10) & 0x80000)) {
                RMT8(actor,0x1A17) = 0;
                hit = 2;
                rmt_water_sound(actor);
            }
            if ((s8)RMT8(actor,0x1A17) > 0) {
                RMT8(actor,0x571) = 15;
                rmt_set_back_pos(actor, RMTA(actor,0x328).y, 60.0f, -120.0f);
                RMT16(actor,0x57E) = 0x19;
                RMT16(actor,0x572) = 10;
                RMTA(actor,0x320).x = -0x4000;
                if ((u32)((u32)&RMTV(actor,0x37C)) != 0) {
                    rmt_monster(actor, 0x4803);
                }
            } else {
                hit = 1;
                rmt_monster(actor, 0x4803);
            }
        }
        rmt_animation(actor, 10, 2.0f, 2, 1.0f, 0);
        RMT8(actor,0x19F0) = 0;
    }

    if (hit != 0) {
        rmt_knockback(actor, RMTA(atInfo,0xC).y);
        if (hit == 2) {
            RMTV(actor,0x33C).y = 0.0f;
        }
        RMT8(actor,0x400) = 1;
        RMT8(actor,0x570) = 1;
        RMT8(actor,0x571) = 0x14;
    }
}
