/* Complete public CC0 demo_camera02101F30, d_a_bwd_camera.cpp a874e1a9.
 * MIT fix_vanilla_bugs: safe target seam02102ED8 in death-start state100. */
#pragma once
#ifndef R_MC
#define R_MC(t, a) (*(volatile t *)(a))
#endif
#ifndef RMC_PLAYER_POSITION
#define RMC_PLAYER_POSITION(player, position, angle)                           \
  ((void (*)(u32, u32, s32))(R_MC(u32, R_MC(u32, (player) + 0xB4) + 0x114)))(  \
      (player), (position), (angle))
#endif
WWHD_GAME_FUNC(0x025200D4, u32, rmc_game, (void));
WWHD_GAME_FUNC(0x025D7B24, void, rmc_order,
               (u32 actor, s32 flag, s32 hind, s32 parameter));
WWHD_GAME_FUNC(0x02514F2C, void, rmc_stop, (u32 camera));
WWHD_GAME_FUNC(0x02515280, void, rmc_trim, (u32 camera, s32 size));
WWHD_GAME_FUNC(0x0200ED84, void, rmc_calculate,
               (u32 value, f32 target, f32 ratio, f32 maximum));
WWHD_GAME_FUNC(0x0200EDC8, void, rmc_decelerate,
               (u32 value, f32 ratio, f32 maximum));
WWHD_GAME_FUNC(0x0200F428, void, rmc_angle_calculate,
               (u32 value, s32 target, s32 ratio, s32 maximum));
WWHD_GAME_FUNC(0x025F1884, void, rmc_y_rotation, (u32 matrix, s32 angle));
WWHD_GAME_FUNC(0x0200FCD8, void, rmc_transform, (u32 vector, u32 result));
WWHD_GAME_FUNC(0x028E8D88, void, rmc_vector_add, (u32 a, u32 b, u32 result));
WWHD_GAME_FUNC(0x025E1944, void, rmc_stream_play, (void));
WWHD_GAME_FUNC(0x025CB408, void, rmc_quake_start,
               (u32 vibration, s32 strength, s32 flags, u32 direction));
WWHD_GAME_FUNC(0x025CB610, void, rmc_quake_stop, (u32 vibration, s32 id));
WWHD_GAME_FUNC(0x02520540, s32, rmc_reverb, (s32 room));
WWHD_GAME_FUNC(0x025E1A40, void, rmc_sound,
               (u32 sound, u32 position, u32 parameter, s32 reverb));
/* The native formatter saves F1 only when the variadic CR6 flag is set.
 * Keep the variadic declaration so the compiler emits crset/crclr correctly. */
WWHD_GAME_FUNC(0x027EC9E8, void, rmc_report, (s32 x, s32 y, u32 text, ...));
WWHD_GAME_FUNC(0x025E18EC, void, rmc_bgm, (u32 id));
WWHD_GAME_FUNC(0x025B9098, void, rmc_dungeon_item, (u32 memory, s32 item));
WWHD_GAME_FUNC(0x02008E0C, void, rmc_ground_construct, (u32 check));
WWHD_GAME_FUNC(0x02008DAC, void, rmc_ground_destroy, (u32 check, s32 flags));
WWHD_GAME_FUNC(0x02008974, f64, rmc_ground_cross, (u32 background, u32 check));
WWHD_GAME_FUNC(0x025D8A5C, u32, rmc_item_create,
               (u32 position, s32 item, s32 room, u32 angle, u32 scale,
                s32 parameter));
WWHD_GAME_FUNC(0x025D5218, u32, rmc_actor_judge, (u32 callback, u32 key));
WWHD_GAME_FUNC(0x025D9874, void, rmc_warp_create,
               (u32 position, u32 angle, s32 room, s32 parameter));
WWHD_GAME_FUNC(0x0251510C, void, rmc_reset, (u32 camera, u32 center, u32 eye));
WWHD_GAME_FUNC(0x02514F38, void, rmc_start, (u32 camera));
WWHD_GAME_FUNC(0x02514FE8, void, rmc_set,
               (u32 camera, u32 center, u32 eye, s32 bank, f32 fov));
WWHD_GAME_FUNC(0x020198D8, f64, rmc_random, (f64 range));
WWHD_GAME_FUNC(0x028F4384, f64, rmc_sqrt, (f64 square));
#ifndef R_MOLGERA_POS
#define R_MOLGERA_POS(t, a) R_MC(t, a)
#endif
#define RANDOMIZER_MOLGERA_SQRT(value) rmc_sqrt(value)
#include "molgera_warp_position.h"
#undef RANDOMIZER_MOLGERA_SQRT
static f32 rmc_reg_float(u32 child, u32 index) {
  return R_MC(f32,
              (u32)&WWHD_GAME_DATA(0x1047B610, u8) + child * 0x90 + index * 4);
}
static s16 rmc_reg_short(u32 child, u32 index) {
  return R_MC(s16,
              (u32)&WWHD_GAME_DATA(0x1047B688, u8) + child * 0x90 + index * 2);
}
static u32 rmc_matrix(void) { return WWHD_GAME_DATA(0x1018C7B0, u32); }
static u32 rmc_target(void) { return (u32)&WWHD_GAME_DATA(0x10462AE8, u8); }
static f32 rmc_hio_height(void) { return WWHD_GAME_DATA(0x10462A8C, f32); }
static void rmc_copy_vector(u32 to, u32 from) {
  /* Character access preserves the representation and may alias typed local
   * f32 arrays. Word stores followed by float reads violate C aliasing and
   * LLVM23 materializes a NaN for the tracking-camera Y calculation. */
  for (u32 i = 0; i < 12; i++)
    R_MC(u8, to + i) = R_MC(u8, from + i);
}
static void rmc_set_vector(u32 vector, f32 x, f32 y, f32 z) {
  R_MC(f32, vector) = x;
  R_MC(f32, vector + 4) = y;
  R_MC(f32, vector + 8) = z;
}
static s32 rmc_integer(f64 value) {
  if (value != value || value <= -2147483648.0)
    return (s32)0x80000000u;
  if (value >= 2147483648.0)
    return 0x7FFFFFFF;
  return (s32)value;
}
static f32 rmc_sine(s32 angle) {
  return R_MC(f32,
              (u32)&WWHD_GAME_DATA(0x104A44F8, u8) + ((u16)angle >> 3) * 8);
}
static f32 rmc_cosine(s32 angle) {
  return R_MC(f32,
              (u32)&WWHD_GAME_DATA(0x104A44F8, u8) + ((u16)angle >> 3) * 8 + 4);
}
static void rmc_quake(void) {
  f32 direction[3] = {0.0f, 1.0f, 0.0f};
  rmc_quake_start(rmc_game() + 0x599C, rmc_reg_short(0, 5) + 3, -0x21,
                  (u32)direction);
}
static void rmc_audio(u32 actor, u32 id, u32 position) {
  const s32 reverb = rmc_reverb(R_MC(s8, actor + 0x326));
  rmc_sound(id, position, 0, reverb);
}
static void rmc_ground_init(unsigned int *ground) {
  rmc_ground_construct((u32)ground);
  u8 *p = (u8 *)ground;
  for (u32 i = 0; i < 7; i++)
    p[0x44 + i] = 0;
  ground[0x50 / 4] = 1;
  ground[1] = (unsigned int)((u32)ground + 0x4C);
  ground[0] = (unsigned int)((u32)ground + 0x40);
  ground[0x4C / 4] = 0x1000BDFC;
  ground[0x40 / 4] = 0x1000BE0C;
  ground[0x20 / 4] = 0x1000BDEC;
  ground[0x10 / 4] = 0x1000BDDC;
}
static void rmc_ground_finish(unsigned int *ground) {
  ground[0x20 / 4] = 0x1000BDEC;
  ground[0x40 / 4] = 0x1000BE0C;
  ground[0x4C / 4] = 0x1000BDCC;
  rmc_ground_destroy((u32)ground, 0);
}
static void rmc_track_center(u32 actor) {
  rmc_calculate(actor + 0x3EAC, R_MC(f32, actor + 0x314), 0.1f, 100.0f);
  f32 y = R_MC(f32, actor + 0x1B88) + rmc_reg_float(0, 10) + 200.0f;
  const f32 minimum = (rmc_reg_float(0, 12) + 100.0f) + rmc_hio_height();
  if (y < minimum)
    y = minimum;
  rmc_calculate(actor + 0x3EB0, y, 0.1f, 100.0f);
  rmc_calculate(actor + 0x3EB4, R_MC(f32, actor + 0x31C), 0.1f, 100.0f);
}
static void randomizer_molgera_camera_body(u32 actor) {
  /* Context callbacks precede the switch load, as in the complete native body.
   */
  const u32 player = R_MC(u32, rmc_game() + 0x5B2C);
  const s32 camera_id = R_MC(s8, rmc_game() + 0x5B30);
  const u32 camera = R_MC(u32, rmc_game() + 0x5AF8 + (u32)camera_id * 0x34);
  f32 offset[3], temporary[3];
  switch (R_MC(s16, actor + 0x3E96)) {
  case 0:
    break;
  case 1: {
    if (R_MC(u16, actor + 0xF8) != 2) {
      rmc_order(actor, 2, 0xFFFF, 0);
      R_MC(u16, actor + 0xFA) |= 2;
      return;
    }
    R_MC(s16, actor + 0x3E96)++;
    const u32 camera2 = R_MC(u32, rmc_game() + 0x5AF8);
    rmc_copy_vector(actor + 0x3EA0, camera2 + 0xDC);
    rmc_copy_vector(actor + 0x3EAC, camera2 + 0xE8);
    rmc_stop(camera + 0x248);
    rmc_trim(camera + 0x248, 2);
    R_MC(s16, actor + 0x3E98) = 0;
    R_MC(s16, actor + 0x3E9A) = 0;
    R_MC(f32, actor + 0x3EC0) = 55.0f;
    R_MC(f32, actor + 0x3EBC) = 0.0f;
    R_MC(s16, actor + 0x3E94) = 150;
  } /* fall through */
  case 2:
    offset[0] = 0.0f;
    offset[1] = 5000.0f;
    offset[2] = 0.0f;
    RMC_PLAYER_POSITION(player, (u32)offset, R_MC(s16, player + 0x32A));
    rmc_y_rotation(rmc_matrix(), R_MC(s16, actor + 0x32A));
    offset[0] = 0.0f;
    offset[1] = rmc_reg_float(0, 6) + 500.0f;
    offset[2] = rmc_reg_float(0, 7) + 1500.0f;
    rmc_transform((u32)offset, (u32)temporary);
    rmc_vector_add((u32)temporary, actor + 0x314, (u32)temporary);
    rmc_calculate(actor + 0x3EA0, temporary[0], 0.1f, 50.0f);
    rmc_calculate(actor + 0x3EA4, temporary[1], 0.1f, 50.0f);
    rmc_calculate(actor + 0x3EA8, temporary[2], 0.1f, 50.0f);
    offset[0] = 0.0f;
    offset[1] = rmc_reg_float(0, 8) + 400.0f;
    offset[2] = rmc_reg_float(0, 9);
    rmc_transform((u32)offset, (u32)temporary);
    rmc_vector_add((u32)temporary, actor + 0x314, (u32)temporary);
    rmc_calculate(actor + 0x3EAC, temporary[0], 0.1f, 50.0f);
    rmc_calculate(actor + 0x3EB0, temporary[1], 0.1f, 50.0f);
    rmc_calculate(actor + 0x3EB4, temporary[2], 0.1f, 50.0f);
    break;
  case 3:
    rmc_copy_vector((u32)temporary, player + 0x314);
    temporary[1] += 100.0f;
    rmc_calculate(actor + 0x3EAC, temporary[0], 0.1f, 100.0f);
    rmc_calculate(actor + 0x3EB0, temporary[1], 0.1f, 100.0f);
    rmc_calculate(actor + 0x3EB4, temporary[2], 0.1f, 100.0f);
    if (R_MC(s16, actor + 0x3E98) > 60)
      R_MC(s16, actor + 0x3E96) = 150;
    break;
  case 50:
    if (R_MC(u16, actor + 0xF8) != 2) {
      rmc_order(actor, 2, 0xFFFF, 0);
      R_MC(u16, actor + 0xFA) |= 2;
      return;
    }
    rmc_stop(camera + 0x248);
    rmc_trim(camera + 0x248, 2);
    R_MC(s16, actor + 0x3E96) = 51;
    R_MC(s16, actor + 0x3E98) = 0;
    R_MC(s16, actor + 0x3E9A) = 0;
    R_MC(f32, actor + 0x3EC0) = 55.0f;
    (void)rmc_game();
    R_MC(f32, actor + 0x3EBC) = 0.0f;
    R_MC(s16, player + 0x420) = 3;
    R_MC(u32, player + 0x428) = 0;
    rmc_stream_play();
    R_MC(s16, actor + 0x3E94) = 100; /* fall through */
  case 51:
    rmc_set_vector(actor + 0x3EAC, rmc_reg_float(10, 0) + 50.0f,
                   rmc_reg_float(10, 1) + 500.0f,
                   rmc_reg_float(10, 2) + 1000.0f);
    rmc_set_vector(actor + 0x3EA0, rmc_reg_float(10, 3) + 200.0f,
                   rmc_reg_float(10, 4) + 488.0f,
                   rmc_reg_float(10, 4) + 3575.0f);
    offset[0] = 0.0f;
    offset[1] = R_MC(f32, player + 0x318);
    offset[2] = rmc_reg_float(0, 4) + 3000.0f;
    RMC_PLAYER_POSITION(player, (u32)offset, -0x8000);
    if (R_MC(s16, actor + 0x3E98) >= 30) {
      if (R_MC(s16, actor + 0x3E98) == 30)
        rmc_quake();
      R_MC(f32, actor + 0x3EC4) = rmc_reg_float(10, 5) + 5.0f;
      if (R_MC(s16, actor + 0x3E98) == 50)
        R_MC(u32, player + 0x430) = 28;
    }
    if (R_MC(s16, actor + 0x3E9A) != (s32)rmc_reg_short(0, 3) + 170)
      break;
    R_MC(s16, actor + 0x3E96) = 52;
    R_MC(s16, actor + 0x3E98) = 0;
    R_MC(s8, actor + 0x3E8D) = 1; /* fall through */
  case 52:
    R_MC(f32, actor + 0x3EC4) = rmc_reg_float(10, 6) + 1.5f;
    R_MC(s8, actor + 0x1AE8) = 10;
    if (R_MC(s16, actor + 0x3E98) >= 150) {
      if (R_MC(s16, actor + 0x3E98) == 150)
        rmc_set_vector(actor + 0x3EAC, 25.0f, rmc_reg_float(0, 8) + 597.0f,
                       1578.0f);
      R_MC(f32, actor + 0x3EAC) -= rmc_reg_float(0, 4) + 0.5f;
      rmc_set_vector(actor + 0x3EA0, -156.0f, 767.0f, 2148.0f);
    } else {
      if (R_MC(s16, actor + 0x3E98) == 20) {
        R_MC(s8, actor + 0x1A69) = 1;
        for (u32 i = 3; i < 6; i++)
          rmc_audio(actor, 0x69E1,
                    (u32)&WWHD_GAME_DATA(0x10462AF4, u8) + 12 * i);
      }
      rmc_set_vector(actor + 0x3EAC, -1110.0f, 1598.0f, -45.0f);
      rmc_set_vector(actor + 0x3EA0, -4426.0f, 2574.0f, 888.0f);
    }
    if (R_MC(s16, actor + 0x3E9A) != (s32)rmc_reg_short(0, 5) + 640)
      break;
    R_MC(s16, actor + 0x3E96) = 53;
    R_MC(s16, actor + 0x3E98) = 0;
    R_MC(f32, actor + 0x3EC0) = 45.0f;
    R_MC(f32, actor + 0x1A64) = 900.0f;
    offset[0] = R_MC(f32, player + 0x314);
    offset[1] = R_MC(f32, player + 0x318) + 400.0f;
    offset[2] = R_MC(f32, player + 0x31C);
    RMC_PLAYER_POSITION(player, (u32)offset, -0x8000); /* fall through */
  case 53:
    if (R_MC(s16, actor + 0x3E98) >= 70 &&
        R_MC(s16, actor + 0x3E98) < (s16)(rmc_reg_short(0, 8) + 90)) {
      if (R_MC(s16, actor + 0x3E98) == 83)
        R_MC(u32, player + 0x430) = 49;
      rmc_set_vector(actor + 0x3EAC, R_MC(f32, player + 0x314),
                     (R_MC(f32, player + 0x380) + rmc_reg_float(0, 6)) - 30.0f,
                     R_MC(f32, player + 0x31C));
      rmc_set_vector(
          actor + 0x3EA0, R_MC(f32, player + 0x314) + rmc_reg_float(0, 7),
          (R_MC(f32, player + 0x380) + rmc_reg_float(0, 8)) - 30.0f,
          (R_MC(f32, player + 0x31C) - 200.0f) + rmc_reg_float(0, 9));
    } else {
      rmc_set_vector(actor + 0x3EAC, 6.0f, 1166.0f, -319.0f);
      rmc_set_vector(actor + 0x3EA0, 180.0f, 1362.0f, 3253.0f);
      rmc_calculate(actor + 0x3EC0, 55.0f, 0.1f, 0.1f);
    }
    if (R_MC(s16, actor + 0x3E9A) < 750)
      R_MC(f32, actor + 0x3EC4) = rmc_reg_float(10, 7) + 2.0f;
    if (R_MC(s16, actor + 0x3E9A) != (s32)rmc_reg_short(0, 3) + 760)
      break;
    R_MC(s16, actor + 0x3E96) = 60;
    R_MC(s16, actor + 0x3E98) = 0;
    rmc_quake_stop(rmc_game() + 0x599C, -1);
    R_MC(s16, actor + 0x3E94) = 200; /* fall through */
  case 60:
    if (R_MC(s16, actor + 0x3E94) > 2)
      rmc_angle_calculate(actor + 0x3E94, 1, 1, 1);
    rmc_track_center(actor);
    rmc_calculate(actor + 0x3EA0, R_MC(f32, actor + 0x314) * 0.5f, 0.05f,
                  50.0f);
    rmc_calculate(actor + 0x3EA4,
                  rmc_hio_height() + 100.0f + rmc_reg_float(0, 15) + 300.0f,
                  0.1f, 50.0f);
    rmc_calculate(actor + 0x3EA8,
                  (R_MC(f32, actor + 0x31C) + 2000.0f) + rmc_reg_float(0, 9),
                  0.05f, 50.0f);
    if (R_MC(s16, actor + 0x3E98) > 550) {
      rmc_calculate(actor + 0x3EC0, 65.0f, 0.05f, 0.1f);
      rmc_report(30, 360, 0x1000BE7C, R_MC(f32, actor + 0x3EC0));
    }
    if (R_MC(s16, actor + 0x3E9A) == (s32)rmc_reg_short(0, 4) + 1460) {
      R_MC(s16, actor + 0x3E96) = 150;
      rmc_bgm(R_MC(u8, rmc_game() + 0x5134) == 'X' ? 0x8000004C : 0x80000029);
      R_MC(s8, actor + 0x3EC8) = 1;
      rmc_dungeon_item(WWHD_GAME_DATA(0x101F84DC, u32) + 0x798, 5);
    }
    break;
  case 100: {
    if (R_MC(u16, actor + 0xF8) != 2) {
      rmc_order(actor, 2, 0xFFFF, 0);
      R_MC(u16, actor + 0xFA) |= 2;
      return;
    }
    rmc_stop(camera + 0x248);
    rmc_trim(camera + 0x248, 2);
    R_MC(s16, actor + 0x3E96) = 101;
    R_MC(s16, actor + 0x3E98) = 0;
    R_MC(s16, actor + 0x3E9A) = 0;
    R_MC(f32, actor + 0x3EC0) = 55.0f;
    const u32 camera2 = R_MC(u32, rmc_game() + 0x5AF8);
    rmc_copy_vector(actor + 0x3EA0, camera2 + 0xDC);
    rmc_copy_vector(actor + 0x3EAC, camera2 + 0xE8);
    R_MC(f32, actor + 0x3EBC) = 0.0f;
    R_MC(s16, player + 0x420) = 3;
    R_MC(u32, player + 0x428) = 0;
    R_MC(f32, rmc_target()) = R_MC(f32, actor + 0x314) - 100.0f;
    R_MC(f32, rmc_target() + 4) = R_MC(f32, player + 0x318);
    R_MC(f32, rmc_target() + 8) = R_MC(f32, actor + 0x31C) + 777.0f;
    /* Upstream fix: only X/Z, after all three native target stores. */
    randomizer_molgera_safe_target(rmc_target());
    R_MC(s16, actor + 0x3E94) = 150;
  } /* fall through */
  case 101: {
    unsigned int ground[0x54 / 4];
    rmc_ground_init(ground);
    rmc_copy_vector((u32)ground + 0x24, rmc_target());
    R_MC(f32, (u32)ground + 0x28) += 1000.0f;
    const f64 height = rmc_ground_cross(rmc_game() + 0x12A0, (u32)ground);
    R_MC(f32, rmc_target() + 4) = height != -1.0e9f ? (f32)height : 3000.0f;
    RMC_PLAYER_POSITION(player, rmc_target(), -0x8000);
    rmc_ground_finish(ground);
    rmc_track_center(actor);
    rmc_calculate(actor + 0x3EA0, R_MC(f32, actor + 0x314), 0.05f, 50.0f);
    rmc_calculate(actor + 0x3EA4,
                  rmc_hio_height() + 100.0f + rmc_reg_float(0, 15) + 300.0f,
                  0.1f, 50.0f);
    rmc_calculate(actor + 0x3EA8,
                  (R_MC(f32, actor + 0x31C) + 2200.0f) + rmc_reg_float(0, 9),
                  0.05f, 50.0f);
    rmc_angle_calculate(actor + 0x3E94, 1, 1, 1);
    break;
  }
  case 102:
    for (u32 i = 0; i < 3; i++)
      rmc_calculate(
          actor + 0x3EAC + 4 * i,
          R_MC(f32, actor + 0x584 + (s32)R_MC(s8, actor + 0x3E9C) * 12 + 4 * i),
          0.1f, R_MC(f32, actor + 0x3EBC) * 200.0f);
    rmc_calculate(actor + 0x3EBC, 1.0f, 1.0f, 0.02f);
    if (R_MC(s16, actor + 0x3E98) == 50)
      R_MC(u32, player + 0x430) = 26;
    break;
  case 104:
    R_MC(f32, actor + 0x3EC0) = rmc_reg_float(0, 6) + 55.0f;
    if (R_MC(s16, actor + 0x3E98) == 20) {
      offset[0] = 1.0f;
      offset[1] = 1.0f;
      offset[2] = 1.0f;
      R_MC(u32, actor + 0x1B64) = rmc_item_create(
          actor + 0x1B68, 0, R_MC(s8, actor + 0x326), 0, (u32)offset, 1);
    }
    if (R_MC(s16, actor + 0x3E98) >= 44) {
      R_MC(s16, actor + 0x3E96) = 105;
      R_MC(s16, actor + 0x3E98) = 0;
    }
    break;
  case 105: {
    rmc_set_vector(actor + 0x3EA0,
                   (R_MC(f32, player + 0x314) - 300.0f) + rmc_reg_float(0, 8),
                   ((R_MC(f32, player + 0x318) - 50.0f) + rmc_reg_float(0, 9)) +
                       200.0f,
                   (R_MC(f32, player + 0x31C) - 600.0f) + rmc_reg_float(0, 10));
    rmc_set_vector(actor + 0x3EAC,
                   R_MC(f32, player + 0x314) + rmc_reg_float(0, 11),
                   R_MC(f32, player + 0x318) + rmc_reg_float(0, 12),
                   (R_MC(f32, player + 0x31C) - 350.0f) + rmc_reg_float(0, 13));
    unsigned int key = (unsigned int)R_MC(u32, actor + 0x1B64);
    const u32 item =
        key == 0xFFFFFFFFu ? 0 : rmc_actor_judge(0x025E1234, (u32)&key);
    if (R_MC(s16, actor + 0x3E98) < 10 && item) {
      rmc_set_vector(item + 0x314, R_MC(f32, player + 0x314),
                     R_MC(f32, player + 0x318) + 300.0f,
                     R_MC(f32, player + 0x31C) - 350.0f);
      rmc_set_vector(item + 0x340, 0.0f, 0.0f, 0.0f);
    }
    if ((s32)R_MC(s16, actor + 0x3E98) == rmc_reg_short(10, 2) + 15)
      R_MC(u32, player + 0x430) = 50;
    if ((s32)R_MC(s16, actor + 0x3E98) == rmc_reg_short(10, 3) + 53)
      R_MC(u32, player + 0x430) = 29;
    if (R_MC(s16, actor + 0x3E98) >= 120 && rmc_reg_short(0, 3) == 0) {
      R_MC(s16, actor + 0x3E96) = 106;
      R_MC(s16, actor + 0x3E98) = 0;
      R_MC(s16, actor + 0x322) = 0;
      R_MC(f32, actor + 0x1A4C) = 0.0f;
      R_MC(f32, actor + 0x1A54) = 0.0f;
      R_MC(u8, actor + 0x1A48) = 202;
      R_MC(s8, actor + 0x1AE9) = 0;
      R_MC(s8, actor + 0x3E8D) = 5;
      R_MC(s16, actor + 0x3E94) = 100;
    }
    break;
  }
  case 106:
    if (R_MC(s16, actor + 0x3E98) == 50)
      R_MC(s8, actor + 0x1A68)++;
    if (R_MC(s16, actor + 0x3E98) == 110)
      R_MC(s8, actor + 0x1A69)++;
    rmc_calculate(actor + 0x1A64, rmc_reg_float(0, 7) + 400.0f, 0.1f, 1.0f);
    R_MC(f32, actor + 0x3EAC) = 0.0f;
    R_MC(f32, actor + 0x3EB4) = 0.0f;
    R_MC(f32, actor + 0x3EB0) = rmc_reg_float(0, 12) + 200.0f;
    R_MC(s16, actor + 0x322) = (s16)(R_MC(s16, actor + 0x322) +
                                     rmc_integer(R_MC(f32, actor + 0x3EBC)));
    if ((s32)R_MC(s16, actor + 0x3E98) < rmc_reg_short(0, 6) + 400)
      rmc_calculate(actor + 0x3EBC, 40.0f, 1.0f, 0.5f);
    if ((s32)R_MC(s16, actor + 0x3E98) > rmc_reg_short(0, 6) + 600)
      rmc_calculate(actor + 0x3EBC, 0.0f, 1.0f, 0.5f);
    rmc_y_rotation(rmc_matrix(), R_MC(s16, actor + 0x322));
    offset[0] = 0.0f;
    offset[1] = rmc_reg_float(0, 13) + 1200.0f;
    offset[2] = rmc_reg_float(0, 14) + 2600.0f;
    rmc_transform((u32)offset, actor + 0x3EA0);
    if ((s32)R_MC(s16, actor + 0x3E98) == rmc_reg_short(0, 6) + 520) {
      R_MC(u8, actor + 0x1A48) = 200;
      rmc_dungeon_item(WWHD_GAME_DATA(0x101F84DC, u32) + 0x798, 3);
      offset[0] = 0.0f;
      offset[1] = 300.0f;
      offset[2] = 0.0f;
      rmc_warp_create((u32)offset, 0, R_MC(s8, actor + 0x326), 0);
      R_MC(s8, actor + 0x3EC8) = 0;
    }
    if ((s32)R_MC(s16, actor + 0x3E98) != rmc_reg_short(0, 6) + 680)
      break;
    R_MC(s16, actor + 0x3E96) = 150; /* fall through */
  case 150: {
    R_MC(u16, rmc_game() + 0x52B8) |= 8;
    R_MC(s16, actor + 0x3E96) = 0;
    f32 center[3], eye[3];
    rmc_copy_vector((u32)center, actor + 0x3EAC);
    rmc_copy_vector((u32)eye, actor + 0x3EA0);
    rmc_reset(camera + 0x248, (u32)center, (u32)eye);
    rmc_start(camera + 0x248);
    rmc_trim(camera + 0x248, 0);
    R_MC(s16, actor + 0x3E94) = 1;
    break;
  }
  default:
    break;
  }
  if (R_MC(s16, actor + 0x3E96) != 0) {
    const s16 timer = R_MC(s16, actor + 0x3E98);
    const f32 amount = R_MC(f32, actor + 0x3EC4);
    const f32 fx = rmc_sine(timer * 0x3300) * amount,
              fy = rmc_cosine(timer * 0x3000) * amount,
              fz = rmc_sine(timer * 0x3700) * amount;
    const f32 ex = R_MC(f32, actor + 0x3EA0) + fx,
              ey = R_MC(f32, actor + 0x3EA4) + fy,
              ez = R_MC(f32, actor + 0x3EA8) + fz;
    const f32 cx = R_MC(f32, actor + 0x3EAC) - fx,
              cy = R_MC(f32, actor + 0x3EB0) - fy,
              cz = R_MC(f32, actor + 0x3EB4) - fz;
    s16 bank = 0;
    if (amount > 0.1f) {
      const f32 cosine = rmc_cosine(timer * 0x1C00);
      const f64 random_bank = rmc_random(20.0);
      const f32 sum = (f32)((f64)(rmc_reg_float(10, 8) + 50.0f) + random_bank);
      bank = (s16)rmc_integer(cosine * sum);
    }
    f32 center[3] = {cx, cy, cz}, eye[3] = {ex, ey, ez};
    rmc_set(camera + 0x248, (u32)center, (u32)eye, bank,
            R_MC(f32, actor + 0x3EC0));
    rmc_decelerate(actor + 0x3EC4, 1.0f, rmc_reg_float(0, 16) + 2.0f);
    rmc_report(30, 390, 0x1000BE88, R_MC(s16, actor + 0x3E96));
    rmc_report(30, 410, 0x1000BE9C, R_MC(s16, actor + 0x3E9A));
    rmc_report(30, 430, 0x1000BEB0, R_MC(s16, actor + 0x3E98));
    R_MC(s16, actor + 0x3E98)++;
    R_MC(s16, actor + 0x3E9A)++;
  }
  if (R_MC(s16, actor + 0x3E96) >= 50 && R_MC(s16, actor + 0x3E96) < 100) {
    if (R_MC(s16, actor + 0x3E9A) > 40 && R_MC(s16, actor + 0x3E9A) < 730)
      rmc_audio(actor, 0x105E, 0);
    if (R_MC(s16, actor + 0x3E9A) > 340 && R_MC(s16, actor + 0x3E9A) < 700)
      rmc_audio(actor, 0x1077, (u32)&WWHD_GAME_DATA(0x10462ADC, u8));
  }
  if (R_MC(s16, actor + 0x3E96) >= 100 && R_MC(s16, actor + 0x3E9A) >= 700 &&
      R_MC(s16, actor + 0x3E9A) <= 1200)
    rmc_audio(actor, 0x1076, (u32)&WWHD_GAME_DATA(0x10462ADC, u8));
}

/* Compose the existing reward-factory scope with the complete consumer. The
 * saved scope is restored even when event acquisition returns early. */
WWHD_REPLACE(0x02101F30, void, randomizer_molgera_camera, (u32 actor)) {
  const u32 active = randomizer_boss_factory_active;
  const u32 item = randomizer_boss_factory_item;
  randomizer_boss_factory_active = 1;
  randomizer_boss_factory_item = init_reward_boss[5];
  randomizer_molgera_camera_body(actor);
  randomizer_boss_factory_active = active;
  randomizer_boss_factory_item = item;
}
