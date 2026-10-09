/* Generated from public ZeldaWWHDDecomp/wwhd 47e1dbc3886cfd8233859dffd73efc41a04a9130.
 * Source: wwhd_src/include/f_op/f_op_actor.h; CC0-1.0 (public-wwhd-LICENSE).
 * Partial views: named scalar fields only; unknown fields remain bytes.
 * Source offset qualifications still apply; see the public source. */
#pragma once
#include "../wwhd_guest.h"
#ifndef WWHD_SDK_ASSERT
#ifdef __cplusplus
#define WWHD_SDK_ASSERT(x, message) static_assert(x, message)
#else
#define WWHD_SDK_ASSERT(x, message) _Static_assert(x, message)
#endif
#endif
WWHD_SDK_ASSERT(sizeof(void*) == 4, "SDK layouts require a 32-bit guest target");

typedef union actor_place {
    u8 bytes[0x14];
    struct __attribute__((packed)) { u8 _pad_roomNo[0x12]; s8 roomNo; };
    struct __attribute__((packed)) { u8 _pad_field_0x13[0x13]; u8 field_0x13; };
} actor_place;
WWHD_SDK_ASSERT(sizeof(actor_place) == 0x14, "actor_place size");
WWHD_SDK_ASSERT(__builtin_offsetof(actor_place, roomNo) == 0x12, "actor_place.roomNo");
WWHD_SDK_ASSERT(__builtin_offsetof(actor_place, field_0x13) == 0x13, "actor_place.field_0x13");

typedef union dKy_tevstr_c {
    u8 bytes[0x1C8];
    struct __attribute__((packed)) { u8 _00[0xB9]; };
    struct __attribute__((packed)) { u8 _pad_mRoomNo[0xB9]; s8 mRoomNo; };
    struct __attribute__((packed)) { u8 _pad__BA[0xBA]; u8 _BA[0x1C8 - 0xBA]; };
} dKy_tevstr_c;
WWHD_SDK_ASSERT(sizeof(dKy_tevstr_c) == 0x1C8, "dKy_tevstr_c size");
WWHD_SDK_ASSERT(__builtin_offsetof(dKy_tevstr_c, _00) == 0x0, "dKy_tevstr_c._00");
WWHD_SDK_ASSERT(__builtin_offsetof(dKy_tevstr_c, mRoomNo) == 0xB9, "dKy_tevstr_c.mRoomNo");
WWHD_SDK_ASSERT(__builtin_offsetof(dKy_tevstr_c, _BA) == 0xBA, "dKy_tevstr_c._BA");

typedef union fopAc_ac_c {
    u8 bytes[0x3AC];
    struct __attribute__((packed)) { u8 _000[0xB0]; };
    struct __attribute__((packed)) { u8 _pad_mParameters[0xB0]; u32 mParameters; };
    struct __attribute__((packed)) { u8 _pad___vtbl[0xB4]; u32 __vtbl; };
    struct __attribute__((packed)) { u8 _pad__0B8[0xB8]; u8 _0B8[0xF4 - 0xB8]; };
    struct __attribute__((packed)) { u8 _pad_heap[0xF4]; u32 heap; };
    struct __attribute__((packed)) { u8 _pad__0F8[0xF8]; u8 _0F8[0x110 - 0xF8]; };
    struct __attribute__((packed)) { u8 _pad_setID[0x2D8]; u16 setID; };
    struct __attribute__((packed)) { u8 _pad_group[0x2DA]; u8 group; };
    struct __attribute__((packed)) { u8 _pad_cullType[0x2DB]; u8 cullType; };
    struct __attribute__((packed)) { u8 _pad_demoActorID[0x2DC]; u8 demoActorID; };
    struct __attribute__((packed)) { u8 _pad_argument[0x2DD]; s8 argument; };
    struct __attribute__((packed)) { u8 _pad_gbaName[0x2DE]; u8 gbaName; };
    struct __attribute__((packed)) { u8 _pad__2DF[0x2DF]; u8 _2DF; };
    struct __attribute__((packed)) { u8 _pad_actor_status[0x2E0]; u32 actor_status; };
    struct __attribute__((packed)) { u8 _pad_actor_condition[0x2E4]; u32 actor_condition; };
    struct __attribute__((packed)) { u8 _pad_parentActorID[0x2E8]; u32 parentActorID; };
    struct __attribute__((packed)) { u8 _pad__32E[0x32E]; u8 _32E[2]; };
    struct __attribute__((packed)) { u8 _pad_cullMtx[0x348]; u32 cullMtx; };
    struct __attribute__((packed)) { u8 _pad_cull[0x34C]; u8 cull[0x18]; };
    struct __attribute__((packed)) { u8 _pad_cullSizeFar[0x364]; f32 cullSizeFar; };
    struct __attribute__((packed)) { u8 _pad_model[0x368]; u32 model; };
    struct __attribute__((packed)) { u8 _pad_jntHit[0x36C]; u32 jntHit; };
    struct __attribute__((packed)) { u8 _pad_speedF[0x370]; f32 speedF; };
    struct __attribute__((packed)) { u8 _pad_gravity[0x374]; f32 gravity; };
    struct __attribute__((packed)) { u8 _pad_maxFallSpeed[0x378]; f32 maxFallSpeed; };
    struct __attribute__((packed)) { u8 _pad_attention_info[0x388]; u8 attention_info[0x18]; };
    struct __attribute__((packed)) { u8 _pad_max_health[0x3A0]; s8 max_health; };
    struct __attribute__((packed)) { u8 _pad_health[0x3A1]; s8 health; };
    struct __attribute__((packed)) { u8 _pad__3A2[0x3A2]; u8 _3A2[2]; };
    struct __attribute__((packed)) { u8 _pad_itemTableIdx[0x3A4]; s32 itemTableIdx; };
    struct __attribute__((packed)) { u8 _pad_stealItemBitNo[0x3A8]; u8 stealItemBitNo; };
    struct __attribute__((packed)) { u8 _pad_stealItemLeft[0x3A9]; s8 stealItemLeft; };
    struct __attribute__((packed)) { u8 _pad__3AA[0x3AA]; u8 _3AA[2]; };
} fopAc_ac_c;
WWHD_SDK_ASSERT(sizeof(fopAc_ac_c) == 0x3AC, "fopAc_ac_c size");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, _000) == 0x0, "fopAc_ac_c._000");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, mParameters) == 0xB0, "fopAc_ac_c.mParameters");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, __vtbl) == 0xB4, "fopAc_ac_c.__vtbl");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, _0B8) == 0xB8, "fopAc_ac_c._0B8");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, heap) == 0xF4, "fopAc_ac_c.heap");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, _0F8) == 0xF8, "fopAc_ac_c._0F8");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, setID) == 0x2D8, "fopAc_ac_c.setID");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, group) == 0x2DA, "fopAc_ac_c.group");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, cullType) == 0x2DB, "fopAc_ac_c.cullType");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, demoActorID) == 0x2DC, "fopAc_ac_c.demoActorID");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, argument) == 0x2DD, "fopAc_ac_c.argument");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, gbaName) == 0x2DE, "fopAc_ac_c.gbaName");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, _2DF) == 0x2DF, "fopAc_ac_c._2DF");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, actor_status) == 0x2E0, "fopAc_ac_c.actor_status");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, actor_condition) == 0x2E4, "fopAc_ac_c.actor_condition");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, parentActorID) == 0x2E8, "fopAc_ac_c.parentActorID");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, _32E) == 0x32E, "fopAc_ac_c._32E");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, cullMtx) == 0x348, "fopAc_ac_c.cullMtx");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, cull) == 0x34C, "fopAc_ac_c.cull");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, cullSizeFar) == 0x364, "fopAc_ac_c.cullSizeFar");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, model) == 0x368, "fopAc_ac_c.model");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, jntHit) == 0x36C, "fopAc_ac_c.jntHit");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, speedF) == 0x370, "fopAc_ac_c.speedF");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, gravity) == 0x374, "fopAc_ac_c.gravity");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, maxFallSpeed) == 0x378, "fopAc_ac_c.maxFallSpeed");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, attention_info) == 0x388, "fopAc_ac_c.attention_info");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, max_health) == 0x3A0, "fopAc_ac_c.max_health");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, health) == 0x3A1, "fopAc_ac_c.health");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, _3A2) == 0x3A2, "fopAc_ac_c._3A2");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, itemTableIdx) == 0x3A4, "fopAc_ac_c.itemTableIdx");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, stealItemBitNo) == 0x3A8, "fopAc_ac_c.stealItemBitNo");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, stealItemLeft) == 0x3A9, "fopAc_ac_c.stealItemLeft");
WWHD_SDK_ASSERT(__builtin_offsetof(fopAc_ac_c, _3AA) == 0x3AA, "fopAc_ac_c._3AA");
