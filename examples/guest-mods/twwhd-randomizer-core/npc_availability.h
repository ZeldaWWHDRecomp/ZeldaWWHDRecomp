/* Early NPC availability from TWWHD Randomizer 1.2.0 make_game_nonlinear.asm.
 * Copyright (c) 2024 SuperDude88. MIT: LICENSE-TWWHD-Randomizer.txt.
 * Fishmen gate022DEF78. Complete sage Create consumers live in sage_dungeon_reentry.h. */
#pragma once
#include "npc_availability_state.h"

/* Each native create routine retains its allocation, stage/story checks and
 * return value. Only its first matching progression predicate is substituted. */
#define RANDOMIZER_NPC_SPAWN(address,name,member) \
    WWHD_GAME_ORIGINAL(address,u32,randomizer_native_##name##_create,(u32 actor)); \
    WWHD_REPLACE(address,u32,randomizer_##name##_create,(u32 actor)) { \
        const struct randomizer_spawn_queries previous=randomizer_spawn_query; \
        randomizer_spawn_query=(struct randomizer_spawn_queries){0}; \
        randomizer_spawn_query.member=1; \
        const u32 result=randomizer_native_##name##_create(actor); \
        randomizer_spawn_query=previous; \
        return result; \
    }
RANDOMIZER_NPC_SPAWN(0x022DED84,fish,fish)
#undef RANDOMIZER_NPC_SPAWN

WWHD_GAME_ORIGINAL(0x02520A84,u32,randomizer_native_fish_boss,(u32 stage));
WWHD_REPLACE(0x02520A84,u32,randomizer_fish_boss,(u32 stage)) {
    if(randomizer_spawn_query.fish && stage==3) {
        randomizer_spawn_query.fish=0;
        return 1;
    }
    return randomizer_native_fish_boss(stage);
}
