/* Ivan's prize call is verified in public CC0 d_a_npc_mk.cpp (022987B0).
 * Keep all native event movement/dialogue logic and replace only action 13's gift. */
#pragma once
static u32 randomizer_ivan_actor;
WWHD_GAME_ORIGINAL(0x022987B0,u8,randomizer_native_ivan_demo,(u32 actor));
WWHD_REPLACE(0x022987B0,u8,randomizer_ivan_demo,(u32 actor)) {
    const u32 previous=randomizer_ivan_actor;
    randomizer_ivan_actor=actor;
    const u8 result=randomizer_native_ivan_demo(actor);
    randomizer_ivan_actor=previous;
    return result;
}
