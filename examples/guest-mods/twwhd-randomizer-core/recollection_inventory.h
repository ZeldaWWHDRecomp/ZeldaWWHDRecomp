/* make_game_nonlinear.asm disables the original recollection-inventory snapshots.
 * Otherwise skipping an original boss can restore an uninitialized inventory
 * after a later recollection fight. Keep the player's current inventory intact.
 * Copyright (c) 2024 SuperDude88. MIT: see LICENSE-TWWHD-Randomizer.txt. */
#pragma once
WWHD_REPLACE(0x025221F0,void,randomizer_recollection_copy,(void)) {}
WWHD_REPLACE(0x025224A4,void,randomizer_recollection_set,(void)) {}
WWHD_REPLACE(0x02522C60,void,randomizer_recollection_restore,(void)) {}
