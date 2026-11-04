/* Public CC0 d_a_kb_exec.cpp, Execute helper expanded from the native consumer. */
#pragma once
static inline void rp_particle_tuba(u32 i_this) {
    /* dComIfGp_particle_set(ID_AK_JN_TUBA00, &current.pos, &shape_angle) */
    rp_particle_set(RP_PTR(rp_play(),0x5AB0), 0, 0xE, &RP_VEC(i_this,0x314), &RP_ANGLE(i_this,0x328),
                    0, 0xFF, 0, -1, 0, 0, 0);
}
static inline void rp_copy_angle(volatile rp_angle *dst, volatile rp_angle *src) {
    dst->x = (s16)src->x;
    dst->y = (s16)src->y;
    dst->z = (s16)src->z;
}
static inline s32 rp_checkPass(u32 i_this, f32 f) {
    return rp_check_frame(RP_PTR(i_this,0x744)+0x98,f) != 0;
}
/* inlined into daKb_Execute */
static inline void rp_start_shock(u32 unused) {
    (void)unused;
    rp_vector dir;
    rp_set_vector(&dir,0.0f, 1.0f, 0.0f);
    rp_shock_up(&dir);
}

/* inlined into daKb_Execute */
static inline void rp_pl_attack_hit_check(u32 i_this) {
    if (rp_hit(i_this+0xBD4)) {
        if (RP_U8(i_this,0x74B) == 0) {
            RP_U8(i_this,0x74B) = 1;
            rp_particle_tuba(i_this);
            for (int i = 0; i < 2; i++) {
                rp_follow_end((i_this+0x944+0x14*(i)));
            }
            rp_copy_vector((u32)&RP_VEC(i_this,0x824),(u32)&(RP_VEC(i_this,0xCA0)));
            if (RP_S16(i_this,0x768) != 5) {
                RP_S16(i_this,0x768) = 4;
            }
            unsigned int atInfo[7];
            atInfo[5] = 0;
            atInfo[0] = rp_hit_object(i_this+0xBD4);
            rp_attack_check(i_this,(u32)atInfo);

            if (RP_S8(i_this,0x3A1) <= 0) {
                RP_S8(i_this,0x3A1) = 10;
                RP_U8((u32)&WWHD_GAME_DATA(0x101B7FA0,u8),0) = 1;
                RP_U32(i_this,0x39C) |= 0x00000004;
                RP_S16(i_this,0x768) = 0x21;
                RP_S16(i_this,0x766) = 3;
            }
            rp_catch_sound(i_this);
        }
    } else {
        RP_U8(i_this,0x74B) = 0;
        if (RP_U8((u32)&WWHD_GAME_DATA(0x101B7FA0,u8),0)) {
            if (RP_S32(i_this,0x8BC) != 0x16) {
                rp_animation(i_this, 0x16, 5.0f, 2, 2.0f,
                         0xB);
            }
            RP_U32(i_this,0x39C) |= 0x00000004;
            RP_U32(i_this,0x39C) &= ~(u32)0x00000010;
            RP_F32(i_this,0x80C) = 30.0f;
            RP_S16(i_this,0x766) = 3;
            RP_S16(i_this,0x768) = 0x23;
        }
    }
}
