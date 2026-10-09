/* Public HD object facts at 47e1dbc3886cfd8233859dffd73efc41a04a9130; CC0-1.0.
 * Offsets do not assert a complete model, rope, or controller layout.
 * Source qualifications ([v]/[g]) continue to apply. */
#pragma once
#include "../wwhd_guest.h"

/* wwhd_src/f_op/f_op_actor.cpp */
#define WWHD_OFFSET_actor_profile_methods 0x24
/* wwhd_src/d/d_save_local.h */
#define WWHD_OFFSET_save_inventory 0x5C
/* wwhd_src/d/d_save_local.h */
#define WWHD_COUNT_save_inventory 0x15
/* wwhd_src/d/actor/d_a_player_main_04.cpp */
#define WWHD_OFFSET_Link_equipped_item_model 0x4440
/* wwhd_src/d/actor/d_a_npc_aj1.cpp */
#define WWHD_OFFSET_actor_attention_flags 0x39C
/* wwhd_src/m_Do/m_Do_ext_line.cpp */
#define WWHD_ALLOC_SIZE_line_mat0 0x148
/* wwhd_src/m_Do/m_Do_ext_line.cpp */
#define WWHD_OFFSET_line_mat0_points_table 0x144
/* wwhd_src/include/d/actor/d_a_pz.h */
#define WWHD_OFFSET_J3DModel_mpMtxBlock 0x2C
/* wwhd_src/include/d/actor/d_a_pz.h */
#define WWHD_OFFSET_J3DMtxBlock_mpMtx 0x10
/* wwhd_src/include/m_Do/m_Do_ext.h */
#define WWHD_OFFSET_J3DModel_base_matrix 0xC8
/* wwhd_src/include/bindings.h */
#define WWHD_OFFSET_J3DModel_base_scale 0xBC
/* wwhd_src/include/d/actor/d_a_ship.h */
#define WWHD_OFFSET_line_material_vtable 0x130
/* wwhd_src/include/d/actor/d_a_ship.h */
#define WWHD_OFFSET_line_material_id_vslot 0x14
/* wwhd_src/include/d/actor/d_a_ship.h */
#define WWHD_PLAY_LINE_PACKETS_OFFSET 0x5FB4
/* wwhd_src/include/d/actor/d_a_ship.h */
#define WWHD_STRIDE_line_packet 0x9C
/* wwhd_src/include/d/actor/d_a_npc_md.h */
#define WWHD_ADDR_Medli_flying 0x101D5F3E
/* wwhd_src/include/d/actor/d_a_npc_md.h */
#define WWHD_ADDR_Medli_player_room 0x101D5F41
/* wwhd_src/d/actor/d_a_npc_md_exec.cpp */
#define WWHD_ADDR_Medli_mirror 0x101D5F3F
/* wwhd_src/d/actor/d_a_npc_md_heap.cpp */
#define WWHD_ADDR_Medli_sea_talk 0x101D5F40
/* wwhd_src/d/actor/d_a_npc_md_exec.cpp */
#define WWHD_ADDR_Medli_instance_pointer 0x101CEF74
/* wwhd_src/include/d/actor/d_a_pt.h */
#define WWHD_ADDR_room_control_stay_no 0x1047E6C8
/* wwhd_src/d/actor/d_a_player_main_04.cpp */
#define WWHD_ADDR_Link_debug_position 0x1046CD48
/* wwhd_src/d/actor/d_a_player_main_04.cpp */
#define WWHD_ADDR_Link_debug_shape_angle 0x1046CD10
/* wwhd_src/d/actor/d_a_player_main_04.cpp */
#define WWHD_ADDR_Link_debug_current_angle 0x1046CD08
/* wwhd_src/include/d/actor/d_a_himo2.h */
#define WWHD_ADDR_Himo2_SafeString_vtable 0x10010DEC
/* wwhd_src/d/actor/d_a_player_main_07.cpp */
#define WWHD_PLAY_PLAYER_STATUS1_OFFSET 0x5CDC
/* wwhd_src/d/actor/d_a_player_main_07.cpp */
#define WWHD_PLAY_METRONOME_OFFSET 0x5BD1
/* wwhd_src/include/d/actor/d_a_npc_mn.h */
#define WWHD_PLAY_EVENT_FLAGS_OFFSET 0x52B8
/* wwhd_src/include/d/d_com_inf_game.h */
#define WWHD_PLAY_BG_COLLISION_OFFSET 0x12A0
/* wwhd_src/include/d/d_com_inf_game.h */
#define WWHD_PLAY_PLAYER_STATUS0_OFFSET 0x5CD8
/* wwhd_src/include/d/actor/d_a_player.h; declared fields in public order, size 0xC. */
typedef struct { u32 mVtable, mpEmitter, mpMatrix; } daPy_mtxFollowEcallBack_c;
/* wwhd_src/include/d/d_com_inf_game.h; GHS places the vtable after the string pointer. */
typedef struct { u32 mStringTop, __vtbl; } wwhd_safe_string;
/* wwhd_src/SSystem/SComponent/c_API_controller.cpp */
#define WWHD_ADDR_pad_pointer 0x101F5088
#define WWHD_OFFSET_pad_buttons_124 0x124
#define WWHD_OFFSET_pad_axis_130 0x130
#define WWHD_OFFSET_pad_axis_134 0x134
#define WWHD_OFFSET_pad_axis_138 0x138
#define WWHD_OFFSET_pad_axis_13C 0x13C
/* wwhd_src/include/d/d_bg_s.h; [v kamome] storage only. */
typedef struct { u8 bytes[0x6C]; } wwhd_line_check_storage;
#define WWHD_OFFSET_line_check_pass_flags 0x5C
#define WWHD_OFFSET_line_check_group 0x68
#define WWHD_OFFSET_line_check_pass_pointer0 0x0
#define WWHD_OFFSET_line_check_pass_pointer1 0x4
#define WWHD_OFFSET_line_check_vtable_10 0x10
#define WWHD_OFFSET_line_check_vtable_20 0x20
#define WWHD_OFFSET_line_check_vtable_58 0x58
#define WWHD_OFFSET_line_check_vtable_64 0x64
/* wwhd_src/d/actor/d_a_obj_hole.cpp */
#define WWHD_ADDR_ObjHole_line_check_vtable_10 0x1002A8C8
#define WWHD_ADDR_ObjHole_line_check_vtable_20 0x1002A8D8
#define WWHD_ADDR_ObjHole_line_check_vtable_58 0x1002A8F8
#define WWHD_ADDR_ObjHole_line_check_vtable_64 0x1002A8E8
