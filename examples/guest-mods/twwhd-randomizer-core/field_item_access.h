/* make_game_nonlinear.asm allows spoils/bait before their bags are owned.
 * Copyright (c) 2024 SuperDude88. MIT: see LICENSE-TWWHD-Randomizer.txt. */
#pragma once
WWHD_GAME_ORIGINAL(0x02551150,u8,randomizer_native_check_itemno,(s32 item));
WWHD_REPLACE(0x02551150,u8,randomizer_check_itemno,(s32 item)) {
    const u8 id=(u8)item;
    if(wwhd_isEmono(id) || wwhd_isEsa(id))return id;
    /* Preserve native magic, arrow, bomb and triple-heart conversion. */
    return randomizer_native_check_itemno(item);
}
