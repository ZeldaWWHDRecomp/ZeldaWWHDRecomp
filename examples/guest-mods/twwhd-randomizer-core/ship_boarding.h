/* make_game_nonlinear.asm 0247A574 -> 0247A5CC, MIT TWWHD Randomizer1.2.0.
 * Retain the queried event's native callback, then select the existing boarding
 * branch irrespective of its result. No save or attention flags are prewritten. */
#pragma once
#include "ship_boarding_state.h"
WWHD_GAME_ORIGINAL(0x02477A24,s32,randomizer_native_ship_execute,(u32 actor));
WWHD_REPLACE(0x02477A24,s32,randomizer_ship_execute,(u32 actor)) {
    const u32 previous=randomizer_ship_boarding_scope;
    randomizer_ship_boarding_scope=1;
    const s32 result=randomizer_native_ship_execute(actor);
    randomizer_ship_boarding_scope=previous;
    return result;
}
