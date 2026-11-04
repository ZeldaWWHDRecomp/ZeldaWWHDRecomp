/* MIT tweaks.cpp::add_pirate_ship_to_windfall changes animation entry4's
 * signed BCK index to5. CC0 a874e1a9 d_a_npc_ls1.cpp setAnm_NUM (0227FF7C)
 * passes this entry to setAnm_anm (0227FEE4), preserving BTP, morph, speed
 * and loop mode. The TU initializer022832FC does not rewrite this table.
 * GameMain entry precedes actor resource/animation selection. Preparation
 * validates all14 entries, permitting only this single byte difference. */
#pragma once

WWHD_HOOK(0x028EA120, randomizer_initialize_windfall_seed_data, (void)) {
    WWHD_GAME_DATA(0x101BFFC4, u8) = 5;
}
