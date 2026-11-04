/* Complete CC0 a874e1a9 daHookshot_rockLineCallback02176CE0.
 * MIT fix_vanilla_bugs: a nearer collision without an actor hides the sight.
 * Preserve physical F1 precision and the late hit-position/status loads. */
#pragma once
WWHD_GAME_FUNC(0x028E8DE8, f64, randomizer_hookshot_square_distance,
               (u32 first, u32 second));
WWHD_REPLACE(0x02176CE0, void, randomizer_hookshot_sight,
             (u32 actor, u32 collision, u32 target, u32 unused)) {
    (void)unused;
    const f64 previous = randomizer_hookshot_square_distance(actor + 0xD528, actor + 0x314);
    const f64 current = randomizer_hookshot_square_distance(collision + 0x70, actor + 0x314);
    if (previous > current) {
        *(volatile unsigned int*)(actor + 0xD528) = *(volatile unsigned int*)(collision + 0x70);
        *(volatile unsigned int*)(actor + 0xD52C) = *(volatile unsigned int*)(collision + 0x74);
        *(volatile unsigned int*)(actor + 0xD530) = *(volatile unsigned int*)(collision + 0x78);
        *(volatile unsigned int*)(actor + 0xD50C) = target &&
            ((*(volatile unsigned int*)(target + 0x2E0) & 0x10280000u) != 0);
    }
}
