/* MIT upstream tweaks.cpp::fix_stone_head_bugs: change only profile bit 80.
 * Seed preparation validates the full resulting word against canonical data.
 * This runs before actors can cache their profile status. */
#pragma once
WWHD_HOOK(0x028EA120, randomizer_initialize_fixed_seed_data, (void)) {
    WWHD_GAME_DATA(0x101CA100, u32) &= ~0x80u;
}
