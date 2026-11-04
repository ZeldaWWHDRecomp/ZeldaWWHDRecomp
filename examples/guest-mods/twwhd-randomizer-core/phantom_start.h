/* Complete public CC0 d_a_fganon.cpp start02139AA0 (a874e1a9).
 * MIT fix_vanilla_bugs adds the one-sided player height gate before XZ math.
 * Shared pgm helpers come from phantom_maze.h; no global player/query override.
 */
#pragma once
WWHD_GAME_FUNC(0x025E1960, void, pgs_bgm_mute, (s32 frames));
WWHD_GAME_FUNC(0x025E1918, void, pgs_sub_bgm, (u32 id));
WWHD_REPLACE(0x02139AA0, void, randomizer_phantom_start, (u32 actor)) {
  const u32 player = R_PGM(u32, pgm_game() + 0x5B2C);
  const s16 mode = R_PGM(s16, actor + 0x5BC);
  R_PGM(u32, actor + 0x39C) = 0;
  R_PGM(s16, actor + 0x5E2) = 3;
  switch (mode) {
  case 0: {
    f32 offset[3];
    pgm_vector_subtract(player + 0x314, (u32)offset, actor + 0x2EC);
    /* PPC bge also takes the unordered result; the lower side stays unbounded.
     */
    if (!(offset[1] < 1000.0f))
      break;
    const f32 x = offset[0], z = offset[2];
    const f64 distance = pgm_sqrt(pgm_fma(x, x, z * z));
    const f32 radius = (f32)R_PGM(u8, actor + 0x3D9) * 10.0f;
    if (distance < radius) {
      R_PGM(s16, actor + 0x5BC) = 1;
      R_PGM(s16, actor + 0xD88) = 1;
      pgm_animation(actor, 0x16, 0.0f, 2, 1.0f, -1);
      pgs_bgm_mute(30);
    }
    break;
  }
  case 1: {
    const u32 target = R_PGM(u32, pgm_game() + 0x5B2C);
    R_PGM(s16, actor + 0x32A) = (s16)pgm_search_angle(actor, target);
    break;
  }
  case 2:
    pgm_animation(actor, 0x17, 5.0f, 2, 1.0f, -1);
    pgm_voice(actor, 0x4939);
    R_PGM(s16, actor + 0x5BC) = 3;
    break;
  case 4:
    R_PGM(s16, actor + 0x5BC) = 5;
    R_PGM(s16, actor + 0x5D8) = (s16)(pgm_reg_short(0, 0) + 80);
    pgm_animation(actor, 0x10, 10.0f, 2, 1.0f, -1);
    /* fall through */
  case 5: {
    s16 trigger = (s16)(pgm_reg_short(0, 0) + 75);
    if (R_PGM(s16, actor + 0x5D8) == trigger) {
      R_PGM(s8, actor + 0x63C) = 1;
      pgs_sub_bgm(0x80000041);
      trigger = (s16)(pgm_reg_short(0, 0) + 75);
    }
    if (R_PGM(s16, actor + 0x5D8) <= trigger)
      pgm_effect_sound(actor, 0x5152, 0);
    f32 offset[3];
    offset[0] = 0.0f;
    offset[1] = R_PGM(f32, actor + 0x370) * 0.5f;
    offset[2] = -R_PGM(f32, actor + 0x370);
    pgm_y_rotation(pgm_matrix(), R_PGM(s16, actor + 0x32A));
    pgm_transform((u32)offset, actor + 0x33C);
    pgm_vector_add_inplace(actor + 0x314, actor + 0x33C, actor + 0x314);
    const f32 target =
        R_PGM(s16, actor + 0x5D8) == 0 ? 0.0f : pgm_reg_float(0, 7) + 20.0f;
    pgm_calculate(actor + 0x370, target, 1.0f, pgm_reg_float(0, 5) + 0.5f);
    break;
  }
  default:
    break;
  }
}
