/* Preserve Salvatore's native minigame/event flow; change only its three seed prizes.
 * Reward sites and mode offsets verified in public CC0 d_a_npc_kg1_dialogue.cpp. */
#pragma once
static u32 randomizer_battlesquid_actor;
WWHD_GAME_ORIGINAL(0x02266D60,void,randomizer_native_battlesquid_wait,(u32 actor));
WWHD_REPLACE(0x02266D60,void,randomizer_battlesquid_wait,(u32 actor)) {
    const u32 previous=randomizer_battlesquid_actor;
    randomizer_battlesquid_actor=actor;
    randomizer_native_battlesquid_wait(actor);
    randomizer_battlesquid_actor=previous;
}
