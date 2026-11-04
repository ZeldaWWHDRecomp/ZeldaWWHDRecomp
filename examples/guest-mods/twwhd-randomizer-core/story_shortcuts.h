/* TWWHD Randomizer 1.2.0 flexible_item_locations.asm and
 * make_game_nonlinear.asm. Copyright (c) 2024 SuperDude88.
 * MIT: LICENSE-TWWHD-Randomizer.txt. */
#pragma once

/* Upstream forces the result at 0259D780 inside this pure predicate. Do not
 * change actual Father Letter ownership, reserve inventory or delivery events.
 * This removes the missable first Medli instance from the randomized flow. */
WWHD_REPLACE(0x0259D734,u32,randomizer_letter_passed,(void)) {
    return 1;
}

/* Upstream returns immediately from these two methods. The sea-chart markers
 * must not follow the original linear pearl/story progression. */
WWHD_REPLACE(0x02677D3C,void,randomizer_skip_story_marker_binding,(u32 menu)) {
    (void)menu;
}
WWHD_REPLACE(0x02678B74,void,randomizer_skip_story_marker_update,(u32 menu)) {
    (void)menu;
}
