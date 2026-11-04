/* Complete public CC0 hdCompassClock_c::updateCompass0264C5A0, a874e1a9.
 * Public MIT inverted-compass option retains the signed half-turn heading
 * instead of negating it. Disabled seeds retain the literal native owner. */
#pragma once
#ifndef init_invert_sea_compass_x_axis
#error "Missing validated sea-compass seed option"
#endif
#if init_invert_sea_compass_x_axis != 0 && init_invert_sea_compass_x_axis != 1
#error "Invalid sea-compass seed option"
#endif
#if init_invert_sea_compass_x_axis
#ifndef R_SEA_COMPASS
#define R_SEA_COMPASS(type, address) (*(volatile type *)(address))
#endif
WWHD_GAME_FUNC(0x025D5218, u32, randomizer_compass_find_actor,
               (u32 predicate, u32 argument));
WWHD_REPLACE(0x0264C5A0, void, randomizer_inverted_sea_compass, (u32 self)) {
#pragma clang fp contract(off)
  u16 actor_id = 0xA5;
  const u32 actor = randomizer_compass_find_actor(0x025E121C, (u32)&actor_id);
  if (!actor)
    return;
  const s32 heading =
      (s16)((unsigned int)(s32)R_SEA_COMPASS(s16, actor + 0x32A) + 0x8000u);
  /* Native lookup callbacks may replace the pane. Capture it only afterwards.
   */
  const u32 pane = R_SEA_COMPASS(u32, self + 0x50);
  const f64 bias = WWHD_GAME_DATA(0x100EE848, f64);
  const unsigned int x = R_SEA_COMPASS(unsigned int, pane + 0x28);
  union {
    unsigned long long bits;
    f64 value;
  } converted = {.bits = 0x4330000000000000ull |
                         ((unsigned int)heading ^ 0x80000000u)};
  const f32 scale = WWHD_GAME_DATA(0x100EE850, f32);
  const f32 units = WWHD_GAME_DATA(0x100EE854, f32);
  const f32 angle = (f32)(converted.value - bias);
  const unsigned int y = R_SEA_COMPASS(unsigned int, pane + 0x2C);
  /* Original code also reads the old Z word into its stack copy. */
  (void)R_SEA_COMPASS(unsigned int, pane + 0x30);
  const f32 scaled = angle * scale;
  R_SEA_COMPASS(unsigned int, pane + 0x28) = x;
  const f32 rotated = scaled * units;
  const u8 flags = R_SEA_COMPASS(u8, pane + 0x44);
  R_SEA_COMPASS(unsigned int, pane + 0x2C) = y;
  R_SEA_COMPASS(f32, pane + 0x30) = rotated;
  R_SEA_COMPASS(u8, pane + 0x44) = flags | 0x10;
}
#endif
