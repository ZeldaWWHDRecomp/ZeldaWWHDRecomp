/* Seed-specific rewards stored in game data, not executable instructions. */
#pragma once
#include "seed_reward_data.h"
#include "auction_reward_source.h"

/* Loaded sections and relocations are ready before the original game entry.
 * Apply once before any actor can cache a reward table entry. */
WWHD_HOOK(0x028EA120, randomizer_initialize_reward_data, (void)) {
    randomizer_apply_reward_data();
    randomizer_initialize_auction100_source();
}
