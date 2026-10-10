/* Public HD call-site contracts at 47e1dbc3886cfd8233859dffd73efc41a04a9130; CC0-1.0.
 * Source: wwhd_src/d/actor/d_a_player_main_07.cpp.
 * Reset discards the result, matching the public caller.
 * These declarations do not provide arbitrary melody playback. */
#pragma once
#include "../wwhd_guest.h"

#define WWHD_ADDR_mDoAud_tact_judge 0x025E1F34
WWHD_GAME_FUNC(0x025E1F34, s32, wwhd_tact_judge, (s32 index, s32 direction));
#define WWHD_ADDR_mDoAud_tact_getBeat 0x025E1EFC
WWHD_GAME_FUNC(0x025E1EFC, u32, wwhd_tact_get_beat, (void));
#define WWHD_ADDR_mDoAud_tact_reset 0x025E1E94
WWHD_GAME_FUNC(0x025E1E94, void, wwhd_tact_reset, (void));
/* wwhd_src/d/actor/d_a_obj_gong.cpp; no complete object size asserted by the public source. */
#define WWHD_OFFSET_daObjGong_Act_c_mpMorf 0x3B4
