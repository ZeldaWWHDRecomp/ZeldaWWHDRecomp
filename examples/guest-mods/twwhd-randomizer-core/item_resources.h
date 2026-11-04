/* Player-owned randomized resource descriptors; no executable patches. */
#pragma once
#include "seed_resources.h"
/* The native constructor initializes archive pointers. Apply the seed's
 * descriptors afterwards so those assignments cannot undo the expanded table. */
WWHD_HOOK_RETURN(0x02551CA4,randomizer_initialize_item_resources,(void)) {
    randomizer_apply_item_resources();
    wwhd_log("randomizer item resources initialized");
}
