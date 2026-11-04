/* Complete CC0 a874e1a9 d_a_st.cpp::daSt_Execute02493FB0 with the
 * MIT fix_vanilla_bugs.asm0249421C upper-to-lower light-arrow propagation.
 * Native movement/part/chain calls remain out of line. Damage, collision,
 * emitter and smoke helpers are source-expanded in their original order.
 * Source-expanded helper entry observers are not reproduced. */
#pragma once
#include "stalfos_helpers.h"
static void rst_damage(u32 actor) {
    u32 player = RSTPTR(wwhd_dComIfG_getGameInfo(),0x5B2C);
    rst_status_move(actor+0x648);
    if (!RST16(actor,0x412)) {
        _Alignas(4) unsigned int attack_storage[7];
        const u32 atInfo=(u32)attack_storage;
        RSTU16(atInfo,0x12) = 0;
        RST32(atInfo,0x18) = 0;
        for (u32 index = 0; index < 7; ++index) {
            u32 sphere = ((actor+0x668+0x12C*(index)));
            if (!rst_hit(sphere))
                continue;
            u32 object = rst_hit_object(sphere);
            RSTPTR(atInfo,0) = object;
            RSTPTR(atInfo,0x14) = (sphere+0xCC);
            if (RST32(object,0x10) & 0x100000) {
                RSTS8(actor,0x21F6) = 1;
                u32 held = rst_find(RST32(actor,0x2014));
                if (held)
                    rst_cancel_carry(held);
                RSTS8(actor,0x2018) = 0;
                return;
            }
            RST16(actor,0x412) = 5;
            if (index == 0 && RSTS8(actor,0x2020)) {
                s8 remaining = RSTS8(actor,0x3A9);
                RSTS8(actor,0x3A9) = 0;
                rst_attack_check(actor, atInfo);
                RSTS8(actor,0x3A9) = remaining;
                if (RST8(atInfo,0xA) == 1 && RST8(player,0x69E8))
                    RST16(actor,0x412) = 2;
                const u32 head=actor+0x1644;
                if (RSTS8(actor,0x3A1) <= 0 || RST8(atInfo,0xA) == 9 ||
                    RST8(atInfo,0xA) == 2) {
                    rst_head_animation(actor, 0x10, 1.0f, 0, 1.0f, -1);
                    RSTS8(head,0x4) = 8;
                    if (RST8(atInfo,0xA) == 9 && RST8(player,0x3AC) != 0x11) {
                        RSTV(head,0x20).x = 0.0f;
                        RSTV(head,0x20).y = -5.0f;
                        RSTV(head,0x20).z = 0.0f;
                        WWHD_GAME_DATA(0x101EACB7,u8) = 6;
                    } else {
                        RSTA(head,0x32).x = rst_truncate(rst_random_signed(9000.0f));
                        RSTA(head,0x32).z = rst_truncate(rst_random_signed(9000.0f));
                        rst_rotate_start(rst_matrix(), (s16)RST16(player,0x32A));
                        rst_vector direction;
                        if (RST8(player,0x3AC) == 0x11) {
                            direction.x = rst_reg_f(8,4) - 100.0f;
                            direction.y = 30.0f;
                            direction.z = 0.0f;
                            WWHD_GAME_DATA(0x101EACB7,u8) = 6;
                        } else {
                            direction.x = 0.0f;
                            direction.y = 50.0f;
                            direction.z = 50.0f;
                        }
                        rst_position((u32)&direction, (head+0x20));
                    }
                    RSTS8(head,0x40) = 30;
                    rst_monster(actor, 0x4884, 0);
                } else {
                    rst_head_animation(actor, 0xF, 1.0f, 0, 1.0f, -1);
                    RSTS8(head,0x4) = 7;
                    RSTS8(head,0x40) = 127;
                    RST16(actor,0x20CA) = rst_reg_s(8,2) + 20;
                    rst_monster(actor, 0x487B, 0);
                }
            } else {
                rst_attack_power(atInfo);
                if (RST8(atInfo,8))
                    rst_enemy_damage();
                u32 attacker = RSTPTR(atInfo,4);
                if (attacker && RST16(attacker,8) == 0x1BE && RSTS8(actor,0x3A9)) {
                    --RSTS8(actor,0x3A9);
                    rst_steal((actor+0x314), (u32)RST32(actor,0x3A4),
                                           (s8)RSTS8(actor,0x326), 0,
                                           (u8)RST8(actor,0x3A8));
                    ++RST8(actor,0x3A8);
                    attacker = RSTPTR(atInfo,4);
                }
                if ((attacker && RST16(attacker,8) == 0xA8) ||
                    RST8(atInfo,0xA) == 2) {
                    if (RST8(atInfo,0xA) == 2)
                        RSTS8(actor,0x20CC) = 0;
                    rst_defence_sound(actor, RSTPTR(atInfo,0), 0x34);
                    rst_monster(actor, 0x487B, 0);
                    RSTS8(actor,0x20CC) = (s8)RSTS8(actor,0x20CC) - (u8)RST8(atInfo,8);
                    RSTS8(actor,0x202C) = rst_reg_s(6,RST8(atInfo,0xA) == 9 ? 7 : 6) +
                                    (RST8(atInfo,0xA) == 9 ? 20 : 10);
                    if (RST8(atInfo,0xA) == 1 && RST8(player,0x69E8)) {
                        RST16(actor,0x412) = 2;
                        u8 cut = RST8(player,0x3AC);
                        if (cut == 8 || cut == 9)
                            RSTS8(actor,0x20CC) = (u8)RSTS8(actor,0x20CC) - (u8)RST8(atInfo,8);
                    }
                    rst_bgm_hit((s32)RST32(atInfo,0x18));
                    if ((s8)RSTS8(actor,0x20CC) <= 0) {
                        RSTS8(actor,0x2010) = 0;
                        u8 form = RST8(actor,0x25AD);
                        if (RSTU16(atInfo,0x12) == 0x80 && !form) {
                            RST16(actor,0x3E2) = 33;
                            RST16(actor,0x3E0) = 0;
                            RSTS8(actor,0xFEF) = 10;
                            RST32(actor,0x201C) = rst_create(
                                0xBE, 0xE, (actor+0x314), (s8)RSTS8(actor,0x326),
                                (actor+0x2F8), 0, -1, 0);
                            RSTS8(actor,0x20CC) = 5;
                            RSTS8(actor,0x20CE) = 0;
                        } else {
                            RST16(actor,0x3E0) = form == 2 ? 35 : form == 1 ? 20 : 0;
                            RST16(actor,0x3E2) = 30;
                            RSTS8(actor,0xFEF) = 1;
                            RST16(actor,0x414) = 0;
                            rst_sound(actor, 0x5891, 0);
                            rst_monster(actor, 0x487C, 0);
                        }
                    }
                } else if (index == 0) {
                    RSTS8(actor,0x202C) = rst_reg_s(6,7) + 5;
                    u8 form = RST8(actor,0x25AD);
                    if (form == 1) {
                        RST16(actor,0x3E2) = 31;
                        RST16(actor,0x3E0) = 10;
                    } else if (!form) {
                        RST16(actor,0x3E2) = 31;
                        RST16(actor,0x3E0) = 0;
                    }
                    rst_sound(actor, 0x588E, 0);
                    rst_monster(actor, 0x487A, 0);
                } else {
                    rst_defence_sound(actor, RSTPTR(atInfo,0), 0x34);
                    rst_monster(actor, 0x487B, 0);
                    RSTS8(actor,0x202C) = rst_reg_s(6,7) + 8;
                    if (!RST8(actor,0x25AD)) {
                        RST16(actor,0x3E2) = 0;
                        rst_wait(actor);
                        RST16(actor,0x3E0) = 1;
                        RST16(actor,0x408+2*(1)) = 10;
                    }
                }
            }
            break;
        }
    }
    if (RSTS8(actor,0x202C))
        --RSTS8(actor,0x202C);
}

static void rst_collision(u32 actor) {
    rst_vector zero, center, disabled, disabledWeapon;
    zero.x = 0.0f;
    zero.y = 0.0f;
    zero.z = 0.0f;
    disabled.x = -50000.0f;
    disabled.y = -50000.0f;
    disabled.z = 0.0f;
    disabledWeapon.x = 50000.0f;
    disabledWeapon.y = -50000.0f;
    disabledWeapon.z = 0.0f;
    for (u32 index = 0; index < 7; ++index) {
        s32 active = 0;
        if (RSTS8(actor,0xFEC) != 2) {
            if (RST16(actor,0x3E2) == 33 || RST16(actor,0x3E0) == 35)
                active = !RSTS8(actor,0xFEC) && index >= 5;
            else if (RSTS8(actor,0xFEC))
                active = index == 0;
            else
                active = RST16(actor,0x3E2) != 32 || index < 5;
        }
        u32 sphere = ((actor+0x668+0x12C*(index)));
        if (active) {
            if (!index && RSTS8(actor,0x2020)) {
                u32 model = RSTPTR(RSTPTR(actor,0x3DC),0x90);
                rst_matrix_copy((model?model+0xC8:0), rst_matrix());
            } else
                rst_joint(actor, (s32)RST32((u32)&WWHD_GAME_DATA(0x101D0E9C,u8),index*4));
            rst_position((u32)&zero, (u32)&center);
            rst_sphere_center((sphere+0x118), (u32)&center);
            rst_sphere_radius((sphere+0x118), index ? 25.0f : 45.0f);
            RST32(sphere,0x18) = (u32)RST32(sphere,0x18) | 1;
        } else {
            rst_sphere_radius((sphere+0x118), -500.0f);
            rst_sphere_center((sphere+0x118), (u32)&disabled);
        }
        rst_collision_register(sphere);
    }
    u32 weapon = ((actor+0xE9C));
    if (RSTS8(actor,0xFED) && !RSTS8(actor,0xFEC)) {
        if (RSTS8(actor,0x2010)) {
            rst_copy_vector((u32)&center, actor + 0x1FCC);
            rst_sphere_radius((weapon+0x118), 30.0f);
        } else if (RSTS8(actor,0xFED) == 3) {
            rst_joint(actor, 25);
            zero.x = 0.0f;
            zero.y = 0.0f;
            zero.z = 0.0f;
            rst_position((u32)&zero, (u32)&center);
            rst_sphere_radius((weapon+0x118), 30.0f);
            RST8(weapon,0x6F) = 0;
        } else {
            RST8(weapon,0x6F) = 6;
            rst_joint(actor, 11);
            f32 factor = 1.0f;
            if (RSTS8(actor,0xFED) == 2) {
                f32 blend = __builtin_fabsf((f32)RSTF(actor,0x424));
                if (blend > 0.5f)
                    RST8(weapon,0x6F) = 7;
                factor = blend * 1.5f;
            }
            s32 odd = (u32)RST32(actor,0x404) & 1;
            zero.x = rst_reg_f(0,10) + (odd ? 80.0f : 30.0f);
            rst_sphere_radius((weapon+0x118), factor * (odd ? 70.0f : 50.0f));
            zero.y = 0.0f;
            zero.z = 0.0f;
            rst_position((u32)&zero, (u32)&center);
        }
        if (RSTS8(actor,0x20CF))
            rst_move_attack((actor+0xE9C), (u32)&center);
        else {
            rst_start_attack((actor+0xE9C), (u32)&center);
            ++RSTS8(actor,0x20CF);
        }
        RST32(weapon,0) = (u32)RST32(weapon,0) | 1;
    } else {
        RSTS8(actor,0x20CF) = 0;
        rst_sphere_center((weapon+0x118), (u32)&disabledWeapon);
    }
    rst_collision_register(weapon);
    RSTS8(actor,0xFEC) = 0;
    RSTS8(actor,0xFED) = 0;
}

static void rst_effects(u32 actor) {
    if (RST16(actor,0x21A8))
        --RST16(actor,0x21A8);
    if (RSTS8(actor,0x21B0) == 1) {
        RSTF(actor,0x21AC) = 1.0f;
        if (!RST16(actor,0x21A8)) {
            RST16(actor,0x21A8) = 15;
            for (u32 index = 0; index < 20; ++index)
                if (!RSTPTR(actor,0x2130+4*(index))) {
                    u32 emitter = rst_particle(0x817B, (actor+0x314));
                    RSTPTR(actor,0x2130+4*(index)) = emitter;
                    if (emitter) {
                        RST32(emitter,0x254) = (u32)RST32(emitter,0x254) | 0x40;
                        RST16(actor,0x2180+2*(index)) = 200;
                    }
                    break;
                }
        }
    }
    if (RSTS8(actor,0x21B0) == 2)
        rst_zero((actor+0x21AC), 1.0f, 0.1f);
    for (u32 index = 0; index < 20; ++index) {
        u32 emitter = RSTPTR(actor,0x2130+4*(index));
        if (emitter) {
            RSTV(emitter,0x238).x = RSTF(actor,0x21AC);
            if (RST16(actor,0x2180+2*(index)) && (--RST16(actor,0x2180+2*(index)), !RST16(actor,0x2180+2*(index)))) {
                emitter = RSTPTR(actor,0x2130+4*(index));
                RST32(emitter,0x5C) = 0xFFFFFFFF;
                RST32(emitter,0x254) = (u32)RST32(emitter,0x254) | 1;
                RSTPTR(actor,0x2130+4*(index)) = 0;
            }
        }
    }
}

static void rst_smoke(u32 actor, f32 rate) {
    _Alignas(4) u8 line_storage[0x6C];
    const u32 line=(u32)line_storage;
    rst_line_initialize(line);
    rst_vector start, end;
    rst_copy_vector((u32)&start, ((actor+0x21B4)));
    rst_copy_vector((u32)&end, ((actor+0x21B4)));
    start.y = (f32)start.y + 100.0f;
    end.y = (f32)end.y - 100.0f;
    u32 attribute = 0;
    if (rst_line_cross(line, (u32)&start, (u32)&end, actor)) {
        u32 check = line;
        RSTV(actor,0x21B4).y = RSTF(line,0x34) + 30.0f;
        u32 play = wwhd_dComIfG_getGameInfo();
        attribute =
            rst_attribute((play+0x12A0), (check+0x14));
    } else
        RSTV(actor,0x21B4).y = (f32)RSTV(actor,0x21B4).y - 20000.0f;
    if (!RSTS8(actor,0x21CB) || attribute == 4) {
        ++RSTS8(actor,0x21CB);
        if (attribute < 4 || attribute == 11) {
            rst_smoke_end((actor+0x20D0+0x20*(2)));
            u32 emitter =
                rst_toon(actor, 0x2022, (actor+0x21B4), (actor+0x21C0), (actor+0x20D0+0x20*(2)));
            if (emitter) {
                RSTF(emitter,0x34) = rate;
                RSTF(emitter,0x58) = 1.0f;
                RSTV(emitter,0x220).x = 0.8f;
                RSTV(emitter,0x220).y = 0.8f;
                RSTV(emitter,0x220).z = 0.8f;
                f32 scale = rst_reg_f(0,11) + 2.0f;
                RSTV(emitter,0x238).x = scale;
                RSTV(emitter,0x238).y = scale;
                RSTV(emitter,0x238).z = scale;
            }
        } else if (attribute == 4) {
            u32 emitter = rst_particle_angle(0x24, (actor+0x21B4), (actor+0x21C0));
            if (emitter) {
                RST32(emitter,0x5C) = 3;
                RSTF(emitter,0x34) = rate * 0.5f;
            }
        }
    }
    rst_line_finish(line);
}

static void rst_spin(u32 actor) {
    if (!RST16(actor,0x21C8))
        return;
    --RST16(actor,0x21C8);
    if (RST16(actor,0x21C8) >= 10) {
        rst_vector direction;
        direction.x = 0.0f;
        direction.y = 0.0f;
        RSTA(actor,0x21C0).x = 0;
        RSTA(actor,0x21C0).z = 0;
        rst_translate(0, (f32)RSTV(actor,0x314).x, (f32)RSTV(actor,0x314).y + 7.5f,
                 (f32)RSTV(actor,0x314).z);
        direction.z = (rst_reg_f(0,2) + 1.0f) * -175.0f;
        rst_rotate(rst_matrix(), (s16)RST16(actor,0x21C6));
        rst_position((u32)&direction, (actor+0x21B4));
        RSTA(actor,0x21C0).y = RST16(actor,0x21C6);
        rst_smoke(actor, rst_reg_f(0,3) + 3.0f);
        if (RSTS8(actor,0x21CA) == 2)
            RST16(actor,0x21C6) += (s16)rst_truncate((rst_reg_f(0,11) + 5000.0f) * (f32)RSTF(actor,0x424));
        else {
            s16 step = rst_reg_s(0,7) + 2000;
            RST16(actor,0x21C6) += (RSTS8(actor,0x21CA) == 0 ? step : -step);
        }
    } else
        RSTV(actor,0x21B4).y = (f32)RSTV(actor,0x314).y + 20000.0f;
    if (!RST16(actor,0x21C8)) {
        rst_smoke_end((actor+0x20D0+0x20*(2)));
        RSTS8(actor,0x21CB) = 0;
    }
}

WWHD_REPLACE(0x02493FB0,s32,randomizer_stalfos_execute,(u32 actor)) {
    wwhd_dComIfG_getGameInfo();
    if (RSTS8(actor,0x20CD)) {
        --RSTS8(actor,0x20CD);
        if (!RSTS8(actor,0x20CD))
            rst_delete(actor);
        return 1;
    }
    if (RSTS8(actor,0x416) || ((f32)RSTV(actor,0x2EC).y - (f32)RSTV(actor,0x314).y > 4000.0f)) {
        rst_delete(actor);
        return 1;
    }
    if (RSTS8(actor,0x25AC) && (--RSTS8(actor,0x25AC), !RSTS8(actor,0x25AC)))
        RST32(actor,0x2E0) = (u32)RST32(actor,0x2E0) & ~0x4000u;
    if (rst_ice((actor+0x21F0))) {
        u32 model = RSTPTR(RSTPTR(actor,0x3D8),0x90);
        rst_translate(0, (f32)RSTV(actor,0x314).x, (f32)RSTV(actor,0x314).y,
                 (f32)RSTV(actor,0x314).z);
        rst_rotate(rst_matrix(), (s16)((s16)RSTA(actor,0x328).y + (s16)RST16(actor,0x420)));
        f32 horizontal = RSTF(actor,0x2394), vertical = RSTF(actor,0x2398);
        rst_scale(1, horizontal, vertical, horizontal);
        rst_copy_model(model, rst_matrix());
        rst_morf_calc(RSTPTR(actor,0x3D8));
        rst_morf_calc(RSTPTR(actor,0x3DC));
        rst_visible(actor);
        return 1;
    }
    WWHD_GAME_DATA(0x1046DFDC,u8)=0;
    if (RST32(actor,0x201C) != 0xFFF) {
        u32 upper = rst_find(RST32(actor,0x201C));
        /* Upstream returns the original actor-search result after this write. */
        if (upper && RST8(upper,0x21F6) && !RST8(actor,0x21F6))
            RST8(actor,0x21F6) = 1;
        if (upper && RSTS8(upper,0x20CD)) {
            rst_delete(actor);
            for (u32 index = 0; index < 26; ++index)
                if (RSTS8(actor,0xFF4+0x6C*(index)) < 10)
                    rst_particle(0x817E, (actor+0xFF8+0x6C*index));
            u32 weapon = rst_find(RST32(actor,0x2014));
            if (weapon)
                rst_cancel_carry(weapon);
            return 1;
        }
    }
    ++RST32(actor,0x404);
    for (u32 index = 0; index < 5; ++index)
        if (RST16(actor,0x408+2*(index)))
            --RST16(actor,0x408+2*(index));
    if (RST16(actor,0x412))
        --RST16(actor,0x412);
    if (RST16(actor,0x2024))
        --RST16(actor,0x2024);
    rst_move(actor);
    rst_damage(actor);
    rst_angle_calc((actor+0x32A),
                        (s16)((s16)RSTA(actor,0x320).y + (s16)RST16(actor,0x41E)), 2, 0x1000);
    RST16(actor,0x41E) = 0;
    rst_morf_play(RSTPTR(actor,0x3D8), (actor+0x37C), 0, 0);
    rst_morf_play(RSTPTR(actor,0x3DC), (actor+0x37C), 0, 0);
    u32 model = RSTPTR(RSTPTR(actor,0x3D8),0x90);
    /* Native loads scale Z/X/Y at 338/330/334 before its model writes. */
    const unsigned int sz=RST32(actor,0x338),sx=RST32(actor,0x330),sy=RST32(actor,0x334);
    RST32(model,0xBC)=sx;
    RST32(model,0xC0)=sy;
    RST32(model,0xC4)=sz;
    rst_translate(0, (f32)RSTV(actor,0x314).x, (f32)RSTV(actor,0x314).y, (f32)RSTV(actor,0x314).z);
    rst_rotate(rst_matrix(), (s16)((s16)RSTA(actor,0x328).y + (s16)RST16(actor,0x420)));
    rst_copy_model(model, rst_matrix());
    rst_morf_calc(RSTPTR(actor,0x3D8));
    rst_morf_calc(RSTPTR(actor,0x3DC));
    rst_collision(actor);
    for (u32 joint = 0; joint < 26; ++joint)
        if (RSTPTR(actor,0xFF0+0x6C*(joint)) && (!RSTS8(actor,0x2010) || (joint != 3 && joint != 4)))
            rst_part(actor, joint);
    if (RSTS8(actor,0xFEF) != 2)
        RSTS8(actor,0xFEF) = 0;
    rst_angle_calc((actor+0x420), 0, 2, 0x40);
    if (RST16(actor,0x3E2) != 33 && RST16(actor,0x3E0) != 35)
        rst_chain(actor);
    if (RSTS8(actor,0x2010))
        rst_weapon_chain(actor);
    rst_effects(actor);
    rst_spin(actor);
    u32 environment = rst_environment();
    rst_lighting(environment, 0, (actor+0x314),
                                        (actor+0x10C));
    return 1;
}
