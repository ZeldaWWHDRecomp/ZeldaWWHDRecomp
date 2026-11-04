/* Complete public CC0 hang-wall-catch02418B48 and sidle-blend0242A218,
 * d_a_player_main_04b/06.cpp a874e1a9. MIT tweaks.cpp changes only the
 * hang rate0.8->1.6 and upright sidle rate1.6->2.0 at reviewed loads. */
#pragma once
#ifndef R_TAH
#define R_TAH(t, a) (*(volatile t *)(a))
#endif
WWHD_GAME_FUNC(0x028E8DE8, f64, rtah_square_distance, (u32 a, u32 b));
WWHD_GAME_FUNC(0x028F4384, f64, rtah_sqrt, (f64 square));
WWHD_GAME_FUNC(0x02008974, f64, rtah_ground, (u32 background, u32 check));
WWHD_GAME_FUNC(0x023DFDD8, void, rtah_common, (u32 player, s32 procedure));
WWHD_GAME_FUNC(0x023E0A04, void, rtah_animation,
               (u32 player, s32 animation, f32 rate, f32 start, s32 end,
                f32 morph));
WWHD_GAME_FUNC(0x025200D4, u32, rtah_game, (void));
WWHD_GAME_FUNC(0x023E0E50, void, rtah_move_animation,
               (u32 player, f32 blend, f32 rate, f32 start, s32 moving,
                s32 idle, s32 mode, f32 morph));
/* Native single multiply rounds the second physical FPR operand to25 bits,
 * even when the ABI caller supplied a wider double. */
static f32 rtah_morph(f64 parameter) {
#if defined(__powerpc__)
  f32 result;
  const f64 five = 5.0;
  __asm__("fmuls %0,%1,%2" : "=f"(result) : "f"(five), "f"(parameter));
  return result;
#else
  union { f64 value; unsigned long long bits; } rounded = {.value = parameter};
  rounded.bits = (rounded.bits & 0xFFFFFFFFF8000000ull) +
                 (rounded.bits & 0x8000000ull);
  return (f32)(5.0 * rounded.value);
#endif
}
static f32 rtah_fnmsub(f32 a, f32 b, f32 c) {
#if defined(__powerpc__)
#pragma clang fp contract(fast)
  volatile f32 difference = a * b - c;
  return -difference;
#else
  /* Host fixtures must retain PPC fusion on machines without hardware FMA. */
  volatile f32 difference = __builtin_fmaf(a, b, -c);
  return -difference;
#endif
}
WWHD_REPLACE(0x02418B48, s32, randomizer_hang_wall_catch_initialize,
             (u32 player)) {
#pragma clang fp contract(off)
  const f32 wall[3] = {R_TAH(f32, player + 0x732C), 0.0f,
                       R_TAH(f32, player + 0x7334)};
  const f32 position[3] = {R_TAH(f32, player + 0x314), 0.0f,
                           R_TAH(f32, player + 0x31C)};
  const f64 distance =
      rtah_sqrt(rtah_square_distance((u32)wall, (u32)position));
  const f32 radius = R_TAH(f32, player + 0x780) + 20.0f;
  if (distance > radius)
    return 0;
  const u16 angle = R_TAH(u16, player + 0x697C);
  const u32 entry =
      (u32)&WWHD_GAME_DATA(0x104A44F8, u8) + ((u32)angle >> 3) * 8;
  const f32 x =
      rtah_fnmsub(1.5f, R_TAH(f32, entry), R_TAH(f32, player + 0x732C));
  const f32 y = R_TAH(f32, player + 0x7330) + 10.0f;
  const f32 z =
      rtah_fnmsub(1.5f, R_TAH(f32, entry + 4), R_TAH(f32, player + 0x7334));
  R_TAH(f32, player + 0xB3C) = y;
  R_TAH(f32, player + 0xB38) = x;
  R_TAH(f32, player + 0xB40) = z;
  const f64 height = rtah_ground(rtah_game() + 0x12A0, player + 0xB14);
  const f32 difference = (f32)(height - (f64)R_TAH(f32, player + 0x7330));
  if (__builtin_fabsf(difference) > 30.1f)
    return 0;
  rtah_common(player, 0x31);
  rtah_animation(player, 0x4E, 1.6f, 0.0f, 5, 2.5f);
  R_TAH(f32, player + 0x340) = 0.0f;
  R_TAH(f32, player + 0x6A14) = 0.0f;
  R_TAH(f32, player + 0x318) = (f32)height;
  const u16 facing = (u16)(R_TAH(s16, player + 0x697C) + 0x8000);
  R_TAH(u16, player + 0x32A) = facing;
  R_TAH(u16, player + 0x322) = facing;
  R_TAH(f32, player + 0x31C) = z;
  R_TAH(f32, player + 0x314) = x;
  const u32 game = rtah_game();
  R_TAH(u32, game + 0x5CD8) |= 0x100;
  return 1;
}
WWHD_REPLACE(0x0242A218, void, randomizer_sidle_blend_animation,
             (u32 player, f64 parameter)) {
#pragma clang fp contract(off)
  f32 rate, end_ratio;
  s32 idle, moving;
  if (!(R_TAH(u32, player + 0x3B8) & 0x10000)) {
    rate = 2.0f;
    idle = 0x42;
    end_ratio = 1.0f;
    moving = R_TAH(u8, player + 0x68D4) == 2 ? 0x45 : 0x44;
  } else {
    rate = 1.8f;
    idle = 0x43;
    end_ratio = 0.98f;
    moving = R_TAH(u8, player + 0x68D4) == 2 ? 0x47 : 0x46;
  }
  const f32 ratio = R_TAH(f32, player + 0x6A14) / R_TAH(f32, player + 0x3C4);
  /* Keep physical F1 until the native fmuls; do not narrow before multiplying.
   */
  const f32 morph = rtah_morph(parameter);
  f32 blend;
  if (ratio < 0.0f) {
    blend = 1.0f;
    R_TAH(f32, player + 0x69F0) = 1.0f;
  } else if (ratio < 0.9f) {
    blend = 1.0f - ratio / 0.9f;
    R_TAH(f32, player + 0x69F0) = 1.0f;
  } else {
    blend = 0.0f;
    const f32 first = 1.0f - end_ratio, second = ratio - 0.9f,
              product = first * second;
    /* Native GHS folded the denominator to the0.1 constant, not1.0f-0.9f. */
    R_TAH(f32, player + 0x69F0) = 1.0f - product / 0.1f;
  }
  rtah_move_animation(player, blend, rate, 0.0f, moving, idle, 5, morph);
  const s16 end = R_TAH(s16, player + 0x58B2);
  R_TAH(s16, player + 0x58A2) = 28;
  R_TAH(f32, player + 0x58A8) = 0.0f;
  const f32 frame = (f32)(s32)end - 0.001f;
  R_TAH(f32, player + 0x58AC) = frame;
  R_TAH(f32, R_TAH(u32, player + 0x580C)) = frame;
}
