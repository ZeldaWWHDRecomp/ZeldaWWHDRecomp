/* Full public CC0 FF warp lookup/opening/initialization/text consumers.
 * d/hd_screen_sea_chart_menu.cpp, hd_screen_warp_map.cpp and
 * hd_screen_sequence_window_sequence.cpp a874e1a9; MIT misc_rando_features.
 * Fixed seed quadrant9==0 is validated before source package conversion. */
#pragma once
#ifndef R_FFW
#define R_FFW(t, a) (*(volatile t *)(a))
#endif
WWHD_GAME_FUNC(0x0267A23C, u32, rfw_resource, (u32 index));
WWHD_GAME_FUNC(0x02680DE0, u32, rfw_grid_find, (u32 grid, u32 coordinate));
WWHD_GAME_FUNC(0x026DBC44, void, rfw_selection, (u32 selection, u32 value));
WWHD_GAME_FUNC(0x02680CB4, void, rfw_reset_details, (u32 details));
WWHD_GAME_FUNC(0x02679C6C, void, rfw_update_map, (u32 chart));
WWHD_GAME_FUNC(0x026EF280, void, rfw_refresh_chart, (u32 map));
WWHD_GAME_FUNC(0x026EEDAC, void, rfw_hide_area, (u32 area));
WWHD_GAME_FUNC(0x025F6FCC, void, rfw_restart_text,
               (u32 message, u32 position, u32 mode));
WWHD_GAME_FUNC(0x02704504, void, rfw_prepare_message,
               (u32 pane, u32 message, u32 flag));
WWHD_GAME_FUNC(0x027045B0, void, rfw_reset_message,
               (u32 pane, u32 archive, u32 label));
WWHD_REPLACE(0x0267A23C, u32, randomizer_ff_warp_resource, (u32 index)) {
  if (index == 9)
    return 0;
  return R_FFW(u32, (u32)&WWHD_GAME_DATA(0x100F7BAC, u8) +
                        (unsigned int)(index * 4));
}
WWHD_REPLACE(0x0267A250, s32, randomizer_ff_warp_find, (u32 resource)) {
  for (u32 index = 0; index < 10; index++)
    if (rfw_resource(index) == resource)
      return (s32)index;
  return -1;
}
WWHD_REPLACE(0x02678770, void, randomizer_ff_warp_open, (u32 self)) {
  const u32 selected = R_FFW(u32, R_FFW(u32, self + 0x14C) + 0x118);
  for (u32 index = 0; index < 10; index++) {
    const u32 cell =
        rfw_grid_find(R_FFW(u32, self + 0x14C),
                      (u32)&WWHD_GAME_DATA(0x10495480, u8) + index * 2);
    if (cell != selected)
      continue;
    const u32 owner = WWHD_GAME_DATA(0x101F8344, u32);
    const u32 selection = R_FFW(u32, owner + 0x1BC);
    if (!selection)
      continue;
    rfw_selection(selection, R_FFW(u8, selected + 0x97) ? index + 1 : 0);
    R_FFW(u32, self + 0x15C) = index;
    const u32 current = R_FFW(u32, self + 0x14C);
    R_FFW(u8, self + 0x160) = 1;
    R_FFW(u8, current + 0x151) = 1;
    return;
  }
}
WWHD_REPLACE(0x026EF2DC, void, randomizer_ff_warp_initialize, (u32 self)) {
  u32 chart = R_FFW(u32, self + 0x54);
  R_FFW(u32, self + 0x58) = 0xFFFFFFFFu;
  if (chart) {
    const u32 details = R_FFW(u32, chart + 0x14C);
    if (details) {
      rfw_reset_details(details);
      chart = R_FFW(u32, self + 0x54);
    }
    rfw_update_map(chart);
    R_FFW(u8, R_FFW(u32, self + 0x54) + 0x154) = 1;
  }
  rfw_refresh_chart(self);
  for (u32 index = 1; index <= 10; index++) {
    const u32 count = R_FFW(u32, self + 0x48);
    const u32 children = R_FFW(u32, self + 0x4C);
    rfw_hide_area(R_FFW(u32, children + (index < count ? index * 4 : 0)));
  }
}
WWHD_REPLACE(0x026D9C48, void, randomizer_ff_warp_confirmation, (u32 self)) {
  if (R_FFW(u8, self + 0xCF)) {
    rfw_restart_text(R_FFW(u32, self + 0x90), R_FFW(u32, self + 0xC4), 335);
    const u32 kind = R_FFW(u32, self + 0xA8);
    const u32 message = R_FFW(u32, self + 0x90);
    rfw_prepare_message(R_FFW(u32, self + 0x64 + (kind < 3 ? kind * 4 : 0)),
                        message, 1);
    R_FFW(u32, self + 0xC4) = R_FFW(u32, R_FFW(u32, self + 0x90) + 0x64C);
    return;
  }
  const u32 mode = R_FFW(u32, self + 0x98), text = R_FFW(u32, self + 0x8C),
            kind = R_FFW(u32, self + 0xA8);
  u32 label = R_FFW(u32, text + 0xC);
  if (mode == 2 && label == 8) {
    label = (unsigned int)(R_FFW(u32, self + 0xC0) + 8);
    if (label == 18)
      label = 51;
  }
  const u32 layout = R_FFW(u32, self + 0x44), lyt = R_FFW(u32, layout + 4);
  const u32 pane = R_FFW(u32, self + 0x64 + (kind < 3 ? kind * 4 : 0));
  const u32 archive[2] = {R_FFW(u32, lyt + 0x1C),
                          (u32)&WWHD_GAME_DATA(0x1010394C, u8)};
  static const char ff_label[] = "00076";
  const u32 custom[2] = {(u32)ff_label, (u32)&WWHD_GAME_DATA(0x1010394C, u8)};
  const u32 name = label == 51 ? (u32)custom
                               : (u32)&WWHD_GAME_DATA(0x1049B0F0, u8) +
                                     (unsigned int)(label * 8);
  rfw_reset_message(pane, (u32)archive, name);
}
