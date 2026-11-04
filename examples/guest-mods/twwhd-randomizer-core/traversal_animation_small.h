/* Complete public CC0 traversal consumers from d_a_player_main_04b/06/07.cpp
 * a874e1a9. MIT tweaks.cpp's reviewed relocation changes are local constants,
 * never changes to the port's optional traversal settings or shared floats. */
#pragma once
#ifndef R_TAS
#define R_TAS(t, a) (*(volatile t *)(a))
#endif
WWHD_GAME_FUNC(0x02019510, s32, rtas_rad_to_short, (f32 radians));
WWHD_GAME_FUNC(0x0243CEE0, s32, rtas_push_keep, (u32 player, s32 mode));
WWHD_GAME_FUNC(0x023DFDD8, void, rtas_common, (u32 player, s32 procedure));
WWHD_GAME_FUNC(0x023E0A04, void, rtas_animation,
               (u32 player, s32 animation, f32 rate, f32 start, s32 end,
                f32 morph));
WWHD_GAME_FUNC(0x025200D4, u32, rtas_game, (void));
WWHD_REPLACE(0x0242B800, f32, randomizer_crawl_move_speed, (u32 player)) {
#pragma clang fp contract(off)
  f32 frame = R_TAS(f32, player + 0x589C);
  if (!(frame < 17.0f))
    frame = frame - 17.0f;
  const f32 radians = frame * WWHD_GAME_DATA(0x10036084, f32);
  const u16 angle = (u16)rtas_rad_to_short(radians);
  const f32 rate = R_TAS(f32, player + 0x5898);
  const f32 amplitude = 6.0f * rate;
  const f32 sine =
      R_TAS(f32, (u32)&WWHD_GAME_DATA(0x104A44F8, u8) + ((u32)angle >> 3) * 8);
  return amplitude * sine;
}
WWHD_REPLACE(0x02417FF4, f32, randomizer_ladder_move_animation_speed,
             (u32 player)) {
  return 1.0f + R_TAS(f32, player + 0x6A08);
}
WWHD_REPLACE(0x0243D06C, s32, randomizer_push_move_initialize, (u32 player)) {
  if (!rtas_push_keep(player, 0))
    return 0;
  rtas_common(player, 0x33);
  rtas_animation(player, 0x7F, 1.4f, 0.0f, -1, 5.0f);
  R_TAS(s32, player + 0x69C0) = 1;
  const u32 game = rtas_game();
  R_TAS(u32, game + 0x5CD8) |= 0x04000000;
  return 1;
}
WWHD_REPLACE(0x0243D118, s32, randomizer_pull_move_initialize, (u32 player)) {
  if (!rtas_push_keep(player, 0))
    return 0;
  rtas_common(player, 0x34);
  rtas_animation(player, 0x80, 1.4f, 0.0f, -1, 5.0f);
  R_TAS(s32, player + 0x69C0) = 1;
  const u32 game = rtas_game();
  R_TAS(u32, game + 0x5CD8) |= 0x04000000;
  return 1;
}
WWHD_REPLACE(0x02418178, s32, randomizer_ladder_up_end_initialize,
             (u32 player, s32 left_foot)) {
  rtas_common(player, 0x39);
  rtas_animation(player, left_foot ? 0x86 : 0x85, 1.4f, 0.0f, -1, 3.0f);
  R_TAS(f32, player + 0x370) = 0.0f;
  R_TAS(f32, player + 0x374) = 0.0f;
  R_TAS(u8, player + 0x68DE) = 7;
  R_TAS(f32, player + 0x340) = 0.0f;
  R_TAS(f32, player + 0x6A14) = 0.0f;
  const u32 game = rtas_game();
  R_TAS(u32, game + 0x5CD8) |= 0x02000000;
  return 1;
}
WWHD_REPLACE(0x024184C8, void, randomizer_climb_up_start_sub, (u32 player)) {
  /* The relocated load feeds BOTH F3 and its F1 copy, not only animation rate.
   */
  rtas_animation(player, 0x84, 1.6f, 0.0f, -1, 1.6f);
  R_TAS(s32, player + 0x69C0) = 1;
  R_TAS(u8, player + 0x68DE) = 4;
  const u32 game = rtas_game();
  R_TAS(u32, game + 0x5CDC) |= 0x00010000;
}
