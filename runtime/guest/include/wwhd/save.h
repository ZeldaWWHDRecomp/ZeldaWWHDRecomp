/* Generated from public wwhd_src/d/d_save.cpp (47e1dbc3886cfd8233859dffd73efc41a04a9130); CC0-1.0.
 * Partial status prefix only; no full save-object size is claimed. */
#pragma once
#include "../wwhd_guest.h"
#include "data.h"
#define WWHD_ADDR_save_info_pointer WWHD_ADDR_dComIfG_save_info_pointer
#define WWHD_OFFSET_save_info 0x20
typedef struct dSv_player_status_a_view {
    u16 max_life;
    u16 life;
    u16 rupees;
} dSv_player_status_a_view;
#define dComIfGs_player_status_a (*(volatile dSv_player_status_a_view*)(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer, u32) + WWHD_OFFSET_save_info))
