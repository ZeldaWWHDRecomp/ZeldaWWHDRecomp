/* Complete public CC0 d_a_bdk_move.cpp0206D644, a874e1a9.
 * Public MIT fix_rng.asm changes three choices. Keep their RNG calls, every
 * other action/gate/callback, and the common animation continuation.
 * Disabled seeds retain the literal native function. */
/* Public CC0 HD Helmaroc field views. All production offsets are asserted. */
#pragma once
#ifndef init_fix_helmaroc_rng
#error "Missing validated Helmaroc RNG seed option"
#endif
#if init_fix_helmaroc_rng != 0 && init_fix_helmaroc_rng != 1
#error "Invalid Helmaroc RNG seed option"
#endif
#if init_fix_helmaroc_rng
#include <stdbool.h>
typedef struct {
  f32 x, y, z;
} hrng_vec;
typedef struct {
  s16 x, y, z;
} hrng_angle;
typedef struct {
  hrng_vec pos;
  hrng_angle angle;
  s8 room;
  u8 pad;
} hrng_place;
typedef struct {
  u8 distances[8];
  hrng_vec position;
  u32 flags;
} hrng_attention;
typedef struct {
  u8 pad[0x98];
  f32 rate, frame;
  u8 tail[7];
  u8 state;
} hrng_morf;
typedef struct {
  u8 pad[0x28];
  u32 flags;
  u8 body[0x94 - 0x2C];
  f32 ground;
} hrng_acch;
typedef struct {
  s8 m000, m001;
  u8 pad[2];
  hrng_vec m004;
  u8 body[0x3C - 0x10];
  s16 m03C;
  u8 tail[0x174 - 0x3E];
} hrng_effect;
typedef struct {
  u8 pad[0x98];
  u8 mColorK0[4];
} hrng_light;
typedef struct {
  u8 bytes[0x20];
} hrng_smoke;
typedef union {
  u8 bytes[0x6C];
  unsigned int alignment;
} hrng_line;
typedef struct {
  s8 no;
  u8 mode;
  s16 value;
  f32 m008, m00C, m010;
  u8 m014;
  u8 pad[3];
  f32 m018, m01C, m020, m024;
  s16 m028;
  u8 pad26[2];
  u32 vtable;
} hrng_hio;
#ifndef HRNG_FIELD_VIEW
#define HRNG_FIELD_VIEW union
#endif
typedef HRNG_FIELD_VIEW hrng_actor hrng_actor;
HRNG_FIELD_VIEW hrng_actor {
  u8 storage[0x65E0];
  struct {
    u8 pad_shape_angle[0x328];
    hrng_angle shape_angle;
  };
  struct {
    u8 pad_pid[0x4];
    u32 pid;
  };
  struct {
    u8 pad_home[0x2EC];
    hrng_place home;
  };
  struct {
    u8 pad_current[0x314];
    hrng_place current;
  };
  struct {
    u8 pad_speed[0x33C];
    hrng_vec speed;
  };
  struct {
    u8 pad_speedF[0x370];
    f32 speedF;
  };
  struct {
    u8 pad_gravity[0x374];
    f32 gravity;
  };
  struct {
    u8 pad_eyePos[0x37C];
    hrng_vec eyePos;
  };
  struct {
    u8 pad_attention_info[0x388];
    hrng_attention attention_info;
  };
  struct {
    u8 pad_mpMorf[0x3D4];
    hrng_morf *mpMorf;
  };
  struct {
    u8 pad_m2C4[0x3D8];
    s16 m2C4;
  };
  struct {
    u8 pad_mAction[0x3DA];
    s16 mAction;
  };
  struct {
    u8 pad_mState[0x3DC];
    s16 mState;
  };
  struct {
    u8 pad_m2CA[0x3DE];
    s16 m2CA;
  };
  struct {
    u8 pad_m2CC[0x3E0];
    hrng_vec m2CC;
  };
  struct {
    u8 pad_m2DC[0x3F0];
    f32 m2DC;
  };
  struct {
    u8 pad_m2E0[0x3F4];
    f32 m2E0;
  };
  struct {
    u8 pad_m2E4[0x3F8];
    f32 m2E4;
  };
  struct {
    u8 pad_m2E8[0x3FC];
    f32 m2E8;
  };
  struct {
    u8 pad_m2EC[0x400];
    s16 m2EC[5];
  };
  struct {
    u8 pad_m2FC[0x410];
    u8 m2FC;
  };
  struct {
    u8 pad_m8F8[0xA08];
    u8 m8F8;
  };
  struct {
    u8 pad_mF10[0x1010];
    u8 mF10;
  };
  struct {
    u8 pad_mAcch[0x1058];
    hrng_acch mAcch;
  };
  struct {
    u8 pad_m1134[0x1230];
    s16 m1134;
  };
  struct {
    u8 pad_m1136[0x1232];
    s16 m1136;
  };
  struct {
    u8 pad_m1138[0x1234];
    s16 m1138;
  };
  struct {
    u8 pad_m1168[0x1264];
    hrng_vec m1168;
  };
  struct {
    u8 pad_m1174[0x1270];
    hrng_vec m1174[2];
  };
  struct {
    u8 pad_m2584[0x2680];
    u8 m2584;
  };
  struct {
    u8 pad_m2586[0x2682];
    s8 m2586;
  };
  struct {
    u8 pad_m2588[0x2684];
    s32 m2588;
  };
  struct {
    u8 pad_m258C[0x2688];
    s32 m258C;
  };
  struct {
    u8 pad_m2590[0x268C];
    u8 m2590;
  };
  struct {
    u8 pad_m2591[0x268D];
    u8 m2591;
  };
  struct {
    u8 pad_m2592[0x268E];
    s8 m2592;
  };
  struct {
    u8 pad_m2593[0x268F];
    s8 m2593;
  };
  struct {
    u8 pad_m2594[0x2690];
    s8 m2594;
  };
  struct {
    u8 pad_m2618[0x2714];
    s8 m2618;
  };
  struct {
    u8 pad_m261C[0x2718];
    hrng_effect m261C[40];
  };
  struct {
    u8 pad_m6078[0x6174];
    s16 m6078[4];
  };
  struct {
    u8 pad_m6080[0x617C];
    hrng_smoke m6080[4];
  };
  struct {
    u8 pad_m6224[0x6320];
    hrng_light m6224;
  };
};
#if defined(__powerpc__)
_Static_assert(__builtin_offsetof(hrng_actor, shape_angle) == 0x328,
               "shape angles");
_Static_assert(__builtin_offsetof(hrng_actor, pid) == 0x4, "Helmaroc pid");
_Static_assert(__builtin_offsetof(hrng_actor, home) == 0x2EC, "Helmaroc home");
_Static_assert(__builtin_offsetof(hrng_actor, current) == 0x314,
               "Helmaroc current");
_Static_assert(__builtin_offsetof(hrng_actor, speed) == 0x33C,
               "Helmaroc speed");
_Static_assert(__builtin_offsetof(hrng_actor, speedF) == 0x370,
               "Helmaroc speedF");
_Static_assert(__builtin_offsetof(hrng_actor, gravity) == 0x374,
               "Helmaroc gravity");
_Static_assert(__builtin_offsetof(hrng_actor, eyePos) == 0x37C,
               "Helmaroc eyePos");
_Static_assert(__builtin_offsetof(hrng_actor, attention_info) == 0x388,
               "Helmaroc attention_info");
_Static_assert(__builtin_offsetof(hrng_actor, mpMorf) == 0x3D4,
               "Helmaroc mpMorf");
_Static_assert(__builtin_offsetof(hrng_actor, m2C4) == 0x3D8, "Helmaroc m2C4");
_Static_assert(__builtin_offsetof(hrng_actor, mAction) == 0x3DA,
               "Helmaroc mAction");
_Static_assert(__builtin_offsetof(hrng_actor, mState) == 0x3DC,
               "Helmaroc mState");
_Static_assert(__builtin_offsetof(hrng_actor, m2CA) == 0x3DE, "Helmaroc m2CA");
_Static_assert(__builtin_offsetof(hrng_actor, m2CC) == 0x3E0, "Helmaroc m2CC");
_Static_assert(__builtin_offsetof(hrng_actor, m2DC) == 0x3F0, "Helmaroc m2DC");
_Static_assert(__builtin_offsetof(hrng_actor, m2E0) == 0x3F4, "Helmaroc m2E0");
_Static_assert(__builtin_offsetof(hrng_actor, m2E4) == 0x3F8, "Helmaroc m2E4");
_Static_assert(__builtin_offsetof(hrng_actor, m2E8) == 0x3FC, "Helmaroc m2E8");
_Static_assert(__builtin_offsetof(hrng_actor, m2EC) == 0x400, "Helmaroc m2EC");
_Static_assert(__builtin_offsetof(hrng_actor, m2FC) == 0x410, "Helmaroc m2FC");
_Static_assert(__builtin_offsetof(hrng_actor, m8F8) == 0xA08, "Helmaroc m8F8");
_Static_assert(__builtin_offsetof(hrng_actor, mF10) == 0x1010, "Helmaroc mF10");
_Static_assert(__builtin_offsetof(hrng_actor, mAcch) == 0x1058,
               "Helmaroc mAcch");
_Static_assert(__builtin_offsetof(hrng_actor, m1134) == 0x1230,
               "Helmaroc m1134");
_Static_assert(__builtin_offsetof(hrng_actor, m1136) == 0x1232,
               "Helmaroc m1136");
_Static_assert(__builtin_offsetof(hrng_actor, m1138) == 0x1234,
               "Helmaroc m1138");
_Static_assert(__builtin_offsetof(hrng_actor, m1168) == 0x1264,
               "Helmaroc m1168");
_Static_assert(__builtin_offsetof(hrng_actor, m1174) == 0x1270,
               "Helmaroc m1174");
_Static_assert(__builtin_offsetof(hrng_actor, m2584) == 0x2680,
               "Helmaroc m2584");
_Static_assert(__builtin_offsetof(hrng_actor, m2586) == 0x2682,
               "Helmaroc m2586");
_Static_assert(__builtin_offsetof(hrng_actor, m2588) == 0x2684,
               "Helmaroc m2588");
_Static_assert(__builtin_offsetof(hrng_actor, m258C) == 0x2688,
               "Helmaroc m258C");
_Static_assert(__builtin_offsetof(hrng_actor, m2590) == 0x268C,
               "Helmaroc m2590");
_Static_assert(__builtin_offsetof(hrng_actor, m2591) == 0x268D,
               "Helmaroc m2591");
_Static_assert(__builtin_offsetof(hrng_actor, m2592) == 0x268E,
               "Helmaroc m2592");
_Static_assert(__builtin_offsetof(hrng_actor, m2593) == 0x268F,
               "Helmaroc m2593");
_Static_assert(__builtin_offsetof(hrng_actor, m2594) == 0x2690,
               "Helmaroc m2594");
_Static_assert(__builtin_offsetof(hrng_actor, m2618) == 0x2714,
               "Helmaroc m2618");
_Static_assert(__builtin_offsetof(hrng_actor, m261C) == 0x2718,
               "Helmaroc m261C");
_Static_assert(__builtin_offsetof(hrng_actor, m6078) == 0x6174,
               "Helmaroc m6078");
_Static_assert(__builtin_offsetof(hrng_actor, m6080) == 0x617C,
               "Helmaroc m6080");
_Static_assert(__builtin_offsetof(hrng_actor, m6224) == 0x6320,
               "Helmaroc m6224");
_Static_assert(__alignof__(hrng_line) >= 4, "line-check alignment");
_Static_assert(sizeof(hrng_effect) == 0x174, "effect stride");
_Static_assert(sizeof(hrng_smoke) == 0x20, "smoke stride");
_Static_assert(__builtin_offsetof(hrng_morf, state) == 0xA7, "morf stop state");
_Static_assert(__builtin_offsetof(hrng_attention, flags) == 0x14,
               "attention flags");
_Static_assert(__builtin_offsetof(hrng_acch, ground) == 0x94, "ground height");
_Static_assert(sizeof(hrng_hio) == 0x2C, "HIO");
#endif
enum {
  hrng_dRes_INDEX_BDK_BCK_FLY1_e = 0x2A,
  hrng_dRes_INDEX_BDK_BAS_FLY1_e = 0xB,
  hrng_dRes_INDEX_BDK_BCK_TOBITATU1_e = 0x3B,
  hrng_dRes_INDEX_BDK_BAS_TOBITATU1_e = 0x16,
  hrng_dRes_INDEX_BDK_BCK_TYAKUTI1_e = 0x3E,
  hrng_dRes_INDEX_BDK_BAS_TYAKUTI1_e = 0x17,
  hrng_dRes_INDEX_BDK_BCK_KAKKU1_e = 0x2F,
  hrng_ID_IT_SN_DK_TYAKUTI_ROCK00 = 0x8131,
  hrng_ID_IT_ST_DK_TYAKUTI_SMOKE00 = 0xA132,
  hrng_dRes_INDEX_BDK_BCK_WAIT1_e = 0x40,
  hrng_dRes_INDEX_BDK_BCK_JUMP1_e = 0x2E,
  hrng_dRes_INDEX_BDK_BAS_JUMP1_e = 0xF,
  hrng_dRes_INDEX_BDK_BCK_SENKAI_L1_e = 0x37,
  hrng_dRes_INDEX_BDK_BAS_SENKAI_L1_e = 0x13,
  hrng_dRes_INDEX_BDK_BCK_SENKAI_R1_e = 0x38,
  hrng_dRes_INDEX_BDK_BAS_SENKAI_R1_e = 0x14,
  hrng_dRes_INDEX_BDK_BCK_WALK1_e = 0x42,
  hrng_dRes_INDEX_BDK_BAS_WALK1_e = 0x19,
  hrng_dRes_INDEX_BDK_BCK_JIDANDA1_e = 0x2D,
  hrng_dRes_INDEX_BDK_BAS_JIDANDA1_e = 0xE,
  hrng_ID_IT_SN_DK_JIDANDA_ROCK00 = 0x8133,
  hrng_ID_IT_ST_DK_JIDANDA_SMOKE00 = 0xA134,
  hrng_dRes_INDEX_BDK_BCK_ATTACK1_e = 0x1C,
  hrng_dRes_INDEX_BDK_BAS_ATTACK1_e = 7,
  hrng_JA_SE_CV_DK_ATTACK = 0x4865,
  hrng_JA_SE_CM_DK_ATTACK = 0x5872,
  hrng_ID_IT_SN_DK_KUTI_SENKO00 = 0x812D,
  hrng_dRes_INDEX_BDK_BCK_NUKENAI1_e = 0x33,
  hrng_JA_SE_CM_DK_JITABATA = 0x5873,
  hrng_JA_SE_CV_DK_NUKEZU = 0x4866,
  hrng_dRes_INDEX_BDK_BCK_NUKU1_e = 0x34,
  hrng_JA_SE_CM_DK_PULL_UP_HEAD = 0x5874,
  hrng_JA_SE_CV_DK_NUKERU = 0x4867,
  hrng_ID_IT_ST_DK_KUTI_SMOKE00 = 0xA12E,
  hrng_ID_IT_SN_DK_KUTI_ROCK00 = 0x812C,
  hrng_ACTION_FLY = 0x0,
  hrng_ACTION_UP_FLY = 0x1,
  hrng_ACTION_LANDING = 0x2,
  hrng_ACTION_WAIT = 0x3,
  hrng_ACTION_JUMP = 0x4,
  hrng_ACTION_KUTI_ATTACK = 0x5,
  hrng_ACTION_JIDA_ATTACK = 0x6,
  hrng_ACTION_FLY_ATTACK = 0x7,
  hrng_ACTION_WIND_ATTACK = 0x8,
  hrng_ACTION_KAMEN_DEMO = 0x9,
  hrng_ACTION_END = 0xa,
  hrng_ACTION_START = 0xf,
  hrng_ACTION_T_FLY = 0x64,
  hrng_ACTION_T_LASTATTACK = 0x66,
  hrng_ACTION_T_DOWN = 0x67,
  hrng_ACTION_AFTER_FIGHT = 0x6e,
};
WWHD_GAME_FUNC(0x025200D4, u32, hrng_game, (void));
WWHD_GAME_FUNC(0x028F4384, f64, hrng_sqrt, (f64));
#ifndef HRNG_READ_POINTER
#define HRNG_READ_POINTER(a) (*(volatile u32 *)(a))
#endif
#define hrng_hio_data WWHD_GAME_DATA(0x1046191C, hrng_hio)
static f32 hrng_REG_F(s32 child, s32 index) {
  return (
      (volatile f32 *)&WWHD_GAME_DATA(0x1047B610, u8))[child * 0x24 + index];
}
static s16 hrng_REG_S(s32 child, s32 index) {
  return (
      (volatile s16 *)&WWHD_GAME_DATA(0x1047B688, u8))[child * 0x48 + index];
}
#define hrng_REG0_F(i) hrng_REG_F(0, i)
#define hrng_REG0_S(i) hrng_REG_S(0, i)
#define hrng_REG8_F(i) hrng_REG_F(8, i)
static hrng_actor *hrng_dComIfGp_getPlayer(s32 index) {
  return (hrng_actor *)(u32)HRNG_READ_POINTER(hrng_game() + 0x5B2C + 8 * index);
}
static s8 hrng_fopAcM_GetRoomNo(hrng_actor *a) { return a->current.room; }
static void *hrng_calc_mtx(void) {
  return (void *)(u32)WWHD_GAME_DATA(0x1018C7B0, u32);
}
static s32 hrng_morf_stopped(hrng_morf *m) {
  return (m->state & 1) || m->rate == 0.0f;
}
static void hrng_copy_float(volatile void *dst, const volatile void *src) {
  *(volatile unsigned int *)dst = *(const volatile unsigned int *)src;
}
static f32 hrng_fadds_ppc(f64 a, f64 b) { return (f32)(a + b); }
static f32 hrng_fsubs_ppc(f64 a, f64 b) { return (f32)(a - b); }
static f32 hrng_fmuls_ppc(f64 a, f64 b) {
#if defined(__powerpc__)
  f32 result;
  __asm__("fmuls %0,%1,%2" : "=f"(result) : "f"(a), "f"(b));
  return result;
#else
  union {
    f64 value;
    unsigned long long bits;
  } rounded = {.value = b};
  rounded.bits =
      (rounded.bits & 0xFFFFFFFFF8000000ull) + (rounded.bits & 0x8000000ull);
  return (f32)(a * rounded.value);
#endif
}
static f32 hrng_madd(f32 a, f32 b, f32 c) {
#pragma clang fp contract(fast)
  return a * b + c;
}
static s32 hrng_ftoi(f64 value) {
  if (value != value || value <= -2147483648.0)
    return (s32)0x80000000u;
  if (value >= 2147483648.0)
    return 0x7FFFFFFF;
  return (s32)value;
}
WWHD_GAME_FUNC(0x027EC9E8, void, hrng_BdkMove_report,
               (s32, s32, const char *, ...));
WWHD_GAME_FUNC(0x02065208, s32, hrng_BdkMove_landAreaCheck, (hrng_vec *, f32));
WWHD_GAME_FUNC(0x02008B4C, void, hrng_line_destroy_native, (hrng_line *, s32));
WWHD_GAME_FUNC(0x02008FEC, void, hrng_line_ctor_native, (hrng_line *));
WWHD_GAME_FUNC(0x024F1AFC, void, hrng_dBgS_LinChk_Set,
               (hrng_line *, hrng_vec *, hrng_vec *, hrng_actor *));
WWHD_GAME_FUNC(0x02008860, u32, hrng_line_cross, (void *, hrng_line *));
WWHD_GAME_FUNC(0x020198D8, f64, hrng_cM_rndF, (f64));
WWHD_GAME_FUNC(0x02019918, f64, hrng_cM_rndFX, (f64));
WWHD_GAME_FUNC(0x028E8DD0, f64, hrng_PSVECSquareMag, (hrng_vec *));
WWHD_GAME_FUNC(0x0201ADE0, void, hrng_vec_mi,
               (const hrng_vec *, hrng_vec *, const hrng_vec *));
WWHD_GAME_FUNC(0x0201AD78, void, hrng_vec_pl,
               (const hrng_vec *, hrng_vec *, const hrng_vec *));
WWHD_GAME_FUNC(0x028E8D88, void, hrng_PSVECAdd,
               (const hrng_vec *, const hrng_vec *, hrng_vec *));
WWHD_GAME_FUNC(0x0200F164, void, hrng_cLib_addCalcPos2,
               (hrng_vec *, const hrng_vec *, f32, f32));
WWHD_GAME_FUNC(0x0200F428, void, hrng_cLib_addCalcAngleS2,
               (s16 *, s16, s16, s16));
WWHD_GAME_FUNC(0x0200FCD8, void, hrng_MtxPosition, (hrng_vec *, hrng_vec *));
WWHD_GAME_FUNC(0x025F1884, void, hrng_cMtx_YrotS, (void *, s16));
WWHD_GAME_FUNC(0x02520540, s32, hrng_dComIfGp_getReverb, (s32));
WWHD_GAME_FUNC(0x025E1A40, void, hrng_mDoAud_seStart,
               (u32, hrng_vec *, u32, s32));
WWHD_GAME_FUNC(0x025E1AA4, void, hrng_monster_sound,
               (u32, hrng_vec *, u32, u32, s32));
WWHD_GAME_FUNC(0x025D6894, s16, hrng_actor_angle, (hrng_actor *, hrng_actor *));
WWHD_GAME_FUNC(0x025D68EC, f64, hrng_actor_distance,
               (hrng_actor *, hrng_actor *));
WWHD_GAME_FUNC(0x025A847C, u32, hrng_particle,
               (u32, u32, u32, const hrng_vec *, const hrng_angle *,
                const hrng_vec *, u32, void *, s32, void *, void *, void *));
WWHD_GAME_FUNC(0x025CB374, void, hrng_shock, (void *, s32, s32, hrng_vec *));
WWHD_GAME_FUNC(0x02065620, void, hrng_anm_init,
               (hrng_actor *, s32, f32, u8, f32, s32, u8));
WWHD_GAME_FUNC(0x02065278, void, hrng_eff_hane_set,
               (hrng_actor *, hrng_vec *, s32, s8));
WWHD_GAME_FUNC(0x02066398, void, hrng_pos_move, (hrng_actor *));
WWHD_GAME_FUNC(0x02066634, void, hrng_ground_move, (hrng_actor *));
WWHD_GAME_FUNC(0x0206A610, void, hrng_fly_attack, (hrng_actor *));
WWHD_GAME_FUNC(0x0206AE64, void, hrng_wind_attack, (hrng_actor *));
WWHD_GAME_FUNC(0x0206B22C, void, hrng_kamen_demo, (hrng_actor *));
WWHD_GAME_FUNC(0x0206B67C, void, hrng_start, (hrng_actor *));
WWHD_GAME_FUNC(0x0206BAA8, void, hrng_end, (hrng_actor *));
WWHD_GAME_FUNC(0x0206C3AC, void, hrng_t_fly, (hrng_actor *));
WWHD_GAME_FUNC(0x0206CAD8, void, hrng_t_lastattack, (hrng_actor *));
WWHD_GAME_FUNC(0x0206D0FC, void, hrng_t_down, (hrng_actor *));
WWHD_GAME_FUNC(0x0206D638, void, hrng_BdkMove_afterFight, (hrng_actor *));
static s16 hrng_fopAcM_searchPlayerAngleY(hrng_actor *a) {
  return hrng_actor_angle(a, hrng_dComIfGp_getPlayer(0));
}
static f64 hrng_fopAcM_searchPlayerDistance(hrng_actor *a) {
  return hrng_actor_distance(a, hrng_dComIfGp_getPlayer(0));
}
static void hrng_bdk_monsSeStart_e(hrng_actor *a, u32 id, u32 param) {
  if ((u32)&a->eyePos) {
    const s32 room = hrng_fopAcM_GetRoomNo(a);
    const u32 pid = a ? a->pid : 0xFFFFFFFFu;
    hrng_monster_sound(id, &a->eyePos, pid, param,
                       hrng_dComIfGp_getReverb(room));
  }
}
static void hrng_bdk_monsSeStart(hrng_actor *a, u32 id, u32 param) {
  if (a && (u32)&a->eyePos) {
    const s32 room = hrng_fopAcM_GetRoomNo(a);
    const u32 pid = a ? a->pid : 0xFFFFFFFFu;
    hrng_monster_sound(id, &a->eyePos, pid, param,
                       hrng_dComIfGp_getReverb(room));
  }
}
static void hrng_bdk_seStart_a(hrng_actor *a, u32 id, u32 param) {
  if (a && (u32)&a->eyePos)
    hrng_mDoAud_seStart(id, &a->eyePos, param,
                        hrng_dComIfGp_getReverb(hrng_fopAcM_GetRoomNo(a)));
}
static void hrng_bdk_StartShock(s32 add) {
  const u32 play = hrng_game();
  const s32 strength = hrng_REG0_S(2) + add;
  hrng_vec direction = {0, 1, 0};
  hrng_shock((void *)(play + 0x599C), strength, -0x21, &direction);
}
static u32 hrng_bdk_particle_setToon(u16 id, const hrng_vec *pos,
                                     const hrng_angle *angle,
                                     const hrng_vec *scale, u8 alpha,
                                     void *callback, s8 room) {
  const u32 control = HRNG_READ_POINTER(hrng_game() + 0x5AB0);
  return hrng_particle(control, 2, id, pos, angle, scale, alpha, callback, room,
                       0, 0, 0);
}
static u32 hrng_particle_full(u16 id, const hrng_vec *pos,
                              const hrng_angle *angle, const hrng_vec *scale,
                              u8 alpha, void *callback, s8 room, void *primary,
                              void *environment) {
  const u32 control = HRNG_READ_POINTER(hrng_game() + 0x5AB0);
  return hrng_particle(control, 0, id, pos, angle, scale, alpha, callback, room,
                       primary, environment, 0);
}
static u32 hrng_particle_basic(u16 id, const hrng_vec *pos,
                               const hrng_angle *angle, const hrng_vec *scale,
                               u8 alpha) {
  return hrng_particle_full(id, pos, angle, scale, alpha, 0, -1, 0, 0);
}
static void hrng_line_construct(hrng_line *line) {
  hrng_line_ctor_native(line);
  for (unsigned i = 0; i < 7; i++)
    line->bytes[0x5C + i] = 0;
  *(volatile unsigned int *)(line->bytes + 0x68) = 1;
  *(volatile u32 *)(line->bytes + 4) = (u32)line + 0x64;
  *(volatile u32 *)(line->bytes) = (u32)line + 0x58;
  *(volatile unsigned int *)(line->bytes + 0x10) = 0x10007F08;
  *(volatile unsigned int *)(line->bytes + 0x64) = 0x10007F28;
  *(volatile unsigned int *)(line->bytes + 0x58) = 0x10007F38;
  *(volatile unsigned int *)(line->bytes + 0x20) = 0x10007F18;
}
static void hrng_BdkMove_destroyLineCheck(hrng_line *line) {
  *(volatile unsigned int *)(line->bytes + 0x58) = 0x10007F38;
  *(volatile unsigned int *)(line->bytes + 0x64) = 0x10007E88;
  *(volatile unsigned int *)(line->bytes + 0x20) = 0x10007E78;
  hrng_line_destroy_native(line, 0);
}
// HD: word copies retain NaN payloads and interleave each load with its store.
static inline void hrng_bdk_vbits(hrng_vec *dst, const hrng_vec *src) {
#pragma clang fp contract(off)
  hrng_copy_float(&dst->x, &src->x);
  hrng_copy_float(&dst->y, &src->y);
  hrng_copy_float(&dst->z, &src->z);
}
static inline f64 hrng_bdk_dist(hrng_actor *i_this, hrng_vec *a, hrng_vec *b) {
#pragma clang fp contract(off)
  (void)i_this;
  hrng_vec d_storage, *d = &d_storage;
  hrng_vec_mi(a, d, b);
  hrng_vec c_storage, *c = &c_storage;
  hrng_bdk_vbits(c, d);
  return hrng_sqrt(hrng_PSVECSquareMag(c));
}
/* fopAcM_seStart + fopAcM_monsSeStart (HD inline pair: one actor and eyePos
 * test) */
static inline void hrng_bdk_se_pair(hrng_actor *a, u32 se, u32 voice) {
#pragma clang fp contract(off)
  if (a != 0 && (u32)(&a->eyePos) != 0) {
    hrng_mDoAud_seStart(se, &a->eyePos, 0,
                        hrng_dComIfGp_getReverb(hrng_fopAcM_GetRoomNo(a)));
    hrng_bdk_monsSeStart_e(a, voice, 0);
  }
}
#define hrng_BDK_EFF27 (i_this->m261C[0x27])

/* inlined into move */
static inline void hrng_fly(hrng_actor *i_this) {
#pragma clang fp contract(off)
  hrng_actor *actor = i_this;
  i_this->mF10 = 1;
  if (i_this->m2CA == -1) {
    hrng_anm_init(i_this, hrng_dRes_INDEX_BDK_BCK_FLY1_e, 10.0f, 2, 1.0f,
                  hrng_dRes_INDEX_BDK_BAS_FLY1_e, 0);
    i_this->m2CA = 0;
  }
  hrng_copy_float(&i_this->m2E4, &hrng_hio_data.m020);
  i_this->m2E8 = hrng_fadds_ppc(hrng_REG0_F(9), 1.0f);
  i_this->m2DC = 600.0f;

  switch ((u16)i_this->mState) {
  case 0: {
    u8 fc;
    if (i_this->m2FC == 0) {
      fc = (u8)hrng_ftoi(hrng_cM_rndF(2.99f));
      i_this->m2FC = fc;
    } else {
      (void)hrng_cM_rndF(1.0f);
      fc = i_this->m2FC;
      if (fc == 1) {
        fc = 2;
        i_this->m2FC = fc;
      } else if (fc == 2) {
        fc = 1;
        i_this->m2FC = fc;
      }
    }
    if (fc == 0) {
      actor->home.pos.x = hrng_fadds_ppc(hrng_REG0_F(0), 6500.0f);
      actor->home.pos.y = hrng_fadds_ppc(hrng_REG0_F(1), 11000.0f);
      actor->home.pos.z = hrng_fadds_ppc(hrng_REG0_F(2), 2600.0f);
      {
        f64 r = hrng_cM_rndFX(hrng_fadds_ppc(hrng_REG0_F(3), 3000.0f));
        i_this->m2CC.x = hrng_fadds_ppc(actor->home.pos.x, r);
      }
      i_this->m2CC.y = hrng_fadds_ppc(actor->home.pos.y, 1000.0f);
      {
        f64 r = hrng_cM_rndFX(hrng_fadds_ppc(hrng_REG0_F(3), 3000.0f));
        i_this->m2CC.z = hrng_fadds_ppc(actor->home.pos.z, r);
      }
    } else if (fc == 1) {
      actor->home.pos.x = hrng_fadds_ppc(hrng_REG0_F(0), 9800.0f);
      actor->home.pos.y = hrng_fadds_ppc(hrng_REG0_F(1), 12000.0f);
      actor->home.pos.z = hrng_fadds_ppc(hrng_REG0_F(2), -1500.0f);
      {
        f64 r = hrng_cM_rndFX(hrng_fadds_ppc(hrng_REG0_F(3), 1000.0f));
        i_this->m2CC.x = hrng_fadds_ppc(actor->home.pos.x, r);
      }
      {
        f64 r = hrng_cM_rndF(1000.0f);
        i_this->m2CC.y = hrng_fadds_ppc(actor->home.pos.y, r);
      }
      {
        f64 r = hrng_cM_rndFX(hrng_fadds_ppc(hrng_REG0_F(3), 1000.0f));
        i_this->m2CC.z = hrng_fadds_ppc(actor->home.pos.z, r);
      }
    } else if (fc == 2) {
      actor->home.pos.x = hrng_fadds_ppc(hrng_REG0_F(0), -1000.0f);
      actor->home.pos.y = hrng_fadds_ppc(hrng_REG0_F(1), 12000.0f);
      actor->home.pos.z = hrng_fadds_ppc(hrng_REG0_F(2), -2900.0f);
      {
        f64 r = hrng_cM_rndFX(hrng_fadds_ppc(hrng_REG0_F(3), 1000.0f));
        i_this->m2CC.x = hrng_fadds_ppc(actor->home.pos.x, r);
      }
      i_this->m2CC.y = hrng_fadds_ppc(actor->home.pos.y, 1000.0f);
      {
        f64 r = hrng_cM_rndFX(hrng_fadds_ppc(hrng_REG0_F(3), 1000.0f));
        i_this->m2CC.z = hrng_fadds_ppc(actor->home.pos.z, r);
      }
    }
    i_this->m2E0 = 0.0f;
    if (hrng_bdk_dist(i_this, &i_this->m2CC, &actor->current.pos) > 1000.0f) {
      i_this->mState = i_this->mState + 1;
      i_this->m2EC[0] = 0;
    }
    break;
  }
  case 1:
    if (hrng_bdk_dist(i_this, &i_this->m2CC, &actor->current.pos) < 1500.0f) {
      if (i_this->m2FC != 0)
        (void)hrng_cM_rndF(1.0f);
      i_this->m2593 = 0;
      i_this->mAction = hrng_ACTION_LANDING;
      i_this->mState = 0;
    }
    break;
  default:
    break;
  }
  hrng_pos_move(i_this);
}

/* inlined into move */
static inline void hrng_up_fly(hrng_actor *i_this) {
#pragma clang fp contract(off)
  hrng_actor *actor = i_this;
  u8 pos_flag = 0;
  switch ((u16)i_this->mState) {
  case 0: {
    hrng_anm_init(i_this, hrng_dRes_INDEX_BDK_BCK_TOBITATU1_e, 5.0f, 0, 1.0f,
                  hrng_dRes_INDEX_BDK_BAS_TOBITATU1_e, 0);
    i_this->m2618 = 0x19;
    hrng_cMtx_YrotS(hrng_calc_mtx(), actor->current.angle.y);
    hrng_vec diff_storage, *diff = &diff_storage;
    hrng_vec out_storage, *out = &out_storage;
    hrng_vec sum_storage, *sum = &sum_storage;
    diff->x = 0.0f;
    diff->y = 2000.0f;
    diff->z = 5000.0f;
    hrng_MtxPosition(diff, out);
    hrng_vec_pl(&actor->current.pos, sum, out);
    actor->speedF = 0.0f;
    hrng_bdk_vbits(&i_this->m2CC, sum);
    i_this->mState = i_this->mState + 1;
    i_this->m2E4 = 0.0f;
    i_this->m2E8 = 1.0f;
    break;
  }
  case 1:
    if (hrng_ftoi(i_this->mpMorf->frame) != hrng_REG0_S(7) + 0x17) {
      pos_flag = 1;
      break;
    }
    i_this->mState = 2;
    i_this->m2E4 = 40.0f;
    actor->speedF = hrng_fadds_ppc(hrng_REG0_F(8), 50.0f);
    actor->current.angle.x = -0x3000;
    i_this->m2E8 = hrng_fadds_ppc(hrng_REG0_F(9), 1.0f);
    /* fall through */
  case 2:
    i_this->m2E0 = 0.0f;
    if (hrng_morf_stopped(i_this->mpMorf)) {
      i_this->mAction = hrng_ACTION_FLY;
      i_this->m2CA = -1;
      i_this->mState = 0;
    }
    break;
  default:
    break;
  }
  if (!pos_flag) {
    hrng_pos_move(i_this);
  }
}

// HD: shared by the transition frame and the following approach frames.
static inline void hrng_landing_approach(hrng_actor *i_this) {
#pragma clang fp contract(off)
  hrng_actor *actor = i_this;
  hrng_cLib_addCalcPos2(&actor->current.pos, &i_this->m2CC, 1.0f,
                        actor->speedF);
  hrng_cLib_addCalcAngleS2(&actor->current.angle.z, 0, 2, 0x800);
  hrng_cLib_addCalcAngleS2(&actor->current.angle.x, 0, 2, 0x800);
  if (hrng_bdk_dist(i_this, &i_this->m2CC, &actor->current.pos) <
      hrng_fadds_ppc(hrng_REG0_F(3), 300.0f)) {
    i_this->mState = 3;
  }
}

/* inlined into move. HD: the final approach moves with hrng_cLib_addCalcPos2 at
 * speedF and starts in the frame the boss gets close; the drop keeps the
 * horizontal speed (damped by 0.9) */
static inline void hrng_landing(hrng_actor *i_this) {
#pragma clang fp contract(off)
  hrng_actor *actor = i_this;
  hrng_actor *player = hrng_dComIfGp_getPlayer(0);
  u8 pos_flag = 0;
  u8 eff_hane_num = 0;
  i_this->mF10 = 1;
  switch ((u16)i_this->mState) {
  case 0xFFFE:
  case 0: {
    s16 angle = hrng_fopAcM_searchPlayerAngleY(actor);
    hrng_cMtx_YrotS(hrng_calc_mtx(), angle);
    hrng_vec diff2_storage, *diff2 = &diff2_storage;
    hrng_vec out_storage, *out = &out_storage;
    hrng_vec sum_storage, *sum = &sum_storage;
    diff2->x = 0.0f;
    diff2->y = 0.0f;
    diff2->z = i_this->mState == 0 ? -800.0f : 0.0f;
    hrng_MtxPosition(diff2, out);
    hrng_vec_pl(&player->current.pos, sum, out);
    hrng_bdk_vbits(&i_this->m2CC, sum);
    if (!hrng_BdkMove_landAreaCheck(&i_this->m2CC, 1400.0f)) {
      i_this->mAction = hrng_ACTION_FLY;
      i_this->mState = 0;
      break;
    }
    i_this->mState = 1;
    i_this->m2EC[0] = 0x32;
    i_this->m2CC.y =
        hrng_fadds_ppc(i_this->m2CC.y, hrng_fadds_ppc(hrng_REG0_F(2), 600.0f));
    i_this->m2DC = 1000.0f;
    i_this->m2E0 = 0.0f;
  }
    /* fall through */
  case 1:
    if (hrng_bdk_dist(i_this, &i_this->m2CC, &actor->current.pos) < 1300.0f) {
      i_this->mState = 2;
      hrng_anm_init(i_this, hrng_dRes_INDEX_BDK_BCK_TYAKUTI1_e, 15.0f, 0,
                    0.001f, hrng_dRes_INDEX_BDK_BAS_TYAKUTI1_e, 0);
      pos_flag = 1;
      // HD: hrng_start the approach on the transition frame, without setting
      // m2E0.
      hrng_landing_approach(i_this);
      break;
    }
    if (i_this->m2EC[0] == 1) {
      hrng_anm_init(i_this, hrng_dRes_INDEX_BDK_BCK_KAKKU1_e, 30.0f, 2, 1.0f,
                    -1, 0);
    }
    break;
  case 2:
    pos_flag = 1;
    hrng_landing_approach(i_this);
    break;
  case 3:
  case 4:
    pos_flag = 1;
    eff_hane_num = 3;
    {
      f32 sz = actor->speed.z;
      f32 sy = actor->speed.y;
      f32 sx = actor->speed.x;
      actor->current.pos.y = hrng_fadds_ppc(actor->current.pos.y, sy);
      actor->speed.y = hrng_fadds_ppc(sy, actor->gravity);
      actor->current.pos.x = hrng_fadds_ppc(actor->current.pos.x, sx);
      actor->current.pos.z = hrng_fadds_ppc(actor->current.pos.z, sz);
      actor->speed.x = hrng_fmuls_ppc(sx, 0.9f);
      actor->speed.z = hrng_fmuls_ppc(sz, 0.9f);
    }
    hrng_cLib_addCalcAngleS2(&actor->current.angle.z, 0, 2, 0x800);
    hrng_cLib_addCalcAngleS2(&actor->current.angle.x, 0, 2, 0x800);
    if (i_this->m2586 != 0 || (i_this->mAcch.flags & 0x20) ||
        !(actor->current.pos.y > hrng_fadds_ppc(i_this->mAcch.ground, 1.0f))) {
      actor->speedF = 0.0f;
      actor->speed.z = 0.0f;
      actor->speed.x = 0.0f;
      actor->speed.y = -10.0f;
      if (i_this->mState == 3) {
        hrng_anm_init(i_this, hrng_dRes_INDEX_BDK_BCK_TYAKUTI1_e, 5.0f, 0, 1.0f,
                      hrng_dRes_INDEX_BDK_BAS_TYAKUTI1_e, 0);
        i_this->mState = 4;
        eff_hane_num = 10;
        hrng_bdk_StartShock(5);
        hrng_particle_full(hrng_ID_IT_SN_DK_TYAKUTI_ROCK00, &actor->current.pos,
                           0, 0, 0xFF, 0, (s8)hrng_fopAcM_GetRoomNo(actor),
                           &i_this->m6224.mColorK0, &i_this->m6224.mColorK0);
        if (i_this->m6078[0] == 0) {
          i_this->m6078[0] = 100;
          hrng_bdk_particle_setToon(
              hrng_ID_IT_ST_DK_TYAKUTI_SMOKE00, &actor->current.pos, 0, 0, 0xB9,
              &i_this->m6080[0], (s8)hrng_fopAcM_GetRoomNo(actor));
        }
      }
    }
    if (hrng_morf_stopped(i_this->mpMorf)) {
      i_this->mAction = hrng_ACTION_WAIT;
      i_this->mState = 0;
      i_this->m2EC[3] =
          (s16)hrng_ftoi(hrng_fadds_ppc(hrng_cM_rndF(600.0f), 600.0f));
    }
    i_this->m2584 = 2;
    break;
  default:
    break;
  }
  if (!pos_flag) {
    hrng_pos_move(i_this);
  }
  if (eff_hane_num != 0) {
    if (eff_hane_num <= 7) {
      if (!(i_this->m2C4 & eff_hane_num)) {
        hrng_eff_hane_set(i_this, &i_this->m1168, 1, 0);
      }
    } else {
      hrng_eff_hane_set(i_this, &i_this->m1168, eff_hane_num, 0);
    }
  }
}

/* HD: a wind attack when the player stands near (4706, -4635) or near a wall */
static inline s32 hrng_wait_wind_check(hrng_actor *i_this, hrng_actor *player) {
#pragma clang fp contract(off)
  f32 dz = hrng_fsubs_ppc(-4635.0f, player->current.pos.z);
  f32 dx = hrng_fsubs_ppc(4706.0f, player->current.pos.x);
  if (hrng_sqrt(hrng_madd(dx, dx, hrng_fmuls_ppc(dz, dz))) < 400.0f) {
    return 1;
  }
  s32 hit = 0;
  hrng_line linChk_storage, *linChk = &linChk_storage;
  hrng_vec start_storage, *hrng_start = &start_storage;
  hrng_vec vec_storage, *vec = &vec_storage;
  hrng_vec end_storage, *hrng_end = &end_storage;
  hrng_line_construct(linChk);
  hrng_copy_float(&hrng_start->x, &player->current.pos.x);
  f32 py = player->current.pos.y;
  hrng_copy_float(&hrng_start->y, &player->current.pos.y);
  hrng_copy_float(&hrng_start->z, &player->current.pos.z);
  vec->y = 0.0f;
  vec->x = 0.0f;
  vec->z = hrng_fadds_ppc(hrng_REG_F(18, 2), 300.0f);
  hrng_start->y = hrng_fadds_ppc(py, 50.0f);
  s16 a = 0;
  for (s32 i = 8; i != 0; i--) {
    hrng_cMtx_YrotS(hrng_calc_mtx(), a);
    hrng_MtxPosition(vec, hrng_end);
    hrng_PSVECAdd(hrng_end, hrng_start, hrng_end);
    hrng_dBgS_LinChk_Set(linChk, hrng_start, hrng_end, i_this);
    if (hrng_line_cross((void *)(hrng_game() + 0x12A0), linChk)) {
      hit = 1;
      break;
    }
    a = (s16)(a + 0x2000);
  }
  hrng_BdkMove_destroyLineCheck(linChk);
  return hit;
}

static inline void hrng_wait_to_up_fly(hrng_actor *i_this) {
#pragma clang fp contract(off)
  i_this->m2594 = 0;
  i_this->m2592 = 0;
  i_this->mAction = hrng_ACTION_UP_FLY;
}

/* inlined into move. HD: turns of more than 0x1000 are made by hopping (height
 * by the angle); a wall hit while walking and the new state 10 (random)
 * hrng_end in a take-off; the wind attack is forced near a fixed point or a
 * wall; m2DC 0 / 100 while turning */
static inline void hrng_wait(hrng_actor *i_this) {
#pragma clang fp contract(off)
  hrng_actor *actor = i_this;
  hrng_actor *player = hrng_dComIfGp_getPlayer(0);
  f64 dist = hrng_fopAcM_searchPlayerDistance(actor);
  i_this->mF10 = 1;
  hrng_bdk_vbits(&i_this->m2CC, &player->current.pos);
  s16 pa = hrng_fopAcM_searchPlayerAngleY(actor);
  s16 angle = (s16)(actor->current.angle.y - pa);
  i_this->m2E4 = 0.0f;
  i_this->m2E8 = 5.0f;
  i_this->m2DC = 500.0f;
  /* pl_view_check (inlined) */
  s32 view;
  {
    s16 pa2 = hrng_fopAcM_searchPlayerAngleY(actor);
    s16 a = (s16)(actor->current.angle.y - pa2);
    if (a < 0) {
      a = (s16)-a;
    }
    view = (u16)a < 0x6000;
  }

  switch ((u16)i_this->mState) {
  case 0:
    if (i_this->m2594 >= 5 || (i_this->m2592 >= 2 && i_this->m8F8 < 4) ||
        !view || i_this->m2EC[3] == 0) {
      i_this->m2594 = 0;
      i_this->m2592 = 0;
      i_this->mAction = hrng_ACTION_UP_FLY;
      break;
    }
    hrng_anm_init(i_this, hrng_dRes_INDEX_BDK_BCK_WAIT1_e, 10.0f, 2, 1.0f, -1,
                  0);
    i_this->mState = 1;
    i_this->m2EC[0] =
        (s16)hrng_ftoi(hrng_fadds_ppc(hrng_cM_rndF(30.0f), 40.0f));
    actor->speedF = 0.0f;
    i_this->m2CA = 0;
    /* fall through */
  case 1:
    i_this->m2DC = 0.0f;
    if ((u32)(angle + 0x1000) >= 0x2001) {
      i_this->mState = 5;
      hrng_anm_init(i_this, hrng_dRes_INDEX_BDK_BCK_JUMP1_e, 5.0f, 0, 0.0f,
                    hrng_dRes_INDEX_BDK_BAS_JUMP1_e, 0);
      s16 aa = angle;
      if (aa < 0) {
        aa = (s16)-aa;
      }
      f32 sy = hrng_fmuls_ppc((f32)aa, 0.005f);
      if (sy > 50.0f) {
        sy = 50.0f;
      }
      actor->speed.y = sy;
      actor->speedF = 0.0f;
      break;
    }
    if (i_this->m2EC[1] == 0) {
      if (!view) {
        i_this->mState = 0;
        break;
      }
      if (angle > 0x800) {
        if (i_this->m2CA != 1) {
          i_this->m2CA = 1;
          hrng_anm_init(i_this, hrng_dRes_INDEX_BDK_BCK_SENKAI_L1_e, 10.0f, 2,
                        1.0f, hrng_dRes_INDEX_BDK_BAS_SENKAI_L1_e, 0);
          i_this->m2EC[1] = 0x14;
        }
      } else if (angle < -0x800) {
        if (i_this->m2CA != 2) {
          i_this->m2CA = 2;
          hrng_anm_init(i_this, hrng_dRes_INDEX_BDK_BCK_SENKAI_R1_e, 10.0f, 2,
                        1.0f, hrng_dRes_INDEX_BDK_BAS_SENKAI_R1_e, 0);
          i_this->m2EC[1] = 0x14;
        }
      } else if (i_this->m2CA != 3) {
        i_this->m2CA = 3;
        hrng_anm_init(i_this, hrng_dRes_INDEX_BDK_BCK_WAIT1_e, 10.0f, 2, 1.0f,
                      -1, 0);
        i_this->m2EC[1] = 0x14;
      }
    }
    if (i_this->m2CA != 3) {
      i_this->m2DC = 100.0f; /* HD: 100 (GameCube hrng_REG0_F(0) + 800) */
    }
    if (i_this->m2EC[0] == 0 && i_this->m2CA == 3) {
      u8 mode = hrng_hio_data.m014;
      if (mode != 0) {
        if (mode == 1) {
          i_this->mAction = hrng_ACTION_KUTI_ATTACK;
        } else if (mode == 2) {
          i_this->mAction = hrng_ACTION_WIND_ATTACK;
        }
        i_this->mState = 0;
      } else {
        i_this->m2592 = i_this->m2592 + 1;
        if (dist < hrng_fadds_ppc(hrng_REG0_F(11), 450.0f)) {
          i_this->mAction = hrng_ACTION_JIDA_ATTACK;
          i_this->mState = 0;
        } else if (dist < hrng_fadds_ppc(hrng_REG0_F(12), 550.0f)) {
          if (hrng_wait_wind_check(i_this, player)) {
            i_this->mAction = hrng_ACTION_WIND_ATTACK;
          } else if (i_this->m8F8 < 2) {
            i_this->mAction = hrng_ACTION_KUTI_ATTACK;
          } else {
            (void)hrng_cM_rndF(1.0f);
            i_this->mAction = hrng_ACTION_KUTI_ATTACK;
          }
          i_this->mState = 0;
        } else if (dist < hrng_fadds_ppc(hrng_REG0_F(15), 1300.0f)) {
          i_this->mState = 2;
          hrng_anm_init(i_this, hrng_dRes_INDEX_BDK_BCK_WALK1_e, 3.0f, 2,
                        hrng_hio_data.m01C, hrng_dRes_INDEX_BDK_BAS_WALK1_e, 0);
          i_this->m2E0 = 0.0f;
          actor->speedF = 0.0f;
        } else {
          f64 r = hrng_cM_rndF(1.0f);
          i_this->mState = 0;
          if (r < 0.5f) {
            i_this->mAction = hrng_ACTION_JUMP;
          } else {
            i_this->mAction = hrng_ACTION_WIND_ATTACK;
          }
        }
      }
    }
    break;
  case 2:
    hrng_copy_float(&i_this->m2E4, &hrng_hio_data.m018);
    i_this->m2E8 = 1.0f;
    if (dist < hrng_fadds_ppc(hrng_REG0_F(12), 530.0f) || dist > 1350.0f) {
      i_this->mState = 1;
    }
    if ((i_this->mAcch.flags & 0x10)) { /* HD */
      hrng_wait_to_up_fly(i_this);
      i_this->mState = 0;
    }
    break;
  case 5:
    i_this->mState = 6; /* HD: the hrng_jump animation is started in state 1 */
                        /* fall through */
  case 6:
    i_this->m2DC = 700.0f;
    if (actor->speed.y < 0.0f &&
        actor->current.pos.y < hrng_fadds_ppc(i_this->mAcch.ground, 5.0f)) {
      hrng_anm_init(i_this, hrng_dRes_INDEX_BDK_BCK_JUMP1_e, 2.0f, 0, 1.0f,
                    hrng_dRes_INDEX_BDK_BAS_JUMP1_e, 0);
      i_this->mState = 7;
    }
    break;
  case 7:
    i_this->m2DC = 0.0f;
    if (hrng_morf_stopped(i_this->mpMorf)) {
      i_this->mState = 1;
      hrng_anm_init(i_this, hrng_dRes_INDEX_BDK_BCK_WAIT1_e, 10.0f, 2, 1.0f, -1,
                    0); /* HD */
    }
    break;
  case 10: /* HD */
    if (hrng_morf_stopped(i_this->mpMorf)) {
      if (hrng_cM_rndF(1.0f) < 0.5f) {
        hrng_wait_to_up_fly(i_this);
        i_this->mState = 0;
      } else {
        i_this->mState = 1;
        hrng_anm_init(i_this, hrng_dRes_INDEX_BDK_BCK_WAIT1_e, 10.0f, 2, 1.0f,
                      -1, 0);
        i_this->m2EC[0] = 0;
        i_this->m2CA = 0;
      }
    }
    break;
  default:
    break;
  }
  i_this->m2586 = 2; /* HD */
  hrng_ground_move(i_this);
}

/* inlined into move */
static inline void hrng_jump(hrng_actor *i_this) {
#pragma clang fp contract(off)
  hrng_actor *actor = i_this;
  hrng_actor *player = hrng_dComIfGp_getPlayer(0);
  i_this->m2E4 = 0.0f;
  i_this->mF10 = 1;
  i_this->m2E8 = 5.0f;
  i_this->m2DC = 500.0f;
  switch ((u16)i_this->mState) {
  case 0:
    hrng_anm_init(i_this, hrng_dRes_INDEX_BDK_BCK_TOBITATU1_e, 5.0f, 0, 1.0f,
                  hrng_dRes_INDEX_BDK_BAS_TOBITATU1_e, 0);
    i_this->m2618 = 0x19;
    i_this->mState = 1;
    hrng_bdk_vbits(&i_this->m2CC, &player->current.pos);
    break;
  case 1: {
    if (hrng_REG0_S(7) + 0x17 != hrng_ftoi(i_this->mpMorf->frame)) {
      break;
    }
    i_this->m2586 = 0;
    i_this->mState = 2;
    actor->speed.y = hrng_fadds_ppc(hrng_REG8_F(9), 50.0f);
    hrng_vec diff_storage, *diff = &diff_storage;
    hrng_vec out_storage, *out = &out_storage;
    hrng_vec sum_storage, *sum = &sum_storage;
    diff->x = 0.0f;
    diff->y = 0.0f;
    diff->z = 10000.0f;
    hrng_cMtx_YrotS(hrng_calc_mtx(), actor->current.angle.y);
    hrng_MtxPosition(diff, out);
    hrng_vec_pl(out, sum, &actor->current.pos);
    hrng_bdk_vbits(&i_this->m2CC, sum);
    i_this->m2EC[0] = (s16)(hrng_REG_S(8, 5) + 0x1E);
  }
    /* fall through */
  case 2:
    i_this->m2E4 = 30.0f;
    i_this->m2E8 = 30.0f;
    actor->speedF = hrng_fadds_ppc(hrng_REG8_F(10), 30.0f);
    if (actor->speed.y < 0.0f) {
      actor->speed.y = 0.0f;
    }
    if (i_this->m2EC[0] == 0) {
      hrng_anm_init(i_this, hrng_dRes_INDEX_BDK_BCK_TYAKUTI1_e, 15.0f, 0,
                    0.001f, hrng_dRes_INDEX_BDK_BAS_TYAKUTI1_e, 0);
      i_this->mAction = hrng_ACTION_LANDING;
      i_this->mState = 3;
    }
    break;
  default:
    break;
  }
  hrng_ground_move(i_this);
}

/* inlined into move */
static inline void hrng_jida_attack(hrng_actor *i_this) {
#pragma clang fp contract(off)
  hrng_actor *actor = i_this;
  f64 dist = hrng_fopAcM_searchPlayerDistance(actor);
  i_this->mF10 = 1;
  switch ((u16)i_this->mState) {
  case 0:
    hrng_anm_init(i_this, hrng_dRes_INDEX_BDK_BCK_JIDANDA1_e, 10.0f, 2, 1.0f,
                  hrng_dRes_INDEX_BDK_BAS_JIDANDA1_e, 0);
    i_this->mState = 1;
    i_this->m2EC[0] =
        (s16)((unsigned int)hrng_ftoi(hrng_cM_rndF(5.0f)) * 0x18u);
    /* fall through */
  case 1: {
    if (!(i_this->m2C4 & 7)) {
      hrng_eff_hane_set(i_this, &i_this->m1168, 1, 0);
    }
    i_this->m2584 = 2;
    s32 frame = hrng_ftoi(i_this->mpMorf->frame);
    if (frame == 1 || frame == 0xD) {
      s32 index = 0;
      if (frame == 0xD) {
        index = 1;
      }
      hrng_vec *p = &i_this->m1174[index];
      hrng_particle_full(hrng_ID_IT_SN_DK_JIDANDA_ROCK00, p, 0, 0, 0xFF, 0,
                         (s8)hrng_fopAcM_GetRoomNo(actor),
                         &i_this->m6224.mColorK0, &i_this->m6224.mColorK0);
      if (i_this->m6078[1] == 0) {
        i_this->m6078[1] = 0xA;
        hrng_bdk_particle_setToon(hrng_ID_IT_ST_DK_JIDANDA_SMOKE00, p, 0, 0,
                                  0xB9, &i_this->m6080[1],
                                  (s8)hrng_fopAcM_GetRoomNo(actor));
      }
    }
    hrng_vec vec1_storage, *vec1 = &vec1_storage;
    if (((unsigned int)frame - 4u) < 8 || ((unsigned int)frame - 0x10u) < 8) {
      hrng_cLib_addCalcAngleS2(&actor->current.angle.y,
                               hrng_fopAcM_searchPlayerAngleY(actor), 8, 0x200);
      vec1->z = hrng_fadds_ppc(hrng_REG0_F(1), 10.0f);
      if (frame == 4 || frame == 0x10) {
        hrng_bdk_StartShock(4);
      }
    } else {
      vec1->z = 0.0f;
    }
    hrng_cMtx_YrotS(hrng_calc_mtx(), hrng_fopAcM_searchPlayerAngleY(actor));
    vec1->x = 0.0f;
    vec1->y = -10.0f;
    hrng_MtxPosition(vec1, &actor->speed);
    hrng_PSVECAdd(&actor->current.pos, &actor->speed, &actor->current.pos);
    if (i_this->m2EC[0] == 0 || dist > 600.0f) {
      i_this->mAction = hrng_ACTION_WAIT;
      i_this->mState = 0;
    }
    break;
  }
  default:
    break;
  }
}

/* inlined into move */
static inline void hrng_kuti_attack(hrng_actor *i_this) {
#pragma clang fp contract(off)
  hrng_actor *actor = i_this;
  hrng_game(); /* HD: an unused play-object read */
  s8 bVar1 = 0;
  s8 bVar2 = 0;
  i_this->m1134 = 2500;
  switch ((u16)i_this->mState) {
  case 0:
    hrng_anm_init(i_this, hrng_dRes_INDEX_BDK_BCK_ATTACK1_e, 5.0f, 0, 1.0f,
                  hrng_dRes_INDEX_BDK_BAS_ATTACK1_e, 0);
    i_this->mState = 2;
    hrng_bdk_monsSeStart(actor, hrng_JA_SE_CV_DK_ATTACK, 0);
    break;
  case 2:
    if (i_this->mpMorf->frame < 36.0f) {
      i_this->m2584 = 1;
    }
    if ((s16)(hrng_REG0_S(5) + 0x1E) == (s16)hrng_ftoi(i_this->mpMorf->frame)) {
      i_this->m1138 = (s16)(hrng_REG0_S(6) + 0x1E);
      i_this->m1136 = 0;
      hrng_bdk_seStart_a(actor, hrng_JA_SE_CM_DK_ATTACK, 0);
      hrng_bdk_StartShock(6);
      hrng_BDK_EFF27.m03C = 0;
      hrng_BDK_EFF27.m000 = 2;
      hrng_cMtx_YrotS(hrng_calc_mtx(), actor->shape_angle.y);
      hrng_vec vec1_storage, *vec1 = &vec1_storage;
      vec1->x = 0.0f;
      vec1->y = 0.0f;
      vec1->z = 500.0f;
      hrng_MtxPosition(vec1, &hrng_BDK_EFF27.m004);
      hrng_PSVECAdd(&hrng_BDK_EFF27.m004, &actor->current.pos,
                    &hrng_BDK_EFF27.m004);
      hrng_particle_basic(hrng_ID_IT_SN_DK_KUTI_SENKO00, &hrng_BDK_EFF27.m004,
                          0, 0, 0xFF);
      hrng_eff_hane_set(i_this, &i_this->m1168, 5, 0);
      bVar2 = 1;
    }
    if (hrng_morf_stopped(i_this->mpMorf)) {
      hrng_anm_init(i_this, hrng_dRes_INDEX_BDK_BCK_NUKENAI1_e, 2.0f, 0, 1.0f,
                    -1, 0);
      i_this->mState = 5;
      i_this->m2EC[0] =
          (s16)hrng_ftoi(hrng_fadds_ppc(hrng_cM_rndF(60.0f), 90.0f));
      hrng_bdk_se_pair(actor, hrng_JA_SE_CM_DK_JITABATA,
                       hrng_JA_SE_CV_DK_NUKEZU);
    }
    break;
  case 3:
    if (hrng_morf_stopped(i_this->mpMorf)) {
      i_this->mAction = hrng_ACTION_WAIT;
      i_this->mState = 0;
    }
    break;
  case 5:
    if (hrng_morf_stopped(i_this->mpMorf)) {
      if (i_this->m2EC[0] == 0) {
        bVar1 = 1;
      } else {
        i_this->mpMorf->rate = -1.0f;
        i_this->mState = 6;
      }
    }
    break;
  case 6:
    if (hrng_morf_stopped(i_this->mpMorf)) {
      i_this->mpMorf->rate = 1.0f;
      i_this->mState = 5;
      hrng_bdk_se_pair(actor, hrng_JA_SE_CM_DK_JITABATA,
                       hrng_JA_SE_CV_DK_NUKEZU);
      hrng_eff_hane_set(i_this, &i_this->m1168, 2, 0);
    }
    break;
  case 10:
    if (hrng_morf_stopped(i_this->mpMorf)) {
      if (i_this->m2EC[0] == 0) {
        bVar1 = 1;
      } else {
        hrng_anm_init(i_this, hrng_dRes_INDEX_BDK_BCK_NUKENAI1_e, 2.0f, 0, 1.0f,
                      -1, 0);
        i_this->mState = 5;
        hrng_bdk_se_pair(actor, hrng_JA_SE_CM_DK_JITABATA,
                         hrng_JA_SE_CV_DK_NUKEZU);
      }
      hrng_eff_hane_set(i_this, &i_this->m1168, 2, 0);
    }
    break;
  default:
    break;
  }
  if (i_this->m2EC[2] == 1 || bVar1 != 0) {
    hrng_anm_init(i_this, hrng_dRes_INDEX_BDK_BCK_NUKU1_e, 1.0f, 0, 1.0f, -1,
                  0);
    i_this->mState = 3;
    hrng_bdk_se_pair(actor, hrng_JA_SE_CM_DK_PULL_UP_HEAD,
                     hrng_JA_SE_CV_DK_NUKERU);
    hrng_BDK_EFF27.m000 = 0;
    bVar2 = 1;
  }
  if (bVar2) {
    if (i_this->m6078[2] == 0) {
      i_this->m6078[2] = 0x3C;
      hrng_bdk_particle_setToon(
          hrng_ID_IT_ST_DK_KUTI_SMOKE00, &hrng_BDK_EFF27.m004, 0, 0, 0xB9,
          &i_this->m6080[2], (s8)hrng_fopAcM_GetRoomNo(actor));
    }
    hrng_particle_full(hrng_ID_IT_SN_DK_KUTI_ROCK00, &hrng_BDK_EFF27.m004, 0, 0,
                       0xFF, 0, (s8)hrng_fopAcM_GetRoomNo(actor),
                       &i_this->m6224.mColorK0, &i_this->m6224.mColorK0);
  }
  actor->current.pos.y = hrng_fadds_ppc(actor->current.pos.y, actor->speed.y);
}

// HD: two debug reports; no t_landing action.
/* 0206D644 */
WWHD_REPLACE(0x0206D644, void, randomizer_helmaroc_fixed_rng_move, (u32 self)) {
#pragma clang fp contract(off)
  hrng_actor *i_this = (hrng_actor *)self;
  hrng_actor *actor = i_this;
  actor->attention_info.flags = 4u;
  hrng_BdkMove_report(0x1E, 0x168,
                      (const char *)(u32)&WWHD_GAME_DATA(0x10007F68, u8),
                      i_this->mAction);
  hrng_BdkMove_report(0x1E, 0x17C,
                      (const char *)(u32)&WWHD_GAME_DATA(0x10007F78, u8),
                      i_this->mState);
  switch ((u16)i_this->mAction) {
  case hrng_ACTION_FLY:
    hrng_fly(i_this);
    break;
  case hrng_ACTION_UP_FLY:
    hrng_up_fly(i_this);
    break;
  case hrng_ACTION_LANDING:
    hrng_landing(i_this);
    break;
  case hrng_ACTION_WAIT:
    hrng_wait(i_this);
    break;
  case hrng_ACTION_JUMP:
    hrng_jump(i_this);
    break;
  case hrng_ACTION_KUTI_ATTACK:
    hrng_kuti_attack(i_this);
    break;
  case hrng_ACTION_JIDA_ATTACK:
    hrng_jida_attack(i_this);
    break;
  case hrng_ACTION_FLY_ATTACK:
    hrng_fly_attack(i_this);
    break;
  case hrng_ACTION_WIND_ATTACK:
    hrng_wind_attack(i_this);
    break;
  case hrng_ACTION_KAMEN_DEMO:
    hrng_kamen_demo(i_this);
    break;
  case hrng_ACTION_END:
    hrng_end(i_this);
    break;
  case hrng_ACTION_START:
    hrng_start(i_this);
    break;
  case hrng_ACTION_T_FLY:
    hrng_t_fly(i_this);
    actor->attention_info.flags = 0;
    break;
  case hrng_ACTION_T_LASTATTACK:
    hrng_t_lastattack(i_this);
    break;
  case hrng_ACTION_T_DOWN:
    hrng_t_down(i_this);
    actor->attention_info.flags = 0;
    break;
  case hrng_ACTION_AFTER_FIGHT:
    hrng_BdkMove_afterFight(i_this);
    actor->attention_info.flags = 0;
    break;
  default:
    break;
  }
  if (i_this->m2591 != 0) {
    if (hrng_morf_stopped(i_this->mpMorf)) {
      hrng_anm_init(i_this, i_this->m2588, 20.0f, i_this->m2590, 1.0f,
                    i_this->m258C, 0);
      i_this->m2591 = 0;
    }
  }
}

#endif
