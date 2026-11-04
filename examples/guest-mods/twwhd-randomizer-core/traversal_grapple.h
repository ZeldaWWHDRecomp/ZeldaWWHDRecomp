/* Complete public CC0 new_himo2_move0216F7FC, a874e1a9. MIT tweaks.cpp
 * changes launch20->10, coil maximum-step base10->20, increment1->6 and
 * shared scale17->25. The startup data observer owns the last change.
 * All rope states, boss demo states and shared camera/stick tail remain. */
/* Guest field views from pinned public CC0 d_a_himo2.h and f_op_actor.h.
 * Union overlays keep unrelated fields out of each declared native subobject.
 */
#pragma once
#include <stdbool.h>
typedef struct {
  f32 x, y, z;
} trg_vec;
typedef struct {
  s16 x, y, z;
} trg_angle;
typedef struct {
  trg_vec pos;
  trg_angle angle;
  s8 room;
  u8 pad;
} trg_place;
typedef struct {
  u16 mCommand, mCondition;
} trg_event;
typedef struct {
  u8 pad[0x10];
  trg_vec m10;
  u8 tail[8];
} trg_segment;
/* A host fixture can separate the field views; production remains a native
 * overlay union, with every offset asserted below. */
#ifndef TRG_FIELD_VIEW
#define TRG_FIELD_VIEW union
#endif
typedef TRG_FIELD_VIEW trg_actor trg_actor;
TRG_FIELD_VIEW trg_actor {
  u8 storage[0x705C];
  struct {
    u8 pad_name[0x8];
    s16 name;
  };
  struct {
    u8 pad_mParameters[0xB0];
    u32 mParameters;
  };
  struct {
    u8 pad___vtbl[0xB4];
    u32 __vtbl;
  };
  struct {
    u8 pad_eventInfo[0xF8];
    trg_event eventInfo;
  };
  struct {
    u8 pad_actor_status[0x2E0];
    u32 actor_status;
  };
  struct {
    u8 pad_home[0x2EC];
    trg_place home;
  };
  struct {
    u8 pad_current[0x314];
    trg_place current;
  };
  struct {
    u8 pad_shape_angle[0x328];
    trg_angle shape_angle;
  };
  struct {
    u8 pad_scale[0x330];
    trg_vec scale;
  };
  struct {
    u8 pad_speed[0x33C];
    trg_vec speed;
  };
  struct {
    u8 pad_speedF[0x370];
    f32 speedF;
  };
  struct {
    u8 pad_eyePos[0x37C];
    trg_vec eyePos;
  };
  struct {
    u8 pad_health[0x3A1];
    s8 health;
  };
  struct {
    u8 pad_m029C[0x3B8];
    s16 m029C;
  };
  struct {
    u8 pad_m029E[0x3BA];
    s16 m029E;
  };
  struct {
    u8 pad_m02A0[0x3BC];
    s16 m02A0;
  };
  struct {
    u8 pad_m02A2[0x3BE];
    s16 m02A2;
  };
  struct {
    u8 pad_m02A4[0x3C0];
    s16 m02A4;
  };
  struct {
    u8 pad_mResetFlg0[0x3C0];
    u32 mResetFlg0;
  };
  struct {
    u8 pad_m02CC[0x3E8];
    s32 m02CC;
  };
  struct {
    u8 pad_m02D8[0x3F4];
    u32 m02D8;
  };
  struct {
    u8 pad_m02DC[0x3F8];
    s32 m02DC;
  };
  struct {
    u8 pad_m02E0[0x3FC];
    s8 m02E0;
  };
  struct {
    u8 pad_m02E4[0x400];
    f32 m02E4;
  };
  struct {
    u8 pad_m02EC[0x408];
    trg_vec m02EC[2];
  };
  struct {
    u8 pad_m0308[0x424];
    s16 m0308;
  };
  struct {
    u8 pad_m0310[0x42C];
    trg_segment m0310[100];
  };
  struct {
    u8 pad_bkMode[0x4A0];
    s16 bkMode;
  };
  struct {
    u8 pad_bkAction[0x4A2];
    s16 bkAction;
  };
  struct {
    u8 pad_unk_40A[0x526];
    u8 unk_40A;
  };
  struct {
    u8 pad_unk_414[0x530];
    f32 unk_414;
  };
  struct {
    u8 pad_unk_4BA[0x5D6];
    s16 unk_4BA;
  };
  struct {
    u8 pad_unk_4CA[0x5E6];
    s16 unk_4CA;
  };
  struct {
    u8 pad_unk_50C[0x628];
    u8 unk_50C;
  };
  struct {
    u8 pad_m2050[0x2550];
    u8 m2050[0x12C];
  };
  struct {
    u8 pad_m217C[0x267C];
    trg_actor *m217C;
  };
  struct {
    u8 pad_m2180[0x2680];
    u32 m2180;
  };
  struct {
    u8 pad_m2184[0x2684];
    f32 m2184;
  };
  struct {
    u8 pad_m2188[0x2688];
    f32 m2188;
  };
  struct {
    u8 pad_m24B4[0x2AE4];
    f32 m24B4;
  };
  struct {
    u8 pad_m24B8[0x2AE8];
    f32 m24B8;
  };
  struct {
    u8 pad_m24BC[0x2AEC];
    s32 m24BC;
  };
  struct {
    u8 pad_m24C0[0x2AF0];
    s32 m24C0;
  };
  struct {
    u8 pad_m24C4[0x2AF4];
    f32 m24C4;
  };
  struct {
    u8 pad_m24C8[0x2AF8];
    s16 m24C8;
  };
  struct {
    u8 pad_m24CA[0x2AFA];
    s16 m24CA;
  };
  struct {
    u8 pad_m24D8[0x2B08];
    s8 m24D8;
  };
  struct {
    u8 pad_m24D9[0x2B09];
    s8 m24D9;
  };
  struct {
    u8 pad_m24DC[0x2B0C];
    trg_vec m24DC;
  };
  struct {
    u8 pad_m24E8[0x2B18];
    trg_vec m24E8;
  };
  struct {
    u8 pad_m24F4[0x2B24];
    f32 m24F4;
  };
  struct {
    u8 pad_m24F8[0x2B28];
    f32 m24F8;
  };
  struct {
    u8 pad_m24FC[0x2B2C];
    f32 m24FC;
  };
  struct {
    u8 pad_m2500[0x2B30];
    f32 m2500;
  };
  struct {
    u8 pad_m2504[0x2B34];
    trg_vec m2504;
  };
  struct {
    u8 pad_m2510[0x2B40];
    s16 m2510;
  };
  struct {
    u8 pad_m2512[0x2B42];
    s16 m2512;
  };
  struct {
    u8 pad_m2514[0x2B44];
    f32 m2514;
  };
  struct {
    u8 pad_m2518[0x2B48];
    f32 m2518;
  };
  struct {
    u8 pad_m251C[0x2B4C];
    s8 m251C;
  };
  struct {
    u8 pad_m2520[0x2B50];
    f32 m2520;
  };
  struct {
    u8 pad_m2524[0x2B54];
    trg_vec m2524;
  };
  struct {
    u8 pad_m2530[0x2B60];
    f32 m2530;
  };
  struct {
    u8 pad_m6190[0x63D4];
    u8 m6190;
  };
  struct {
    u8 pad_m6E15[0x7059];
    u8 m6E15;
  };
};
typedef struct {
  u8 pad[0xDC];
  trg_vec eye;
  u8 padE8[0x248 - 0xE8];
  u8 mCamera[1];
} trg_camera;
typedef struct {
  u8 pad[0x20];
  u32 mFlags;
} trg_attention;
typedef struct {
  u8 pad[0x254];
  u32 mStatus;
} trg_emitter;
typedef union {
  u8 bytes[0x6C];
  u32 alignment;
} trg_line;
typedef struct {
  u32 vtable;
  f32 mFloatRegs[30];
  s16 mShortRegs[10];
} trg_reg_child;
typedef struct {
  u8 pad[4];
  trg_reg_child mChild[22];
} trg_regs;
typedef struct {
  s8 no;
  u8 pad;
  s16 m06, m08, m0A, m0C, m0E;
  f32 m10, m14, m18, m1C, m20;
  u32 vtable;
} trg_hio;
#if defined(__powerpc__)
_Static_assert(__builtin_offsetof(trg_actor, name) == 0x8, "grapple name");
_Static_assert(__builtin_offsetof(trg_actor, mParameters) == 0xB0,
               "grapple mParameters");
_Static_assert(__builtin_offsetof(trg_actor, __vtbl) == 0xB4, "grapple __vtbl");
_Static_assert(__builtin_offsetof(trg_actor, eventInfo) == 0xF8,
               "grapple eventInfo");
_Static_assert(__builtin_offsetof(trg_actor, actor_status) == 0x2E0,
               "grapple actor_status");
_Static_assert(__builtin_offsetof(trg_actor, home) == 0x2EC, "grapple home");
_Static_assert(__builtin_offsetof(trg_actor, current) == 0x314,
               "grapple current");
_Static_assert(__builtin_offsetof(trg_actor, shape_angle) == 0x328,
               "grapple shape_angle");
_Static_assert(__builtin_offsetof(trg_actor, scale) == 0x330, "grapple scale");
_Static_assert(__builtin_offsetof(trg_actor, speed) == 0x33C, "grapple speed");
_Static_assert(__builtin_offsetof(trg_actor, speedF) == 0x370,
               "grapple speedF");
_Static_assert(__builtin_offsetof(trg_actor, eyePos) == 0x37C,
               "grapple eyePos");
_Static_assert(__builtin_offsetof(trg_actor, health) == 0x3A1,
               "grapple health");
_Static_assert(__builtin_offsetof(trg_actor, m029C) == 0x3B8, "grapple m029C");
_Static_assert(__builtin_offsetof(trg_actor, m029E) == 0x3BA, "grapple m029E");
_Static_assert(__builtin_offsetof(trg_actor, m02A0) == 0x3BC, "grapple m02A0");
_Static_assert(__builtin_offsetof(trg_actor, m02A2) == 0x3BE, "grapple m02A2");
_Static_assert(__builtin_offsetof(trg_actor, m02A4) == 0x3C0, "grapple m02A4");
_Static_assert(__builtin_offsetof(trg_actor, mResetFlg0) == 0x3C0,
               "grapple mResetFlg0");
_Static_assert(__builtin_offsetof(trg_actor, m02CC) == 0x3E8, "grapple m02CC");
_Static_assert(__builtin_offsetof(trg_actor, m02D8) == 0x3F4, "grapple m02D8");
_Static_assert(__builtin_offsetof(trg_actor, m02DC) == 0x3F8, "grapple m02DC");
_Static_assert(__builtin_offsetof(trg_actor, m02E0) == 0x3FC, "grapple m02E0");
_Static_assert(__builtin_offsetof(trg_actor, m02E4) == 0x400, "grapple m02E4");
_Static_assert(__builtin_offsetof(trg_actor, m02EC) == 0x408, "grapple m02EC");
_Static_assert(__builtin_offsetof(trg_actor, m0308) == 0x424, "grapple m0308");
_Static_assert(__builtin_offsetof(trg_actor, m0310) == 0x42C, "grapple m0310");
_Static_assert(__builtin_offsetof(trg_actor, bkMode) == 0x4A0,
               "grapple bkMode");
_Static_assert(__builtin_offsetof(trg_actor, bkAction) == 0x4A2,
               "grapple bkAction");
_Static_assert(__builtin_offsetof(trg_actor, unk_40A) == 0x526,
               "grapple unk_40A");
_Static_assert(__builtin_offsetof(trg_actor, unk_414) == 0x530,
               "grapple unk_414");
_Static_assert(__builtin_offsetof(trg_actor, unk_4BA) == 0x5D6,
               "grapple unk_4BA");
_Static_assert(__builtin_offsetof(trg_actor, unk_4CA) == 0x5E6,
               "grapple unk_4CA");
_Static_assert(__builtin_offsetof(trg_actor, unk_50C) == 0x628,
               "grapple unk_50C");
_Static_assert(__builtin_offsetof(trg_actor, m2050) == 0x2550, "grapple m2050");
_Static_assert(__builtin_offsetof(trg_actor, m217C) == 0x267C, "grapple m217C");
_Static_assert(__builtin_offsetof(trg_actor, m2180) == 0x2680, "grapple m2180");
_Static_assert(__builtin_offsetof(trg_actor, m2184) == 0x2684, "grapple m2184");
_Static_assert(__builtin_offsetof(trg_actor, m2188) == 0x2688, "grapple m2188");
_Static_assert(__builtin_offsetof(trg_actor, m24B4) == 0x2AE4, "grapple m24B4");
_Static_assert(__builtin_offsetof(trg_actor, m24B8) == 0x2AE8, "grapple m24B8");
_Static_assert(__builtin_offsetof(trg_actor, m24BC) == 0x2AEC, "grapple m24BC");
_Static_assert(__builtin_offsetof(trg_actor, m24C0) == 0x2AF0, "grapple m24C0");
_Static_assert(__builtin_offsetof(trg_actor, m24C4) == 0x2AF4, "grapple m24C4");
_Static_assert(__builtin_offsetof(trg_actor, m24C8) == 0x2AF8, "grapple m24C8");
_Static_assert(__builtin_offsetof(trg_actor, m24CA) == 0x2AFA, "grapple m24CA");
_Static_assert(__builtin_offsetof(trg_actor, m24D8) == 0x2B08, "grapple m24D8");
_Static_assert(__builtin_offsetof(trg_actor, m24D9) == 0x2B09, "grapple m24D9");
_Static_assert(__builtin_offsetof(trg_actor, m24DC) == 0x2B0C, "grapple m24DC");
_Static_assert(__builtin_offsetof(trg_actor, m24E8) == 0x2B18, "grapple m24E8");
_Static_assert(__builtin_offsetof(trg_actor, m24F4) == 0x2B24, "grapple m24F4");
_Static_assert(__builtin_offsetof(trg_actor, m24F8) == 0x2B28, "grapple m24F8");
_Static_assert(__builtin_offsetof(trg_actor, m24FC) == 0x2B2C, "grapple m24FC");
_Static_assert(__builtin_offsetof(trg_actor, m2500) == 0x2B30, "grapple m2500");
_Static_assert(__builtin_offsetof(trg_actor, m2504) == 0x2B34, "grapple m2504");
_Static_assert(__builtin_offsetof(trg_actor, m2510) == 0x2B40, "grapple m2510");
_Static_assert(__builtin_offsetof(trg_actor, m2512) == 0x2B42, "grapple m2512");
_Static_assert(__builtin_offsetof(trg_actor, m2514) == 0x2B44, "grapple m2514");
_Static_assert(__builtin_offsetof(trg_actor, m2518) == 0x2B48, "grapple m2518");
_Static_assert(__builtin_offsetof(trg_actor, m251C) == 0x2B4C, "grapple m251C");
_Static_assert(__builtin_offsetof(trg_actor, m2520) == 0x2B50, "grapple m2520");
_Static_assert(__builtin_offsetof(trg_actor, m2524) == 0x2B54, "grapple m2524");
_Static_assert(__builtin_offsetof(trg_actor, m2530) == 0x2B60, "grapple m2530");
_Static_assert(__builtin_offsetof(trg_actor, m6190) == 0x63D4, "grapple m6190");
_Static_assert(__builtin_offsetof(trg_actor, m6E15) == 0x7059, "grapple m6E15");
_Static_assert(__builtin_offsetof(trg_camera, eye) == 0xDC, "camera eye");
_Static_assert(__builtin_offsetof(trg_camera, mCamera) == 0x248, "camera body");
_Static_assert(sizeof(trg_hio) == 0x24, "rope HIO");
_Static_assert(sizeof(trg_reg_child) == 0x90, "register stride");
_Static_assert(sizeof(trg_segment) == 0x24, "rope segment stride");
#endif
WWHD_GAME_FUNC(0x025200D4, u32, trg_game, (void));
#ifndef TRG_READ_POINTER
#define TRG_READ_POINTER(address) (*(volatile u32 *)(address))
#endif
WWHD_GAME_FUNC(0x028F4384, f64, trg_sqrt, (f64));
WWHD_GAME_FUNC(0x025B8B94, s32, trg_event_test_native, (u32, u16));
WWHD_GAME_FUNC(0x025B8B68, void, trg_event_set_native, (u32, u16));
#define trg_l_himo2Regs WWHD_GAME_DATA(0x1047B608, trg_regs)
#define trg_l_himo2HIO WWHD_GAME_DATA(0x1046469C, trg_hio)
#define trg_rope_scale WWHD_GAME_DATA(0x10464674, f32)
#define trg_himo2_btd ((trg_actor *)(u32)WWHD_GAME_DATA(0x10464678, u32))
#define trg_himo2_dr ((trg_actor *)(u32)WWHD_GAME_DATA(0x1046467C, u32))
#define trg_l_guideEmitter ((trg_emitter *)(u32)WWHD_GAME_DATA(0x10475628, u32))
static trg_actor *trg_dComIfGp_getPlayer(s32 index) {
  return (trg_actor *)(u32)TRG_READ_POINTER(trg_game() + 0x5B2C + 8 * index);
}
static trg_actor *trg_daPy_getPlayerLinkActorClass(void) {
  return (trg_actor *)(u32)TRG_READ_POINTER(trg_game() + 0x5B34);
}
static s8 trg_dComIfGp_getPlayerCameraID(s32 index) {
  return *(volatile s8 *)(trg_game() + 0x5B30 + 8 * index);
}
static trg_camera *trg_dComIfGp_getCamera(s32 index) {
  return (trg_camera *)(u32)TRG_READ_POINTER(trg_game() + 0x4858 +
                                             0x34 * index);
}
static u32 trg_dComIfGp_ea(void) { return trg_game(); }
static u32 trg_stage(void) { return trg_game() + 0x5150; }
static void *trg_calc_mtx(void) {
  return (void *)(u32)WWHD_GAME_DATA(0x1018C7B0, u32);
}
static u32 trg_fopAcM_GetID(trg_actor *a) {
  return a ? *(volatile u32 *)((u32)a + 4) : 0xFFFFFFFFu;
}
static u32 trg_fopAcM_GetParam(trg_actor *a) { return a->mParameters; }
static s8 trg_fopAcM_GetRoomNo(trg_actor *a) { return a->current.room; }
static s16 trg_fpcM_GetName(trg_actor *a) { return a->name; }
static s32 trg_dComIfGs_isEventBit(u16 flag) {
  return trg_event_test_native(WWHD_GAME_DATA(0x101F84DC, u32) + 0x644, flag);
}
static void trg_dComIfGs_onEventBit(u16 flag) {
  trg_event_set_native(WWHD_GAME_DATA(0x101F84DC, u32) + 0x644, flag);
}
static void trg_event_reset(void *event) {
  *(volatile u16 *)((u32)event + 0xE8) |= 8;
}
static s32 trg_sub32(unsigned int a, unsigned int b) { return (s32)(a - b); }
static s32 trg_ftoi(f64 value) {
  if (value != value || value <= -2147483648.0)
    return (s32)0x80000000u;
  if (value >= 2147483648.0)
    return 0x7FFFFFFF;
  return (s32)value;
}
static f32 trg_mul(f64 a, f64 b) {
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
static f32 trg_madd(f32 a, f32 b, f32 c) {
#pragma clang fp contract(fast)
  return a * b + c;
}
static f32 trg_nmsub(f32 a, f32 b, f32 c) {
#pragma clang fp contract(fast)
  volatile f32 difference = a * b - c;
  return -difference;
}
static u32 trg_player_virtual(trg_actor *player, u32 offset) {
  return ((u32 (*)(trg_actor *))(u32) *
          (volatile u32 *)(player->__vtbl + offset))(player);
}
static f32 trg_cM_ssin(s32 angle) {
  return (
      (volatile f32 *)&WWHD_GAME_DATA(0x104A44F8, u8))[((u16)angle >> 3) * 2];
}
static f32 trg_cM_scos(s32 angle) {
  return ((volatile f32 *)&WWHD_GAME_DATA(0x104A44F8,
                                          u8))[((u16)angle >> 3) * 2 + 1];
}
enum { trg_fopAcStts_UNK4000_e = 0x4000 };
enum {
  trg_HIMO2_DEMO_REQUEST = -1,
  trg_HIMO2_DEMO_INACTIVE = 0,
  trg_HIMO2_DEMO_COIL = 2,
  trg_HIMO2_DEMO_SET_ROCK = 3,
  trg_HIMO2_DEMO_DROP_ROCK = 4,
  trg_HIMO2_DEMO_CONTROL = 5,
  trg_HIMO2_DEMO_JUMP = 6,
  trg_HIMO2_DEMO_RESET = 7,
  trg_HIMO2_DEMO_BOSS_FALL = 8,
  trg_HIMO2_DEMO_BOSS_END = 9
};
enum {
  trg_HIMO2_WAIT = 0,
  trg_HIMO2_SPIN = 1,
  trg_HIMO2_LAUNCH_FREE = 2,
  trg_HIMO2_LAUNCH_TARGET = 3,
  trg_HIMO2_HOOK_BODY = 4,
  trg_HIMO2_PULL_BACK = 5,
  trg_HIMO2_MAX_LENGTH = 8,
  trg_HIMO2_REBOUND = 9,
  trg_HIMO2_HOOKED = 10,
  trg_HIMO2_HANG = 11
};
WWHD_GAME_FUNC(0x020078E8, u32, trg_himo2_padTrigX, (u32));
WWHD_GAME_FUNC(0x02007914, u32, trg_himo2_padTrigY, (u32));
WWHD_GAME_FUNC(0x02007814, u32, trg_himo2_padTrigZ, (u32));
WWHD_GAME_FUNC(0x02007B20, f64, trg_himo2_subStickY, (u32));
WWHD_GAME_FUNC(0x02007AFC, f64, trg_himo2_subStickX, (u32));
WWHD_GAME_FUNC(0x02514F2C, void, trg_himo2_cameraStop, (void *));
WWHD_GAME_FUNC(0x02514F38, void, trg_himo2_cameraStart, (void *));
WWHD_GAME_FUNC(0x02515280, void, trg_himo2_cameraTrim, (void *, s32));
WWHD_GAME_FUNC(0x0251510C, void, trg_himo2_cameraReset,
               (void *, trg_vec *, trg_vec *));
WWHD_GAME_FUNC(0x02514FE8, void, trg_himo2_cameraSet,
               (void *, trg_vec *, trg_vec *, s16, f32));
WWHD_GAME_FUNC(0x0255FDA4, void, trg_himo2_customColorSet, (u32, u32, f32));
WWHD_GAME_FUNC(0x025D7B24, void, trg_himo2_orderPotentialEvent,
               (trg_actor *, u16, u16, u32));
WWHD_GAME_FUNC(0x0216E144, void, trg_himo2_playerPosExternal, (trg_actor *));
WWHD_GAME_FUNC(0x0216DE7C, s32, trg_himo2_bgCheckExternal, (trg_actor *));
WWHD_GAME_FUNC(0x024EDFCC, u32, trg_himo2_LockonTruth, (u32));
WWHD_GAME_FUNC(0x024EC8D0, u32, trg_himo2_LockonTarget, (u32, s32));
WWHD_GAME_FUNC(0x02008B4C, void, trg_himo2_destroyLineCheck, (trg_line *, s32));
WWHD_GAME_FUNC(0x02520540, s32, trg_dComIfGp_getReverb, (s32 room));
WWHD_GAME_FUNC(0x025E1A40, void, trg_mDoAud_seStart,
               (u32 id, trg_vec *pos, u32 param, s32 reverb));
WWHD_GAME_FUNC(0x020195B0, s16, trg_cM_atan2s, (f64 y, f64 x));
WWHD_GAME_FUNC(0x0200F428, void, trg_cLib_addCalcAngleS2,
               (s16 * value, s16 target, s16 scale, s16 maximum));
WWHD_GAME_FUNC(0x0200ED84, void, trg_cLib_addCalc2,
               (f32 * value, f32 target, f32 scale, f32 maximum));
WWHD_GAME_FUNC(0x0200EDC8, void, trg_cLib_addCalc0,
               (f32 * value, f32 scale, f32 maximum));
WWHD_GAME_FUNC(0x0200F164, void, trg_cLib_addCalcPos2,
               (trg_vec * value, const trg_vec *target, f32 scale,
                f32 maximum));
WWHD_GAME_FUNC(0x025F1884, void, trg_mDoMtx_YrotS, (void *matrix, s16 angle));
WWHD_GAME_FUNC(0x025F1BF4, void, trg_mDoMtx_XrotM, (void *matrix, s16 angle));
WWHD_GAME_FUNC(0x025F1C28, void, trg_mDoMtx_YrotM, (void *matrix, s16 angle));
WWHD_GAME_FUNC(0x025F1C5C, void, trg_mDoMtx_ZrotM, (void *matrix, s16 angle));
WWHD_GAME_FUNC(0x0200FCD8, void, trg_MtxPosition,
               (trg_vec * src, trg_vec *dest));
WWHD_GAME_FUNC(0x028E8DD0, f64, trg_PSVECSquareMag, (trg_vec * vector));
WWHD_GAME_FUNC(0x028E8D88, void, trg_PSVECAdd,
               (trg_vec * a, trg_vec *b, trg_vec *out));
WWHD_GAME_FUNC(0x0201ADE0, void, trg_vec_sub,
               (const trg_vec *a, trg_vec *out, const trg_vec *b));
WWHD_GAME_FUNC(0x0201AD78, void, trg_vec_add,
               (const trg_vec *a, trg_vec *out, const trg_vec *b));
WWHD_GAME_FUNC(0x025D5218, trg_actor *, trg_fopAcIt_Judge,
               (u32 callback, u32 *id));
WWHD_GAME_FUNC(0x025D6894, s16, trg_player_angle,
               (trg_actor * a, trg_actor *player));
WWHD_GAME_FUNC(0x025160DC, s32, trg_attack_hit, (void *sphere));
WWHD_GAME_FUNC(0x02008FEC, void, trg_line_native_ctor, (trg_line * line));
WWHD_GAME_FUNC(0x024F1AFC, void, trg_line_set,
               (trg_line * line, trg_vec *a, trg_vec *b, trg_actor *actor));
WWHD_GAME_FUNC(0x02008860, s32, trg_line_cross,
               (void *background, trg_line *line));
static s16 trg_fopAcM_searchPlayerAngleY(trg_actor *a) {
  return trg_player_angle(a, trg_dComIfGp_getPlayer(0));
}
static void trg_line_construct(trg_line *line) {
  trg_line_native_ctor(line);
  for (unsigned i = 0; i < 7; i++)
    line->bytes[0x5C + i] = 0;
  *(volatile unsigned int *)(line->bytes + 0x68) = 1;
  *(volatile u32 *)(line->bytes + 4) = (u32)line + 0x64;
  *(volatile u32 *)line->bytes = (u32)line + 0x58;
  *(volatile unsigned int *)(line->bytes + 0x10) = 0x10010EA4;
  *(volatile unsigned int *)(line->bytes + 0x64) = 0x10010EC4;
  *(volatile unsigned int *)(line->bytes + 0x58) = 0x10010ED4;
  *(volatile unsigned int *)(line->bytes + 0x20) = 0x10010EB4;
}
enum {
  trg_JA_SE_LK_ROPE_LAUNCH = 0x2817,
  trg_JA_SE_LK_ROPE_COIL_1 = 0x2818,
  trg_JA_SE_LK_ROPE_COIL_2 = 0x2819,
  trg_JA_SE_LK_ROPE_COIL_3 = 0x281A,
  trg_JA_SE_LK_ROPE_HOOK_WOOD = 0x281B,
  trg_JA_SE_LK_ROPE_MAXLENGTH = 0x2820,
  trg_JA_SE_LK_ROPE_UNCOIL = 0x2821,
  trg_JA_SE_LK_ROPE_HOOK_BODY = 0x2830,
  trg_JA_SE_LK_ROPE_SWING_ROUND = 0x2842,
  trg_JA_SE_LK_ROPE_UNWIND = 0x201F,
  trg_JA_SE_CV_DRG_ROCKFALL_1 = 0x483F,
  trg_JA_SE_CV_DRG_ROCKFALL_1_2 = 0x4844,
  trg_JA_SE_CV_DRG_ROCKFALL_1_3 = 0x4845,
  trg_JA_SE_CV_DRG_ROCKFALL_2 = 0x4840,
  trg_JA_SE_CV_DRG_SET_ROPE = 0x4841,
  trg_JA_SE_CM_BTD_ROPE_SET = 0x5827,
  trg_JA_SE_CM_BTD_BEF_ROCK_FALL = 0x5829,
  trg_fpcNm_KUI_e = 0xFA,
  trg_fpcNm_BK_e = 0xBD,
};

// HD: unit-local adapters retain the original argument and result widths.
static inline u32 trg_mv_LockonTruth(u32 att) {
  return trg_himo2_LockonTruth(att);
}
// dAttention_c::LockonTarget(s32) (matcher: ActionTarget)
static inline u32 trg_mv_LockonTarget(u32 att, s32 i) {
  return trg_himo2_LockonTarget(att, i);
}
static inline u32 trg_mv_padTrigX() { return trg_himo2_padTrigX(0); }
static inline u32 trg_mv_padTrigY() { return trg_himo2_padTrigY(0); }
static inline u32 trg_mv_padTrigZ() { return trg_himo2_padTrigZ(0); }
static inline f64 trg_mv_subStickY() { return trg_himo2_subStickY(0); }
static inline f64 trg_mv_subStickX() { return trg_himo2_subStickX(0); }
static inline void trg_mv_camStop(trg_camera *cam) {
  trg_himo2_cameraStop(cam->mCamera);
}
static inline void trg_mv_camStart(trg_camera *cam) {
  trg_himo2_cameraStart(cam->mCamera);
}
static inline void trg_mv_camSetTrimSize(trg_camera *cam, s32 s) {
  trg_himo2_cameraTrim(cam->mCamera, s);
}
static inline void trg_mv_camReset(trg_camera *cam, trg_vec *c, trg_vec *e) {
  trg_himo2_cameraReset(cam->mCamera, c, e);
}
static inline void trg_mv_camSet(trg_camera *cam, trg_vec *c, trg_vec *e,
                                 s16 fovy_bank, f32 f) {
  trg_himo2_cameraSet(cam->mCamera, c, e, fovy_bank, f);
}
static inline void trg_mv_colset(f32 f) {
  trg_himo2_customColorSet(0, 4, f);
} /* dKy_custom_colset(0, 4, f) */
static inline void trg_mv_orderPotentialEvent(trg_actor *a, u16 flag) {
  trg_himo2_orderPotentialEvent(a, flag, (u16)0xFFFF, 0);
}
static inline trg_actor *trg_mv_player() { return trg_dComIfGp_getPlayer(0); }
/* fopAcM_seStart (HD: checks &eyePos != NULL; the actor pointer itself is not
 * checked) */
static inline void trg_mv_seStart(trg_actor *a, u32 id) {
  if ((u32)(a) + 0x37C != 0)
    trg_mDoAud_seStart(id, &a->eyePos, 0,
                       trg_dComIfGp_getReverb(trg_fopAcM_GetRoomNo(a)));
}
/* fopAcM_seStart(player, ...) (HD: checks the player and its eyePos) */
static inline void trg_mv_seStartP(trg_actor *a, u32 id) {
  if (a != 0 && (u32)(a) + 0x37C != 0)
    trg_mDoAud_seStart(id, &a->eyePos, 0,
                       trg_dComIfGp_getReverb(trg_fopAcM_GetRoomNo(a)));
}
/* fopAcM_seStartCurrent (HD: checks the actor and its position) */
static inline void trg_mv_seStartCurrent(trg_actor *a, u32 id) {
  if (a != 0 && (u32)(a) + 0x314 != 0)
    trg_mDoAud_seStart(id, &a->current.pos, 0,
                       trg_dComIfGp_getReverb(trg_fopAcM_GetRoomNo(a)));
}
static inline void trg_mv_seStart0(u32 id) { trg_mDoAud_seStart(id, 0, 0, 0); }
/* btd (0x10464678) and dr (0x1046467C) fields used here */
static inline void trg_btd_setBlur(u8 v) {
  trg_himo2_btd->m6E15 = v;
} /* btd->m6E15 */
static inline void trg_btd_onStatus4000() {
  trg_actor *b = trg_himo2_btd;
  b->actor_status |= trg_fopAcStts_UNK4000_e;
}
static inline void trg_btd_offStatus4000() {
  trg_actor *b = trg_himo2_btd;
  b->actor_status &= ~(u32)trg_fopAcStts_UNK4000_e;
}
/* daYkgr_c::hide / show: the ykgr instance at 0x10475628, flags +0x254 */
static inline void trg_ykgr_hide() {
  trg_emitter *emitter = trg_l_guideEmitter;
  if (emitter)
    emitter->mStatus |= 4;
}
static inline void trg_ykgr_show() {
  trg_emitter *emitter = trg_l_guideEmitter;
  if (emitter)
    emitter->mStatus &= ~4u;
}
static inline f64 trg_mv_abs(trg_vec *v) {
  return trg_sqrt(trg_PSVECSquareMag(v));
}
static inline void trg_mv_pl_pos_add(trg_actor *i_this) {
  trg_himo2_playerPosExternal(i_this);
}
static inline s32 trg_mv_bg_check(trg_actor *i_this) {
  return trg_himo2_bgCheckExternal(i_this);
}
static inline void trg_mv_posMove(trg_actor *i_this) {
  /* speed = Rotation(angle) * (0, 0, speedF); pos += speed */
  trg_vec sp130_storage, *sp130 = &sp130_storage;
  sp130->x = 0.0f;
  sp130->y = 0.0f;
  sp130->z = i_this->speedF;
  trg_MtxPosition(sp130, &i_this->speed);
  trg_PSVECAdd(&i_this->current.pos, &i_this->speed,
               &i_this->current.pos); /* trg_PSVECAdd */
}
/* |pos - sp100| (trg_vec::operator- then abs of the copy) */
static inline f64 trg_mv_dist(trg_actor *i_this, trg_vec *sp100) {
  trg_vec tmp_storage, *tmp = &tmp_storage;
  trg_vec_sub(&i_this->current.pos, tmp, &*sp100);
  trg_vec sp130_storage, *sp130 = &sp130_storage;
  *sp130 = *tmp;
  return trg_mv_abs(sp130);
}
/* fopAcM_SearchByID (HD inline): trg_fopAcIt_Judge(fpcSch_JudgeByID, &id)
 * unless id == -1 */
static inline trg_actor *trg_mv_searchByID(u32 id) {
  u32 pid_storage, *pid = &pid_storage;
  *pid = id;
  if (id == 0xFFFFFFFF)
    return 0;
  return trg_fopAcIt_Judge(0x025E1234, pid);
}

/* cases 5/8/9: pull the hook back to the hand */
static inline bool trg_mv_pull_back(trg_actor *i_this, trg_vec *sp100) {
  f32 step =
      (trg_l_himo2Regs.mChild[18].mFloatRegs[13] + 400.0f) * i_this->m2500;
  trg_cLib_addCalcPos2(&i_this->current.pos, sp100, 1.0f, step);
  trg_cLib_addCalc2(&i_this->m2500, 1.0f, 1.0f,
                    trg_l_himo2Regs.mChild[18].mFloatRegs[14] + 0.1f);
  trg_mv_pl_pos_add(i_this);
  return trg_mv_dist(i_this, sp100) > 5.0f;
}

/* the rope wound in the spin (cases 0 and 1) */
static inline void trg_mv_spin(trg_actor *i_this, trg_actor *player,
                               trg_vec *sp100) {
  trg_mDoMtx_YrotS(trg_calc_mtx(), player->shape_angle.y);
  trg_mDoMtx_ZrotM(trg_calc_mtx(),
                   (s16)(trg_l_himo2Regs.mChild[0].mShortRegs[2] + -12000));
  trg_mDoMtx_YrotM(
      trg_calc_mtx(),
      (s16)((unsigned int)i_this->m02D8 *
            (unsigned int)(trg_l_himo2Regs.mChild[0].mShortRegs[3] + 0x2000)));
  trg_vec sp130_storage, *sp130 = &sp130_storage;
  sp130->x = 0.0f;
  sp130->y = 0.0f;
  sp130->z = trg_l_himo2Regs.mChild[0].mFloatRegs[11] + 70.0f;
  trg_MtxPosition(sp130, &i_this->current.pos);
  trg_PSVECAdd(&i_this->current.pos, sp100,
               &i_this->current.pos); /* trg_PSVECAdd */
  i_this->m02CC = trg_l_himo2Regs.mChild[0].mShortRegs[5] + 0x60;
}

/* 0216F7FC */
WWHD_REPLACE(0x0216F7FC, void, randomizer_grapple_move, (u32 self)) {
#pragma clang fp contract(off)
  trg_actor *i_this = (trg_actor *)self;
  trg_actor *player = (trg_actor *)trg_mv_player();
  s8 camId =
      trg_dComIfGp_getPlayerCameraID(0); /* trg_dComIfGp_getPlayerCameraID(0) */
  u32 play = trg_game();
  f32 sz = i_this->m02EC[0].z;
  f32 sx = i_this->m02EC[0].x;
  f32 sy = i_this->m02EC[0].y;
  trg_camera *camera =
      (trg_camera *)(u32)(TRG_READ_POINTER(play + 0x4858 + 0x34 * (s32)camId));
  trg_vec sp100_storage, *sp100 = &sp100_storage;
  sp100->x = sx;
  sp100->y = sy;
  sp100->z = sz;
  f32 dy = i_this->current.pos.y - sy;
  f32 dx = i_this->current.pos.x - sx;
  f32 dz = i_this->current.pos.z - sz;
  i_this->m2188 = trg_sqrt(trg_madd(dz, dz, trg_madd(dx, dx, dy * dy)));
  if (i_this->m0308 != 0)
    i_this->m0308 = i_this->m0308 - 1;
  if (i_this->m02A2 != 0)
    i_this->m02A2 = i_this->m02A2 - 1;
  if (i_this->m029C != 0)
    i_this->m029C = i_this->m029C - 1;
  if (i_this->m029E != 0)
    i_this->m029E = i_this->m029E - 1;
  if (i_this->m02A0 != 0)
    i_this->m02A0 = i_this->m02A0 - 1;

  u32 att = trg_dComIfGp_ea() + 0x5804; /* dComIfGp_getAttention() */
  bool lockon = true;
  if (trg_mv_LockonTruth(att) == 0 &&
      ((u32)((trg_attention *)(u32)att)->mFlags & 0x20000000) == 0)
    lockon = false;
  bool r30 = !lockon; /* attention->Lockon() == false */
  bool r27 = false;   /* a lock-on target */
  bool r25 = false;
  trg_actor *r24 = 0;
  if (r30) {
    trg_rope_scale = trg_l_himo2HIO.m20;
  } else {
    trg_rope_scale = trg_l_himo2HIO.m1C;
    i_this->m029E = 0;
  }
  if (trg_mv_LockonTruth(att) != 0) {
    r24 = (trg_actor *)(u32)(trg_mv_LockonTarget(att, 0));
    if (r24 != 0) {
      i_this->m2524 = r24->eyePos;
      r27 = true;
    }
  }
  if (i_this->m02DC != trg_HIMO2_WAIT &&
      i_this->m24D9 < trg_HIMO2_DEMO_SET_ROCK) {
    trg_actor *link =
        trg_daPy_getPlayerLinkActorClass(); /* trg_daPy_getPlayerLinkActorClass()
                                             */
    /* checkRopeForceEnd() && !eventInfo.checkCommandDemoAccrpt() */
    if (((u32)link->mResetFlg0 & 0x40000000u) &&
        !(i_this->eventInfo.mCommand == 2)) {
      i_this->m24D9 = trg_HIMO2_DEMO_INACTIVE;
      i_this->m0308 = 30;
      s32 dc = i_this->m02DC;
      i_this->mParameters = 0;
      i_this->m24D8 = 0;
      if (dc == 11) {
        trg_mv_seStart(i_this, trg_JA_SE_LK_ROPE_UNCOIL);
        i_this->m02DC = trg_HIMO2_WAIT;
      } else {
        i_this->m02DC = trg_HIMO2_REBOUND;
      }
      trg_actor *t = i_this->m217C;
      if (t != 0 && trg_fpcM_GetName(t) == trg_fpcNm_KUI_e)
        t->health = 0;
      i_this->m217C = 0;
    }
  }

  const u32 ropeState = i_this->m02DC;
  switch (ropeState) {
  case trg_HIMO2_WAIT: {
    i_this->speedF = 0.0f;
    if (!r30 &&
        trg_player_virtual(player, 0xDC) != 0) { /* checkRopeReadyAnime */
      trg_mv_spin(i_this, player, sp100);
      if ((i_this->m02D8 & 7) == 0)
        trg_mv_seStart(i_this, trg_JA_SE_LK_ROPE_COIL_1);
    } else {
      i_this->m02CC = trg_sub32(
          0x61,
          trg_ftoi(i_this->m2188 *
                   (trg_l_himo2Regs.mChild[0].mFloatRegs[3] + 0.060000002f)));
      trg_cLib_addCalc2(&i_this->current.pos.x, sp100->x, 1.0f,
                        200.0f * i_this->m2500);
      trg_cLib_addCalc2(&i_this->current.pos.y, sp100->y, 1.0f,
                        200.0f * i_this->m2500);
      trg_cLib_addCalc2(&i_this->current.pos.z, sp100->z, 1.0f,
                        200.0f * i_this->m2500);
      trg_cLib_addCalc2(&i_this->m2500, 1.0f, 1.0f, 0.005f);
      if (trg_mv_dist(i_this, sp100) > 5.0f)
        trg_mv_seStartP(player, trg_JA_SE_LK_ROPE_UNWIND);
    }
    if (i_this->mParameters == 1) {
      i_this->m02E4 = 0.0f;
      i_this->m2500 = 0.0f;
      i_this->m029E = 0;
      i_this->m0308 = trg_l_himo2Regs.mChild[18].mShortRegs[0] + 10;
      i_this->m02A0 = trg_l_himo2Regs.mChild[0].mShortRegs[9] + 10;
      i_this->m02A2 = 3;
      trg_camera *cam2 = (trg_camera *)trg_dComIfGp_getCamera(
          0); /* trg_dComIfGp_getCamera(0) */
      f32 f26 = i_this->m2524.y - (f32)cam2->eye.y;
      f32 f25 = i_this->m2524.z - (f32)cam2->eye.z;
      f32 f27 = i_this->m2524.x - (f32)cam2->eye.x;
      f32 f24 = f27 * f27;
      f32 f23 = f25 * f25;
      trg_vec sp130_storage, *sp130 = &sp130_storage;
      sp130->z =
          trg_madd(trg_l_himo2Regs.mChild[0].mFloatRegs[15], 100.0f, 1000.0f);
      if (trg_sqrt(trg_madd(f26, f26, f24) + f23) > sp130->z) {
        s16 r24_2 = trg_cM_atan2s(f27, f25);
        s16 r22 = (s16)-trg_cM_atan2s(f26, trg_sqrt(f24 + f23));
        trg_mDoMtx_YrotS(trg_calc_mtx(), r24_2);
        trg_mDoMtx_XrotM(trg_calc_mtx(), r22);
        sp130->x = 0.0f;
        sp130->y = 0.0f;
        trg_vec sp124_storage, *sp124 = &sp124_storage;
        trg_MtxPosition(sp130, sp124);
        i_this->m2524.x = (f32)cam2->eye.x + sp124->x;
        i_this->m2524.y = (f32)cam2->eye.y + sp124->y;
        i_this->m2524.z = (f32)cam2->eye.z + sp124->z;
      }
      if (r30) {
        i_this->m02DC = trg_HIMO2_LAUNCH_FREE;
      } else {
        i_this->m02DC = trg_HIMO2_SPIN;
        i_this->m0308 = trg_l_himo2Regs.mChild[0].mShortRegs[4] + 2;
      }
      i_this->m2530 = trg_l_himo2Regs.mChild[18].mFloatRegs[14] + 4.0f;
      i_this->current.pos = *sp100;
      i_this->m02CC = trg_l_himo2Regs.mChild[0].mShortRegs[6] + 0x5A;
      trg_segment *phVar18 = i_this->m0310;
      for (int iVar8 = 0; iVar8 < 100; iVar8++, phVar18++)
        phVar18->m10 = *sp100;
    }
    break;
  }
  case 1: {
    trg_mv_spin(i_this, player, sp100);
    if ((i_this->m02D8 & 7) == 0)
      trg_mv_seStart(i_this, trg_JA_SE_LK_ROPE_SWING_ROUND);
    s16 r4 =
        (s16)((unsigned int)i_this->m02D8 *
              (unsigned int)(trg_l_himo2Regs.mChild[0].mShortRegs[3] + 0x2000));
    s16 r8 = trg_l_himo2Regs.mChild[0].mShortRegs[7];
    s16 lo = (s16)(r8 + -20000);
    if (r4 >= lo && r4 < (s16)(r8 + -0x2E20))
      r25 = true;
    if (i_this->m0308 == 0 && r25) {
      i_this->m02DC = trg_HIMO2_LAUNCH_TARGET;
      if (r27) {
        i_this->m217C = r24;
        i_this->m0308 = trg_l_himo2Regs.mChild[0].mShortRegs[4] + 70;
        i_this->m2180 = trg_fopAcM_GetID(r24); /* trg_fopAcM_GetID */
      } else {
        i_this->m2180 = 0xFFFFFFFF;
        i_this->m0308 = trg_l_himo2Regs.mChild[0].mShortRegs[2] + 20;
      }
      trg_vec tmp_storage, *tmp = &tmp_storage;
      trg_vec_sub(&i_this->m2524, tmp,
                  &i_this->current.pos); /* trg_vec::operator- */
      trg_vec sp130_storage, *sp130 = &sp130_storage;
      f32 y = tmp->y, x = tmp->x, z = tmp->z;
      sp130->x = x;
      sp130->z = z;
      sp130->y = y;
      i_this->current.angle.y = trg_cM_atan2s(x, z);
      f32 zz = sp130->z;
      f32 xx = sp130->x;
      f64 d = trg_sqrt(trg_madd(xx, xx, zz * zz));
      i_this->current.angle.x = (s16)-trg_cM_atan2s(sp130->y, d);
      trg_mv_seStart(i_this, trg_JA_SE_LK_ROPE_LAUNCH);
    }
    break;
  }
  case 2: {
    i_this->m02CC = trg_l_himo2Regs.mChild[0].mShortRegs[0];
    f32 f27, f26, f25;
    trg_actor *t = i_this->m217C;
    if (t != 0) {
      /* HD: always the target's position (GameCube: m2524 for plain kui) */
      f32 tz = t->current.pos.z, tx = t->current.pos.x;
      s16 ay = i_this->current.angle.y;
      f32 ty = t->current.pos.y;
      trg_mDoMtx_YrotS(trg_calc_mtx(), ay);
      trg_mDoMtx_XrotM(trg_calc_mtx(), i_this->current.angle.x);
      trg_vec sp130_storage, *sp130 = &sp130_storage;
      sp130->y = 0.0f;
      sp130->x = 0.0f;
      sp130->z =
          trg_madd(trg_l_himo2Regs.mChild[0].mFloatRegs[14], 100.0f, 100.0f);
      trg_vec sp124_storage, *sp124 = &sp124_storage;
      trg_MtxPosition(sp130, sp124);
      f27 = (tx + sp124->x) - i_this->current.pos.x;
      f25 = (tz + sp124->z) - i_this->current.pos.z;
      f26 = (ty + sp124->y) - i_this->current.pos.y;
    } else {
      f27 = i_this->m2524.x - i_this->current.pos.x;
      f26 = i_this->m2524.y - i_this->current.pos.y;
      f25 = i_this->m2524.z - i_this->current.pos.z;
    }
    f32 f24 = f27 * f27;
    f32 f23 = f25 * f25;
    f64 f22 = trg_sqrt(trg_madd(f26, f26, f24) + f23);
    f64 f27_2 = trg_sqrt(f24 + f23);
    s16 r31 = (s16)trg_ftoi(
        trg_mul(f22, (trg_l_himo2Regs.mChild[0].mFloatRegs[3] + -5.0f)));
    s16 lim = (s16)(trg_l_himo2Regs.mChild[0].mShortRegs[2] + -3000);
    if (r31 < lim)
      r31 = lim;
    if (i_this->m217C != 0 || f22 > i_this->speedF * 10.0f) {
      i_this->current.angle.y = trg_cM_atan2s(f27, f25);
      i_this->current.angle.x = (s16)-trg_cM_atan2s(f26, f27_2);
    }
    i_this->speedF = trg_l_himo2Regs.mChild[18].mFloatRegs[0] +
                     50.0f; /* HD: 50 (GameCube 20) */
    trg_mDoMtx_YrotS(trg_calc_mtx(), i_this->current.angle.y);
    trg_mDoMtx_XrotM(trg_calc_mtx(), (s16)(i_this->current.angle.x + r31));
    trg_mv_posMove(i_this);
    trg_mv_pl_pos_add(i_this);
    if (i_this->m217C != 0) {
      /* HD: also when m0308 <= 10 (GameCube: m0308 == 0) */
      if (f22 < i_this->speedF * 10.0f || i_this->m0308 <= 10) {
        i_this->m02DC = trg_HIMO2_HOOKED;
        i_this->m24D9 = trg_HIMO2_DEMO_REQUEST;
        i_this->m24D8 = 1;
        if (trg_himo2_btd != 0)
          trg_btd_offStatus4000();
        break;
      }
      if (i_this->m02A2 == 0 && trg_attack_hit(&i_this->m2050)) {
        i_this->m0308 = 0x28;
        i_this->m217C = 0;
        i_this->speedF = -i_this->speedF;
        i_this->m02DC = trg_HIMO2_REBOUND;
        break;
      }
    } else if (!trg_mv_bg_check(i_this)) {
      if (i_this->m02A2 == 0 && trg_attack_hit(&i_this->m2050)) {
        i_this->m0308 = 0x28;
        i_this->m02DC = trg_HIMO2_REBOUND;
        i_this->speedF = -i_this->speedF;
        break;
      }
      if (i_this->m0308 == 0) {
        i_this->m02DC = trg_HIMO2_MAX_LENGTH;
        i_this->m0308 = 50;
        trg_mv_seStart(i_this, trg_JA_SE_LK_ROPE_MAXLENGTH);
        break;
      }
    } else {
      break;
    }
    if ((trg_mv_padTrigX() || trg_mv_padTrigY() || trg_mv_padTrigZ()) &&
        i_this->m02A0 == 0) {
      i_this->m02DC = trg_HIMO2_MAX_LENGTH;
      i_this->m0308 = 0x28;
      trg_mv_seStart(i_this, trg_JA_SE_LK_ROPE_MAXLENGTH);
      i_this->m217C = 0;
    }
    break;
  }
  case 3: {
    u32 id = i_this->m2180;
    i_this->m02CC = trg_l_himo2Regs.mChild[0].mShortRegs[0];
    trg_actor *r4_r27 = trg_mv_searchByID(id);
    bool r26 = false;
    if (r4_r27 != 0) {
      trg_vec tmp_storage, *tmp = &tmp_storage;
      trg_vec_sub(&r4_r27->eyePos, tmp,
                  &i_this->current.pos); /* trg_vec::operator- */
      trg_vec sp130_storage, *sp130 = &sp130_storage;
      f32 x = tmp->x, y = tmp->y, z = tmp->z;
      sp130->x = x;
      sp130->y = y;
      sp130->z = z;
      s16 a = trg_cM_atan2s(x, z);
      trg_cLib_addCalcAngleS2(
          &i_this->current.angle.y, a, 2,
          (s16)(trg_l_himo2Regs.mChild[18].mShortRegs[9] + 0x1000));
      f32 zz = sp130->z;
      f32 xx = sp130->x;
      f64 d = trg_sqrt(trg_madd(xx, xx, zz * zz));
      s16 b = trg_cM_atan2s(sp130->y, d);
      trg_cLib_addCalcAngleS2(
          &i_this->current.angle.x, (s16)-b, 2,
          (s16)(trg_l_himo2Regs.mChild[18].mShortRegs[9] + 0x1000));
      if (trg_mv_abs(sp130) < trg_l_himo2Regs.mChild[0].mFloatRegs[3] + 50.0f)
        r26 = true;
    }
    i_this->speedF = trg_l_himo2Regs.mChild[18].mFloatRegs[1] +
                     50.0f; /* HD: 50 (GameCube 30) */
    trg_mDoMtx_YrotS(trg_calc_mtx(), i_this->current.angle.y);
    trg_mDoMtx_XrotM(trg_calc_mtx(), i_this->current.angle.x);
    trg_mv_posMove(i_this);
    trg_mv_pl_pos_add(i_this);
    s32 bg = trg_mv_bg_check(i_this);
    s16 a2 = i_this->m02A2;
    if (bg) {
      r26 = true;
      i_this->m2500 = 0.1f;
    }
    if (a2 == 0 && trg_attack_hit(&i_this->m2050)) {
      r26 = true;
      i_this->m2500 = 0.1f;
    }
    if (i_this->m0308 == 0) {
      r26 = true;
      i_this->m2500 = 0.02f;
    }
    if (((trg_mv_padTrigX() || trg_mv_padTrigY() || trg_mv_padTrigZ()) &&
         i_this->m02A0 == 0) ||
        r26)
      i_this->m02DC = trg_HIMO2_PULL_BACK;
    break;
  }
  case 4: {
    trg_actor *r4_r27 = trg_mv_searchByID(i_this->m2180);
    if (r4_r27 == 0) {
      i_this->m02DC = trg_HIMO2_PULL_BACK;
    } else {
      trg_mDoMtx_YrotS(trg_calc_mtx(), (s16)(i_this->m02D8 << 13));
      trg_vec sp130_storage, *sp130 = &sp130_storage;
      sp130->y = 0.0f;
      sp130->z = i_this->m2184;
      sp130->x = 0.0f;
      trg_vec sp124_storage, *sp124 = &sp124_storage;
      trg_MtxPosition(sp130, sp124);
      trg_vec tmp_storage, *tmp = &tmp_storage;
      trg_vec_add(&r4_r27->eyePos, tmp, sp124); /* trg_vec::operator+ */
      i_this->current.pos = *tmp;
      trg_cLib_addCalc0(&i_this->m2184, 1.0f, 5.0f);
      if (i_this->m2184 < 0.1f)
        i_this->mParameters = 0;
      s32 r3 = trg_sub32(
          0x61,
          trg_ftoi(i_this->m2188 *
                   (trg_l_himo2Regs.mChild[0].mFloatRegs[5] + 0.060000002f)));
      if (r3 < i_this->m02CC || i_this->m2184 < 0.1f)
        i_this->m02CC = r3;
      if (trg_mv_padTrigX() || trg_mv_padTrigY() || trg_mv_padTrigZ()) {
        i_this->m02DC = trg_HIMO2_PULL_BACK;
        if (r4_r27 != 0 && trg_fpcM_GetName(r4_r27) == trg_fpcNm_BK_e) {
          /* bk->dr.mAction = 0; bk->dr.mMode = 0 */
          r4_r27->bkAction = 0;
          r4_r27->bkMode = 0;
        }
      }
    }
    i_this->m02E4 = -5.0f;
    break;
  }
  case 5: {
    /* HD: no m02CC = REG0_S(0); the pull uses trg_cLib_addCalcPos2 */
    if (trg_mv_pull_back(i_this, sp100)) {
      trg_mv_seStartP(player, trg_JA_SE_LK_ROPE_UNWIND);
    } else {
      i_this->mParameters = 0;
      trg_mv_seStart(i_this, trg_JA_SE_LK_ROPE_UNCOIL);
      i_this->m217C = 0;
      i_this->m24D8 = 0;
      i_this->m02DC = trg_HIMO2_WAIT;
    }
    break;
  }
  case 8:
    trg_cLib_addCalc2(&i_this->speedF, -50.0f, 1.0f,
                      trg_l_himo2Regs.mChild[18].mFloatRegs[12] + 6.0f);
    /* fallthrough */
  case 9: {
    if (trg_mv_pull_back(i_this, sp100)) {
      trg_mv_seStartP(player, trg_JA_SE_LK_ROPE_UNWIND);
    } else {
      i_this->mParameters = 0;
      i_this->current.pos = *sp100;
      i_this->m02DC = trg_HIMO2_WAIT;
      i_this->m029E = trg_l_himo2Regs.mChild[0].mShortRegs[6] + 0x14;
    }
    break;
  }
  case 10:
  case 11: {
    if (ropeState == 10) {
      i_this->m02CC = trg_sub32(
          100,
          trg_ftoi(i_this->m2188 *
                   (trg_l_himo2Regs.mChild[0].mFloatRegs[1] + 0.060000002f)));
      i_this->current.pos = i_this->m2504;
      trg_cLib_addCalc2(&i_this->m02E4, -6.25f, 1.0f, 0.375f);
      if (i_this->mParameters == 3) {
        i_this->m02DC = trg_HIMO2_HANG;
        if ((trg_fopAcM_GetParam(i_this->m217C) & 0xF0) != 0)
          trg_dComIfGs_onEventBit(0x540);
        else
          trg_dComIfGs_onEventBit(0x580);
      }
      i_this->m217C->health = 1;
    } else {
      i_this->current.pos = i_this->m2504;
      i_this->m02CC = trg_sub32(
          100,
          trg_ftoi(i_this->m2188 *
                   (trg_l_himo2Regs.mChild[0].mFloatRegs[2] + 0.060000002f)));
      i_this->m217C->health = 3;
    }
    if (i_this->mParameters == 4) {
      i_this->mParameters = 0;
      trg_mv_seStart(i_this, trg_JA_SE_LK_ROPE_UNCOIL);
      trg_actor *t = i_this->m217C;
      i_this->m02DC = trg_HIMO2_WAIT;
      t->health = 0;
      s8 d9 = i_this->m24D9;
      i_this->m217C = 0;
      if (d9 != 0) {
        i_this->m24D9 = trg_HIMO2_DEMO_JUMP;
        i_this->m029C = 120;
      }
      i_this->m24D8 = 0;
    }
    break;
  }
  default:
    break;
  }

  f32 f27 = 0.0f; /* the m24B4 step (case 2) */
  f64 f26 = 0.0;  /* the sub stick Y (case 5) */
  s8 d9 = i_this->m24D9;
  bool stickBlock = false;
  switch (d9) {
  case trg_HIMO2_DEMO_REQUEST: {
    if (!(i_this->eventInfo.mCommand ==
          2)) { /* !eventInfo.checkCommandDemoAccrpt() */
      trg_mv_orderPotentialEvent(i_this, 1 /* dEvtFlag_NOPARTNER_e */);
      i_this->eventInfo.mCondition |= 2;
      break;
    }
    i_this->m24F4 = trg_l_himo2Regs.mChild[18].mFloatRegs[11] + 50.0f;
    trg_mv_camStop(camera);
    trg_mv_camSetTrimSize(camera, 2);
    s8 m251C = i_this->m251C;
    i_this->m24D9 = trg_HIMO2_DEMO_COIL;
    f32 b4 = trg_l_himo2Regs.mChild[18].mFloatRegs[5] + -400.0f;
    trg_actor *t = i_this->m217C;
    i_this->m24C8 = 0;
    i_this->m24CA = 0;
    i_this->m24B4 = b4;
    i_this->m24C4 = 0.0f;
    i_this->m24BC = 0;
    s16 ay = t->current.angle.y;
    if (m251C != 0)
      i_this->m2510 = (s16)(ay + -0x4000);
    else
      i_this->m2510 = (s16)(ay + 0x4000);
    if ((trg_fopAcM_GetParam(t) & 0xF0) != 0) {
      i_this->m2514 = -1.0f;
      i_this->m24B8 = trg_l_himo2Regs.mChild[0].mFloatRegs[6] + -90.0f;
      i_this->m2518 = trg_l_himo2Regs.mChild[0].mFloatRegs[7] + 1.0f;
      i_this->m24FC = trg_l_himo2Regs.mChild[0].mFloatRegs[16] + 3.5f;
    } else {
      /* HD: no trg_l_himo2HIO.m06 choice: always (-1, -1) */
      i_this->m24B8 = 0.0f;
      i_this->m2514 = -1.0f;
      i_this->m2518 = -1.0f;
      i_this->m24FC = trg_l_himo2Regs.mChild[18].mFloatRegs[6] + 3.0f;
    }
    i_this->m24E8 = i_this->m217C->current.pos;
  }
    /* fallthrough */
  case trg_HIMO2_DEMO_COIL: {
    trg_cLib_addCalc0(&i_this->m24B8, 1.0f,
                      trg_l_himo2Regs.mChild[18].mFloatRegs[4] + 3.0f);
    if ((trg_fopAcM_GetParam(i_this->m217C) & 0xF0) == 0)
      f27 = trg_l_himo2Regs.mChild[18].mFloatRegs[3] + 10.0f;
    /* Randomizer: step20, plus REG+10 for plain kui. */
    trg_cLib_addCalc2(&i_this->m24B4, -144.0f, 1.0f, f27 + 20.0f);
    if (i_this->m24B4 > -145.0f) {
      s32 bc = i_this->m24BC;
      if (bc < 400) {
        f32 f1 = trg_nmsub(i_this->m217C->scale.y - 1.0f,
                           WWHD_GAME_DATA(0x100110C8, f32), 20.0f);
        f32 c4 = i_this->m24C4;
        if (f1 < 0.5f)
          f1 = 0.5f;
        s32 r3 = trg_ftoi(c4 / f1);
        s32 r5 = (s32)((unsigned int)r3 + 6u);
        i_this->m24C0 = bc;
        i_this->m24BC = (s32)((unsigned int)i_this->m24BC + (unsigned int)r5);
        i_this->m24C4 = c4 + (trg_l_himo2Regs.mChild[18].mFloatRegs[2] +
                              2.0f); /* HD: + REG + 2 (GameCube + 1) */
        if (bc == 0) {
          trg_mv_seStart(i_this, trg_JA_SE_LK_ROPE_COIL_1);
        } else if (bc >= 13) {
          s32 nbc = i_this->m24BC;
          if (nbc <= (s32)((unsigned int)r5 + 13u)) {
            trg_mv_seStart(i_this, trg_JA_SE_LK_ROPE_COIL_2);
          } else if (bc >= 26) {
            if (nbc <= (s32)((unsigned int)r5 + 26u) ||
                (bc >= 39 && nbc <= (s32)((unsigned int)r5 + 39u)))
              trg_mv_seStart(i_this, trg_JA_SE_LK_ROPE_COIL_3);
          }
        }
      }
      trg_actor *t = i_this->m217C;
      u32 prm = trg_fopAcM_GetParam(t);
      f32 f3 = t->scale.y;
      if ((prm & 0x0F) == 3)
        f3 = (f3 - 0.14f) +
             (f32)trg_l_himo2Regs.mChild[8].mFloatRegs[7]; /* REG8_F(7) */
      s16 r4 = (s16)trg_ftoi(trg_nmsub(2000.0f, f3 - 1.0f, 4000.0f));
      trg_cLib_addCalcAngleS2(&i_this->m24C8, r4, 1, (s16)(r4 / 8));
      if (i_this->m24BC > 49) {
        s16 ca = i_this->m24CA;
        if (ca < 9000) {
          i_this->m24CA = 9000;
          if ((u32)(i_this) + 0x37C != 0) {
            u32 p = trg_fopAcM_GetParam(i_this->m217C);
            trg_mDoAud_seStart(
                (p & 0xF0) != 0 ? trg_JA_SE_LK_ROPE_HOOK_BODY
                                : trg_JA_SE_LK_ROPE_HOOK_WOOD,
                &i_this->eyePos, 0,
                trg_dComIfGp_getReverb(trg_fopAcM_GetRoomNo(i_this)));
          }
        } else if (ca == 9000) {
          i_this->m24CA = 0x238C;
        }
      }
    }
    trg_cLib_addCalc2(&i_this->m24E8.x, i_this->m217C->current.pos.x, 0.3f,
                      100.0f);
    trg_cLib_addCalc2(&i_this->m24E8.y, i_this->m217C->current.pos.y, 0.3f,
                      100.0f);
    trg_cLib_addCalc2(&i_this->m24E8.z, i_this->m217C->current.pos.z, 0.3f,
                      100.0f);
    i_this->m24DC = i_this->m217C->current.pos;
    trg_mDoMtx_YrotS(trg_calc_mtx(), i_this->m2510);
    trg_vec sp130_storage, *sp130 = &sp130_storage;
    f32 z4c = trg_l_himo2Regs.mChild[18].mFloatRegs[9] + 120.0f;
    f32 x44 = trg_l_himo2Regs.mChild[18].mFloatRegs[7] + 10.0f;
    sp130->y = trg_l_himo2Regs.mChild[18].mFloatRegs[8] + -85.0f;
    sp130->x = x44 * i_this->m2514;
    sp130->z = z4c * i_this->m2518;
    trg_vec sp124_storage, *sp124 = &sp124_storage;
    trg_MtxPosition(sp130, sp124);
    f32 fc = i_this->m24FC;
    f32 nx = trg_madd(sp124->x, fc, i_this->m24DC.x);
    f32 ny = trg_madd(sp124->y, fc, i_this->m24DC.y);
    f32 nz = trg_madd(sp124->z, fc, i_this->m24DC.z);
    i_this->m24DC.x = nx;
    i_this->m24DC.y = ny;
    i_this->m24DC.z = nz;
    if ((trg_fopAcM_GetParam(i_this->m217C) & 0xF0) != 0)
      trg_cLib_addCalc2(&i_this->m24FC,
                        trg_l_himo2Regs.mChild[0].mFloatRegs[6] + 1.8f,
                        trg_l_himo2Regs.mChild[0].mFloatRegs[4] + 0.2f,
                        trg_l_himo2Regs.mChild[0].mFloatRegs[5] + 0.1f);
    else
      trg_cLib_addCalc2(&i_this->m24FC,
                        trg_l_himo2Regs.mChild[18].mFloatRegs[10] + 1.2f,
                        trg_l_himo2Regs.mChild[18].mFloatRegs[4] + 0.2f,
                        trg_l_himo2Regs.mChild[18].mFloatRegs[5] + 0.2f);
    break;
  }
  case trg_HIMO2_DEMO_SET_ROCK: {
    i_this->m217C->health = 2;
    trg_mDoMtx_YrotS(trg_calc_mtx(), i_this->m217C->current.angle.y);
    trg_vec sp130_storage, *sp130 = &sp130_storage;
    sp130->x = trg_l_himo2Regs.mChild[0].mFloatRegs[11] + 900.0f;
    sp130->y =
        (i_this->m217C->home.pos.y + trg_l_himo2Regs.mChild[0].mFloatRegs[12]) +
        2000.0f;
    sp130->z = trg_l_himo2Regs.mChild[0].mFloatRegs[13] + 1000.0f;
    trg_vec sp124_storage, *sp124 = &sp124_storage;
    trg_MtxPosition(sp130, sp124);
    trg_cLib_addCalc2(&i_this->m24DC.x, sp124->x, 0.3f, 200.0f);
    trg_cLib_addCalc2(&i_this->m24DC.y, sp124->y, 0.3f, 200.0f);
    trg_cLib_addCalc2(&i_this->m24DC.z, sp124->z, 0.3f, 200.0f);
    trg_cLib_addCalc2(&i_this->m24E8.y,
                      (i_this->m217C->home.pos.y + 2500.0f) +
                          trg_l_himo2Regs.mChild[0].mFloatRegs[14],
                      0.3f, 100.0f);
    trg_cLib_addCalc2(&i_this->m24F4,
                      trg_l_himo2Regs.mChild[0].mFloatRegs[13] + 80.0f, 1.0f,
                      2.0f);
    if (i_this->m029C <= 30) {
      if (i_this->m029C < 30)
        trg_btd_setBlur(0xB4);
      if (i_this->m029C == 30)
        trg_mv_seStartCurrent(trg_himo2_dr, trg_JA_SE_CM_BTD_ROPE_SET);
    }
    if (*(volatile u8 *)trg_stage() !=
        'X') { /* dComIfGp_getStartStageName()[0] */
      if (i_this->m029C > 1)
        break;
      if (!trg_dComIfGs_isEventBit(0x420))
        trg_mv_colset(1.0f);
    }
    if (i_this->m029C != 0 || trg_l_himo2Regs.mChild[0].mShortRegs[8] != 0)
      break;
    trg_btd_setBlur(1);
    i_this->m24D9 = trg_HIMO2_DEMO_DROP_ROCK;
    if (*(volatile u8 *)trg_stage() == 'X' || trg_dComIfGs_isEventBit(0x420)) {
      i_this->m02E0 = 0;
      trg_actor *t = i_this->m217C;
      i_this->m029C = 0;
      t->health = 0;
    } else {
      trg_dComIfGs_onEventBit(0x420);
      i_this->m029C = trg_l_himo2Regs.mChild[0].mShortRegs[2] + 0x3E;
      trg_ykgr_hide();
      trg_actor *t = i_this->m217C;
      i_this->m02E0 = 0;
      t->health = 0;
    }
    if (trg_himo2_btd != 0)
      trg_btd_onStatus4000();
  }
    /* fallthrough */
  case trg_HIMO2_DEMO_DROP_ROCK: {
    trg_mv_colset(1.0f);
    if ((u32)(s32)i_this->m029C ==
        (u32)(s32)(s16)(trg_l_himo2Regs.mChild[0].mShortRegs[2] + 0x39))
      trg_mv_seStart0(trg_JA_SE_CV_DRG_SET_ROPE);
    bool big = false;
    if (i_this->m029C <= 1) {
      trg_mv_colset(0.0f);
      if (i_this->m029C == 0 && trg_l_himo2Regs.mChild[0].mShortRegs[8] == 0)
        big = true;
    }
    if (big) {
      trg_himo2_dr->unk_50C = 0; /* dr->unk_50C */
      trg_mv_colset(0.0f);
      i_this->m02E0 = 0;
      i_this->m029C = 50;
      i_this->m24D9 = trg_HIMO2_DEMO_CONTROL;
      i_this->mParameters = 2;
      trg_event_reset((void *)(trg_game() + 0x51D0));
      i_this->m02A4 = 20;
      s16 ang = trg_fopAcM_searchPlayerAngleY(
          i_this); /* trg_fopAcM_searchPlayerAngleY */
      s16 a = (s16)(ang + 0x8000);
      i_this->m2510 = a;
      i_this->m24F4 = 65.0f;
      i_this->m2512 = a;
      i_this->m24F8 = 65.0f;
      trg_mDoMtx_YrotS(trg_calc_mtx(), a);
      trg_vec sp130_storage, *sp130 = &sp130_storage;
      sp130->x = trg_l_himo2Regs.mChild[0].mFloatRegs[7] + 300.0f;
      sp130->y = (player->current.pos.y + 700.0f) +
                 trg_l_himo2Regs.mChild[0].mFloatRegs[8];
      sp130->z = trg_l_himo2Regs.mChild[0].mFloatRegs[9] + -500.0f;
      trg_MtxPosition(sp130, &i_this->m24DC);
      i_this->m24DC.x = trg_madd(
          player->current.pos.x,
          trg_l_himo2Regs.mChild[0].mFloatRegs[15] + 0.55f, i_this->m24DC.x);
      i_this->m24DC.z = trg_madd(
          player->current.pos.z,
          trg_l_himo2Regs.mChild[0].mFloatRegs[15] + 0.55f, i_this->m24DC.z);
      i_this->m24E8.x = player->current.pos.x;
      f32 py = player->current.pos.y;
      i_this->m24E8.y = py;
      i_this->m24E8.z = player->current.pos.z;
      i_this->m24E8.y = py - 50.0f;
      trg_ykgr_show();
    } else {
      i_this->m24F4 = trg_l_himo2Regs.mChild[0].mFloatRegs[12] + 50.0f;
      trg_himo2_dr->unk_50C = 1;
      i_this->m24DC.x = trg_l_himo2Regs.mChild[0].mFloatRegs[14] + -5000.0f;
      i_this->m24DC.y = trg_l_himo2Regs.mChild[0].mFloatRegs[15] + 21720.0f;
      i_this->m24DC.z = trg_l_himo2Regs.mChild[0].mFloatRegs[16] + 4000.0f;
      i_this->m24E8.x = trg_l_himo2Regs.mChild[0].mFloatRegs[17];
      i_this->m24E8.y = trg_l_himo2Regs.mChild[0].mFloatRegs[18] + 21420.0f;
      i_this->m24E8.z = trg_l_himo2Regs.mChild[0].mFloatRegs[19];
    }
    break;
  }
  case trg_HIMO2_DEMO_CONTROL: {
    if (i_this->m029C == 0) {
      s8 e0 = i_this->m02E0;
      if (e0 == 0) {
        trg_mv_seStart0(trg_JA_SE_CV_DRG_ROCKFALL_1);
        i_this->m029C = 50;
        i_this->m02E0 = (s8)(i_this->m02E0 + 1);
      } else if (e0 == 1) {
        trg_mv_seStart0(trg_JA_SE_CV_DRG_ROCKFALL_1_2);
        i_this->m029C = 50;
        i_this->m02E0 = (s8)(i_this->m02E0 + 1);
      } else {
        if (e0 >= 2) {
          trg_mv_seStart0(trg_JA_SE_CV_DRG_ROCKFALL_1_3);
          e0 = 2;
        }
        i_this->m02E0 = (s8)(e0 + 1);
        i_this->m029C = 50;
      }
    }
    f26 = trg_mv_subStickY();
    stickBlock = true;
    break;
  }
  case trg_HIMO2_DEMO_JUMP: {
    if (!((u32)player->mResetFlg0 & 0x200u) &&
        i_this->m029C != 0) { /* !getRopeJumpLand() */
      if (!(i_this->eventInfo.mCommand == 2)) {
        trg_mv_orderPotentialEvent(i_this, 2 /* dEvtFlag_STAFF_ALL_e */);
        i_this->eventInfo.mCondition |= 2;
      }
      trg_himo2_dr->unk_4CA = 0x5A; /* dr->unk_4CA */
      trg_cLib_addCalc2(&i_this->m24F8,
                        trg_l_himo2Regs.mChild[0].mFloatRegs[8] + 20.0f, 1.0f,
                        trg_l_himo2Regs.mChild[0].mFloatRegs[9] + 2.0f);
      stickBlock = true; // Keep the sub-stick step at zero.
      break;
    }
    i_this->m24D9 = trg_HIMO2_DEMO_RESET;
    i_this->m029C = 10;
    break;
  }
  case trg_HIMO2_DEMO_RESET: {
    if (i_this->m029C != 0)
      break;
    if ((s16)trg_himo2_dr->unk_4BA >= 10) { /* dr->unk_4BA */
      trg_vec c_storage, *c = &c_storage;
      trg_vec e_storage, *e = &e_storage;
      c->x = i_this->m24E8.x;
      c->y = i_this->m24E8.y;
      e->z = i_this->m24DC.z;
      e->x = i_this->m24DC.x;
      c->z = i_this->m24E8.z;
      i_this->m24D9 = trg_HIMO2_DEMO_INACTIVE;
      e->y = i_this->m24DC.y;
      trg_mv_camReset(camera, c, e);
      trg_mv_camStart(camera);
      trg_mv_camSetTrimSize(camera, 0);
      trg_event_reset((void *)(trg_game() + 0x51D0));
      break;
    }
    i_this->m24F4 = trg_l_himo2Regs.mChild[0].mFloatRegs[12] + 50.0f;
    trg_btd_onStatus4000();
    i_this->m24D9 = trg_HIMO2_DEMO_BOSS_FALL;
    i_this->m24E8.x = 0.0f;
    i_this->m24E8.y =
        trg_madd(trg_l_himo2Regs.mChild[0].mFloatRegs[5], 0.1f, 3000.0f);
    i_this->m24E8.z = trg_l_himo2Regs.mChild[0].mFloatRegs[6] * 0.1f;
    trg_mDoMtx_YrotS(trg_calc_mtx(), (s16)trg_himo2_btd->current.angle.y);
    {
      trg_vec sp130_storage, *sp130 = &sp130_storage;
      sp130->x = trg_l_himo2Regs.mChild[0].mFloatRegs[7] * 0.1f;
      sp130->y =
          trg_madd(trg_l_himo2Regs.mChild[0].mFloatRegs[8], 0.1f, 1000.0f);
      sp130->z =
          trg_madd(trg_l_himo2Regs.mChild[0].mFloatRegs[9], 0.1f, 1400.0f);
      trg_MtxPosition(sp130, &i_this->m24DC);
    }
    i_this->m029C = 210;
    i_this->m24F4 = trg_l_himo2Regs.mChild[0].mFloatRegs[13] + 80.0f;
    {
      trg_line lin_chk_storage, *lin_chk = &lin_chk_storage;
      trg_line_construct(lin_chk);
      trg_line_set(lin_chk, &i_this->m24E8, &i_this->m24DC, i_this);
      if (trg_line_cross((void *)(trg_game() + 0x12A0), lin_chk))
        i_this->m24DC = *(trg_vec *)((u32)lin_chk + 0x30);
      *(volatile unsigned int *)((u32)lin_chk + 0x58) = 0x10010ED4;
      *(volatile unsigned int *)((u32)lin_chk + 0x64) = 0x10010E24;
      *(volatile unsigned int *)((u32)lin_chk + 0x20) = 0x10010E14;
      trg_himo2_destroyLineCheck(lin_chk, 0);
    }
  }
    /* fallthrough */
  case trg_HIMO2_DEMO_BOSS_FALL: {
    trg_himo2_dr->unk_40A = 3; /* dr->unk_40A */
    if (i_this->m029C == 200)
      trg_mv_seStart0(trg_JA_SE_CV_DRG_ROCKFALL_2);
    if (i_this->m029C == 170)
      trg_mv_seStartCurrent(trg_himo2_dr, trg_JA_SE_CM_BTD_BEF_ROCK_FALL);
    trg_actor *dr = trg_himo2_dr;
    f32 f1 = i_this->m029C > 150 ? 2000.0f : 100.0f;
    trg_cLib_addCalc2(&dr->unk_414, f1, 0.5f, 100.0f); /* dr->unk_414 */
    if (i_this->m029C > 120)
      break;
    trg_cLib_addCalc2(
        &i_this->m24E8.y,
        trg_madd(trg_l_himo2Regs.mChild[0].mFloatRegs[5], 0.1f, 650.0f), 1.0f,
        100.0f);
    trg_cLib_addCalc2(&i_this->m24DC.y, 0.0f, 0.5f, 100.0f);
    if ((u32)(s32)i_this->m029C ==
        (u32)(s32)(s16)(trg_l_himo2Regs.mChild[0].mShortRegs[4] + 0x6E)) {
      i_this->m2520 = trg_l_himo2Regs.mChild[0].mFloatRegs[11] + 50.0f;
      trg_btd_setBlur(0xB4);
    }
    if (i_this->m029C > 80)
      break;
    trg_cLib_addCalc2(&i_this->m24F4,
                      trg_l_himo2Regs.mChild[0].mFloatRegs[14] + 40.0f, 0.5f,
                      2.0f);
    if (i_this->m029C == 2)
      trg_btd_setBlur(1);
    if (i_this->m029C != 0)
      break;
    if ((u8)trg_himo2_btd->m6190 >= 3) { /* btd->m6190 */
      i_this->m24D9 = trg_HIMO2_DEMO_BOSS_END;
      i_this->m029C = 220;
      trg_himo2_dr->unk_40A = 0;
    } else {
      i_this->m24D9 = trg_HIMO2_DEMO_INACTIVE;
      f32 ey = player->eyePos.y;
      f32 ex = player->eyePos.x;
      f32 ez = player->eyePos.z;
      trg_vec c_storage, *c = &c_storage;
      trg_vec spDC_storage, *spDC = &spDC_storage;
      c->x = ex;
      c->y = player->eyePos.y;
      c->z = player->eyePos.z;
      spDC->x = ex * 0.9f;
      spDC->y = ey;
      spDC->z = ez * 0.9f;
      trg_mv_camReset(camera, c, spDC);
      trg_mv_camStart(camera);
      trg_mv_camSetTrimSize(camera, 0);
      trg_btd_offStatus4000();
      trg_event_reset((void *)(trg_game() + 0x51D0));
      trg_himo2_dr->unk_40A = 0;
    }
    break;
  }
  case trg_HIMO2_DEMO_BOSS_END: {
    trg_cLib_addCalc2(&i_this->m24F4,
                      trg_l_himo2Regs.mChild[0].mFloatRegs[4] + 50.0f, 0.5f,
                      3.0f);
    f32 by = (f32)trg_himo2_btd->eyePos.y; /* btd->actor.eyePos.y */
    trg_cLib_addCalc2(
        &i_this->m24E8.y,
        trg_madd(trg_l_himo2Regs.mChild[0].mFloatRegs[5], 0.1f, by) + 200.0f,
        0.2f, 1000.0f);
    trg_mDoMtx_YrotS(trg_calc_mtx(), (s16)trg_himo2_btd->current.angle.y);
    trg_vec sp130_storage, *sp130 = &sp130_storage;
    sp130->x = trg_madd(trg_l_himo2Regs.mChild[0].mFloatRegs[7], 0.1f, -300.0f);
    sp130->y = trg_madd(trg_l_himo2Regs.mChild[0].mFloatRegs[8], 0.1f, 100.0f);
    sp130->z = trg_madd(trg_l_himo2Regs.mChild[0].mFloatRegs[9], 0.1f, 2000.0f);
    trg_vec sp124_storage, *sp124 = &sp124_storage;
    trg_MtxPosition(sp130, sp124);
    trg_cLib_addCalc2(&i_this->m24DC.x, sp124->x, 0.1f, 50.0f);
    trg_cLib_addCalc2(&i_this->m24DC.y, sp124->y, 0.1f, 50.0f);
    trg_cLib_addCalc2(&i_this->m24DC.z, sp124->z, 0.1f, 50.0f);
    if (i_this->m029C == 99)
      trg_btd_setBlur(0xB4);
    if (i_this->m029C == 40)
      trg_btd_setBlur(1);
    if (i_this->m029C == 0) {
      i_this->m24D9 = trg_HIMO2_DEMO_INACTIVE;
      f32 ex = player->eyePos.x;
      f32 ez = player->eyePos.z;
      f32 ey = player->eyePos.y;
      trg_vec c_storage, *c = &c_storage;
      trg_vec spD0_storage, *spD0 = &spD0_storage;
      c->x = ex;
      c->y = player->eyePos.y;
      c->z = player->eyePos.z;
      spD0->x = ex * 0.9f;
      spD0->y = ey;
      spD0->z = ez * 0.9f;
      trg_mv_camReset(camera, c, spD0);
      trg_mv_camStart(camera);
      trg_mv_camSetTrimSize(camera, 0);
      trg_btd_offStatus4000();
      trg_event_reset((void *)(trg_game() + 0x51D0));
    }
    break;
  }
  default:
    break;
  }

  if (stickBlock) {
    /* label_1d50 */
    f64 f2 = trg_mv_subStickX();
    s32 add = trg_ftoi(
        trg_mul(f2, (trg_l_himo2Regs.mChild[0].mFloatRegs[6] + 1000.0f)));
    i_this->m2512 = (s16)((unsigned int)(s32)i_this->m2512 + (unsigned int)add);
    trg_cLib_addCalcAngleS2(&i_this->m2510, i_this->m2512, 4, 0x1000);
    if (!(f26 > -0.1f)) {
      trg_cLib_addCalc2(&i_this->m24F8,
                        trg_l_himo2Regs.mChild[0].mFloatRegs[7] + 70.0f, 1.0f,
                        trg_mul(__builtin_fabs(f26), 3.0));
    } else if (!(f26 < 0.1f)) {
      trg_cLib_addCalc2(&i_this->m24F8,
                        trg_l_himo2Regs.mChild[0].mFloatRegs[8] + 20.0f, 1.0f,
                        trg_mul(__builtin_fabs(f26), 3.0));
    }
    trg_cLib_addCalc2(&i_this->m24F4, i_this->m24F8, 0.1f, 10.0f);
    trg_mDoMtx_YrotS(trg_calc_mtx(), i_this->m2510);
    trg_vec sp130_storage, *sp130 = &sp130_storage;
    sp130->x = trg_l_himo2Regs.mChild[0].mFloatRegs[7] + 300.0f;
    sp130->y = (player->current.pos.y + 700.0f) +
               trg_l_himo2Regs.mChild[0].mFloatRegs[8];
    sp130->z = trg_l_himo2Regs.mChild[0].mFloatRegs[9] + -500.0f;
    trg_MtxPosition(sp130, &i_this->m24DC);
    f32 nx = trg_madd(player->current.pos.x,
                      trg_l_himo2Regs.mChild[0].mFloatRegs[15] + 0.55f,
                      i_this->m24DC.x);
    f32 oz = i_this->m24DC.z;
    i_this->m24DC.x = nx;
    i_this->m24DC.z =
        trg_madd(player->current.pos.z,
                 trg_l_himo2Regs.mChild[0].mFloatRegs[15] + 0.55f, oz);
    trg_cLib_addCalc2(&i_this->m24E8.x, player->current.pos.x, 0.3f, 100.0f);
    trg_cLib_addCalc2(&i_this->m24E8.y,
                      (player->current.pos.y - 50.0f) +
                          trg_l_himo2Regs.mChild[0].mFloatRegs[10],
                      0.3f, 100.0f);
    trg_cLib_addCalc2(&i_this->m24E8.z, player->current.pos.z, 0.3f, 100.0f);
    if (i_this->m02A4 == 0 &&
        trg_player_virtual(player, 0x4C) == 0) { /* !checkPlayerFly() */
      trg_mv_camStart(camera);
      i_this->m24D9 = trg_HIMO2_DEMO_INACTIVE;
      i_this->m24D8 = 0;
      i_this->m217C = 0;
      i_this->m02DC = trg_HIMO2_WAIT;
      i_this->mParameters = 0;
      trg_event_reset((void *)(trg_game() + 0x51D0));
    }
  }

  if (i_this->m24D9 > trg_HIMO2_DEMO_INACTIVE) {
    u32 d8 = i_this->m02D8;
    f32 m2520 = i_this->m2520;
    f32 f27_3 = trg_cM_ssin((s32)(d8 * 0x3300)) * m2520;
    f32 f1 = trg_cM_scos((s32)(d8 * 0x3000)) * m2520;
    f32 c3 = trg_cM_scos((s32)(d8 * 0x1C00));
    trg_vec spB8_storage, *spB8 = &spB8_storage;
    trg_vec spC4_storage, *spC4 = &spC4_storage;
    f32 dcx = i_this->m24DC.x;
    f32 e8x = i_this->m24E8.x;
    f32 e8y = i_this->m24E8.y;
    f32 e8z = i_this->m24E8.z;
    spB8->y = e8y + f1;
    spB8->z = e8z;
    spB8->x = e8x + f27_3;
    spC4->x = dcx + f27_3;
    spC4->y = i_this->m24DC.y + f1;
    s16 r23 = (s16)trg_ftoi((c3 * m2520) * 7.5f);
    spC4->z = i_this->m24DC.z;
    trg_mv_camSet(camera, spB8, spC4, r23, i_this->m24F4);
    trg_cLib_addCalc0(&i_this->m2520, 1.0f,
                      trg_l_himo2Regs.mChild[0].mFloatRegs[16] + 2.0f);
    trg_actor *t = i_this->m217C;
    s16 r23_2 = 0;
    s32 bc = i_this->m24BC;
    if (t != 0 && (trg_fopAcM_GetParam(t) & 0xF0) != 0)
      r23_2 = -50;
    if (bc > (s16)(r23_2 + 0x82) &&
        trg_l_himo2Regs.mChild[0].mShortRegs[8] == 0) {
      if ((trg_fopAcM_GetParam(t) & 0xF0) != 0) {
        if (i_this->m24D9 == trg_HIMO2_DEMO_COIL) {
          if ((s16)trg_himo2_dr->unk_4BA >= 10) {
            i_this->m029C = 0;
            i_this->m24D9 = trg_HIMO2_DEMO_DROP_ROCK;
            i_this->m24D8 = 2;
            if (trg_himo2_btd != 0)
              trg_btd_onStatus4000();
          } else {
            i_this->m24D9 = trg_HIMO2_DEMO_SET_ROCK;
            i_this->m029C = trg_l_himo2Regs.mChild[0].mShortRegs[2] + 0x41;
          }
          i_this->m24D8 = 2;
        }
      } else {
        trg_mv_camStart(camera);
        i_this->m24D9 = trg_HIMO2_DEMO_INACTIVE;
        i_this->mParameters = 2;
        i_this->m24D8 = 2;
        trg_event_reset((void *)(trg_game() + 0x51D0));
      }
    }
  }

  if (i_this->m02CC < 2)
    i_this->m02CC = 2;
}
