/* Generated named data addresses from public ZeldaWWHDDecomp/wwhd.
 * Revision: 47e1dbc3886cfd8233859dffd73efc41a04a9130
 * CC0-1.0; see public-wwhd-LICENSE. USA version 0.
 * Pointer slots contain guest addresses; tables contain no copied game bytes. */
#pragma once
#include "../wwhd_guest.h"

/* wwhd_src/include/d/d_com_inf_game.h */
#define WWHD_ADDR_dComIfG_save_info_pointer 0x101F84DC
/* wwhd_src/include/d/d_com_inf_game.h */
#define WWHD_ADDR_dComIfG_resControl_pointer 0x101F4F28
/* wwhd_src/include/bindings.h */
#define WWHD_ADDR_mDoMtx_stack_now 0x1048D0CC
/* wwhd_src/include/bindings.h */
#define WWHD_ADDR_cXyz_Zero 0x101FFBA8
/* wwhd_src/include/d/actor/d_a_itembase.h */
#define WWHD_ADDR_dItem_data_item_resource 0x101E4674
#define WWHD_STRIDE_dItem_data_item_resource 0x24
#define WWHD_DATA_dItem_data_item_resource(index) ((u32)&WWHD_GAME_DATA(WWHD_ADDR_dItem_data_item_resource, u8) + (u32)(index) * WWHD_STRIDE_dItem_data_item_resource)
/* wwhd_src/include/d/actor/d_a_itembase.h */
#define WWHD_ADDR_dItem_data_field_item_res 0x101E6A74
#define WWHD_STRIDE_dItem_data_field_item_res 0x1C
#define WWHD_DATA_dItem_data_field_item_res(index) ((u32)&WWHD_GAME_DATA(WWHD_ADDR_dItem_data_field_item_res, u8) + (u32)(index) * WWHD_STRIDE_dItem_data_field_item_res)
/* wwhd_src/include/d/actor/d_a_itembase.h */
#define WWHD_ADDR_dItem_data_item_info 0x101E8674
#define WWHD_STRIDE_dItem_data_item_info 4
#define WWHD_DATA_dItem_data_item_info(index) ((u32)&WWHD_GAME_DATA(WWHD_ADDR_dItem_data_item_info, u8) + (u32)(index) * WWHD_STRIDE_dItem_data_item_info)
