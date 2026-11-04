/* Complete public CC0 hdWarpMap_c::createChild026F002C from
 * hd_screen_warp_map.cpp a874e1a9. MIT misc_rando_features appends one pane
 * name at index10; allocation, constructor/setup and native fallback stay. */
#pragma once
#ifndef R_FFW
#define R_FFW(t, a) (*(volatile t *)(a))
#endif
#ifndef RFF_STRING_TEXT
#define RFF_STRING_TEXT(object) R_FFW(u32, object)
#endif
#ifndef RFF_ASSURE
#define RFF_ASSURE(object)                                                     \
  ((void (*)(u32))(R_FFW(u32, R_FFW(u32, (object) + 4) + 0x14)))(object)
#endif
WWHD_GAME_FUNC(0x0273B050, u32, rff_allocate,
               (u32 size, u32 heap, u32 alignment));
WWHD_GAME_FUNC(0x02677BFC, u32, rff_chart_construct, (u32 child));
WWHD_GAME_FUNC(0x026EEC90, u32, rff_area_construct, (u32 child));
WWHD_GAME_FUNC(0x02678308, u32, rff_chart_create,
               (u32 child, u32 first, u32 second, u32 description));
WWHD_GAME_FUNC(0x026EECFC, u32, rff_area_setup,
               (u32 child, u32 first, u32 second, u32 description));
WWHD_GAME_FUNC(0x026F92C0, u32, rff_base_child,
               (u32 self, u32 index, u32 description));
static s32 rff_same_name(u32 name, u32 expected) {
  RFF_ASSURE(name);
  const u32 text = RFF_STRING_TEXT(name);
  RFF_ASSURE(expected);
  if (text == RFF_STRING_TEXT(expected))
    return 1;
  const u32 left = RFF_STRING_TEXT(name), right = RFF_STRING_TEXT(expected);
  for (u32 index = 0; index < 0x40001; index++) {
    const u8 value = R_FFW(u8, left + index);
    if (value != R_FFW(u8, right + index))
      return 0;
    if (!value)
      return 1;
  }
  return 0;
}
static u32 rff_child_slot(u32 self, u32 index) {
  const u32 count = R_FFW(u32, self + 0x48), children = R_FFW(u32, self + 0x4C);
  return children + (index < count ? index * 4 : 0);
}
WWHD_REPLACE(0x026F002C, u32, randomizer_ff_warp_child,
             (u32 self, u32 description)) {
  const u32 heap = R_FFW(u32, WWHD_GAME_DATA(0x1018C404, u32) + 0x10);
  static const char ff_name[] = "L_WarpArea_09";
  const u32 custom[2] = {(u32)ff_name, (u32)&WWHD_GAME_DATA(0x1010394C, u8)};
  for (u32 index = 0; index < 11; index++) {
    const u32 expected = index == 10
                             ? (u32)custom
                             : (u32)&WWHD_GAME_DATA(0x1049D634, u8) + index * 8;
    RFF_ASSURE(description + 0x14);
    if (!rff_same_name(description + 0x14, expected))
      continue;
    RFF_ASSURE(description + 8);
    if (rff_same_name(description + 8, (u32)&WWHD_GAME_DATA(0x10495F0C, u8))) {
      const u32 slot = rff_child_slot(self, index);
      u32 child = rff_allocate(0x164, heap, 4);
      if (child)
        child = rff_chart_construct(child);
      R_FFW(u32, slot) = child;
      if (!R_FFW(u32, rff_child_slot(self, index)))
        return 0;
      return rff_chart_create(R_FFW(u32, rff_child_slot(self, index)), 0, 0,
                              description);
    }
    RFF_ASSURE(description + 8);
    if (rff_same_name(description + 8, (u32)&WWHD_GAME_DATA(0x1049D590, u8))) {
      const u32 slot = rff_child_slot(self, index);
      u32 child = rff_allocate(0x50, heap, 4);
      if (child)
        child = rff_area_construct(child);
      R_FFW(u32, slot) = child;
      if (!R_FFW(u32, rff_child_slot(self, index)))
        return 0;
      return rff_area_setup(R_FFW(u32, rff_child_slot(self, index)), 0, 0,
                            description);
    }
    return rff_base_child(self, index, description);
  }
  return 1;
}
