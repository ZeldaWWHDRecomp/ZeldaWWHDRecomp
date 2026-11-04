/* MIT upstream remove_cutscenes.asm replaces the complete checkEndDemo
 * predicate. The native CreateInit boss-completion gate, effects, event lookup
 * and setEndAnim remain in their original consumers. */
#pragma once
WWHD_REPLACE(0x024D602C,s32,randomizer_boss_warp_demo_complete,(u32 actor)) {
    (void)actor;
    return 1;
}
