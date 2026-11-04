/* MIT tweaks.cpp reviewed fixed traversal metadata. Preparation validates all
 *13complete canonical block rows with only these fields changed, and17->25
 * for the unique grapple float. Apply before actors cache their data. */
#pragma once
#ifndef R_TRAVERSAL_DATA
#define R_TRAVERSAL_DATA(t, a) (*(volatile t *)(a))
#endif
WWHD_HOOK(0x028EA120, randomizer_initialize_traversal_data, (void)) {
  WWHD_GAME_DATA(0x100110C8, f32) = 25.0f;
  const u32 blocks = (u32)&WWHD_GAME_DATA(0x101CB424, u8);
  for (u32 index = 0; index < 13; index++) {
    R_TRAVERSAL_DATA(u16, blocks + index * 0x9C + 4) = 12;
    R_TRAVERSAL_DATA(u16, blocks + index * 0x9C + 10) = 12;
  }
}
