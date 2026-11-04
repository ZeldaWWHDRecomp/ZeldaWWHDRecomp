/* Complete public CC0 connectRestrictedWidgets02681B10, hd_sea_chart_grid.cpp
 * a874e1a9. MIT misc_rando_features adds exactly the FF<->M&C diagonal links.
 * Retain both clearing passes, native callback sequence and missing-cell gates.
 */
#pragma once
#ifndef R_FFW
#define R_FFW(t, a) (*(volatile t *)(a))
#endif
WWHD_GAME_FUNC(0x0267A204, u32, rfc_quadrant, (u32 index));
WWHD_GAME_FUNC(0x02680DE0, u32, rfc_find, (u32 grid, u32 coordinate));
WWHD_GAME_FUNC(0x02680748, void, rfc_widget,
               (u32 cell, u32 direction, u32 widget));
WWHD_GAME_FUNC(0x02681AD0, void, rfc_adjacent,
               (u32 grid, u32 cell, u32 coordinate, u32 direction));
static void rfc_coordinate(u8 coordinate[2], u32 index) {
  const u32 packed = rfc_quadrant(index);
  coordinate[0] = (u8)(packed >> 24);
  coordinate[1] = (u8)(packed >> 16);
}
static u32 rfc_find_index(u32 grid, u32 index) {
  u8 coordinate[2];
  rfc_coordinate(coordinate, index);
  return rfc_find(grid, (u32)coordinate);
}
static void rfc_bind(u32 grid, u32 cell, u32 index, u32 direction) {
  u8 coordinate[2];
  rfc_coordinate(coordinate, index);
  rfc_adjacent(grid, cell, (u32)coordinate, direction);
}
WWHD_REPLACE(0x02681B10, void, randomizer_ff_warp_connections, (u32 grid)) {
  const u32 fallback = rfc_find_index(grid, 0);
  if (fallback) {
    for (s32 y = -3; y <= 3; y++)
      for (s32 x = -3; x <= 3; x++) {
        u8 coordinate[2] = {(u8)x, (u8)y};
        const u32 cell = rfc_find(grid, (u32)coordinate);
        if (cell && cell != fallback) {
          R_FFW(u8, cell + 0x9A) = 0;
          for (u32 direction = 0; direction < 8; direction++)
            rfc_widget(cell, direction, R_FFW(u32, fallback + 0x9C));
        }
      }
  }
  for (u32 index = 0; index < 10; index++) {
    const u32 cell = rfc_find_index(grid, index);
    if (cell)
      for (u32 direction = 0; direction < 8; direction++)
        rfc_widget(cell, direction, 0);
  }
  const u32 mother = rfc_find_index(grid, 0);
  if (mother) {
    R_FFW(u8, mother + 0x9A) = 1;
    rfc_bind(grid, mother, 1, 7);
    rfc_bind(grid, mother, 7, 3);
    rfc_bind(grid, mother, 7, 5);
    rfc_bind(grid, mother, 9, 1);
    /* This entire FF branch is inside the native M&C-present block. */
    const u32 fortress = rfc_find_index(grid, 9);
    if (fortress) {
      R_FFW(u8, fortress + 0x9A) = 1;
      rfc_bind(grid, fortress, 0, 5);
    }
  }
  /* Rows preserve public-source order, including repeated target callbacks. */
  static const struct {
    u8 index, count;
    u8 target[7], direction[7];
  } rows[] = {
      {1, 4, {0, 2, 7, 7}, {6, 7, 3, 4}},
      {2, 3, {1, 7, 7}, {6, 4, 3}},
      {7, 6, {0, 0, 1, 3, 3, 4}, {1, 0, 2, 4, 3, 5}},
      {3, 5, {7, 7, 4, 8, 8}, {0, 2, 7, 3, 5}},
      {4, 6, {7, 7, 3, 8, 8, 5}, {1, 0, 6, 4, 3, 5}},
      {8, 7, {3, 3, 4, 5, 6, 6, 6}, {1, 0, 2, 7, 4, 3, 6}},
      {5, 3, {4, 4, 8}, {1, 0, 6}},
      {6, 3, {8, 8, 8}, {0, 2, 7}},
  };
  for (u32 row = 0; row < sizeof(rows) / sizeof(rows[0]); row++) {
    const u32 cell = rfc_find_index(grid, rows[row].index);
    if (!cell)
      continue;
    R_FFW(u8, cell + 0x9A) = 1;
    for (u32 link = 0; link < rows[row].count; link++)
      rfc_bind(grid, cell, rows[row].target[link], rows[row].direction[link]);
  }
}
