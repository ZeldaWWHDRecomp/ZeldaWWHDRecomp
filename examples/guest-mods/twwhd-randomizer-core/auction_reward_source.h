/* MIT TWWHD Randomizer1.2.0 flexible_item_locations.asm, SuperDude88(2024).
 * Public CC0 a874e1a9 d_a_auction.cpp: AuctionItem is eight bytes; the
 * fifth entry is the 100-rupee auction. Its item byte is seed-owned, whereas
 * this obtained-event halfword is source bookkeeping independent of inventory. */
#pragma once
static void randomizer_initialize_auction100_source(void) {
    WWHD_GAME_DATA(0x10190192,u16)=0x6B01;
}
