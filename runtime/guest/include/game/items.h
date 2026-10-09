/* Generated from public ZeldaWWHDDecomp/wwhd 47e1dbc3886cfd8233859dffd73efc41a04a9130.
 * Source: wwhd_src/include/d/actor/d_a_itembase.h; CC0-1.0 (public-wwhd-LICENSE).
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

typedef union daItemBase_c {
    u8 bytes[0x750];
    struct __attribute__((packed)) { u8 _pad_mpModel[0x3B4]; u32 mpModel; };
    struct __attribute__((packed)) { u8 _pad_mpModelArrow[0x3B8]; u32 mpModelArrow[2]; };
    struct __attribute__((packed)) { u8 _pad_mpBtkAnm1[0x3C0]; u32 mpBtkAnm1; };
    struct __attribute__((packed)) { u8 _pad_mpBtkAnm2[0x3C4]; u32 mpBtkAnm2; };
    struct __attribute__((packed)) { u8 _pad_mpBrkAnm1[0x3C8]; u32 mpBrkAnm1; };
    struct __attribute__((packed)) { u8 _pad_mpBrkAnm2[0x3CC]; u32 mpBrkAnm2; };
    struct __attribute__((packed)) { u8 _pad_mpBckAnm[0x3D0]; u32 mpBckAnm; };
    struct __attribute__((packed)) { u8 _pad_mItemBitNo[0x744]; s32 mItemBitNo; };
    struct __attribute__((packed)) { u8 _pad_m_timer[0x748]; s32 m_timer; };
    struct __attribute__((packed)) { u8 _pad_m_get_timer[0x74C]; s16 m_get_timer; };
    struct __attribute__((packed)) { u8 _pad_m_itemNo[0x74E]; u8 m_itemNo; };
    struct __attribute__((packed)) { u8 _pad_mDrawFlags[0x74F]; u8 mDrawFlags; };
} daItemBase_c;
WWHD_SDK_ASSERT(sizeof(daItemBase_c) == 0x750, "daItemBase_c size");
WWHD_SDK_ASSERT(__builtin_offsetof(daItemBase_c, mpModel) == 0x3B4, "daItemBase_c.mpModel");
WWHD_SDK_ASSERT(__builtin_offsetof(daItemBase_c, mpModelArrow) == 0x3B8, "daItemBase_c.mpModelArrow");
WWHD_SDK_ASSERT(__builtin_offsetof(daItemBase_c, mpBtkAnm1) == 0x3C0, "daItemBase_c.mpBtkAnm1");
WWHD_SDK_ASSERT(__builtin_offsetof(daItemBase_c, mpBtkAnm2) == 0x3C4, "daItemBase_c.mpBtkAnm2");
WWHD_SDK_ASSERT(__builtin_offsetof(daItemBase_c, mpBrkAnm1) == 0x3C8, "daItemBase_c.mpBrkAnm1");
WWHD_SDK_ASSERT(__builtin_offsetof(daItemBase_c, mpBrkAnm2) == 0x3CC, "daItemBase_c.mpBrkAnm2");
WWHD_SDK_ASSERT(__builtin_offsetof(daItemBase_c, mpBckAnm) == 0x3D0, "daItemBase_c.mpBckAnm");
WWHD_SDK_ASSERT(__builtin_offsetof(daItemBase_c, mItemBitNo) == 0x744, "daItemBase_c.mItemBitNo");
WWHD_SDK_ASSERT(__builtin_offsetof(daItemBase_c, m_timer) == 0x748, "daItemBase_c.m_timer");
WWHD_SDK_ASSERT(__builtin_offsetof(daItemBase_c, m_get_timer) == 0x74C, "daItemBase_c.m_get_timer");
WWHD_SDK_ASSERT(__builtin_offsetof(daItemBase_c, m_itemNo) == 0x74E, "daItemBase_c.m_itemNo");
WWHD_SDK_ASSERT(__builtin_offsetof(daItemBase_c, mDrawFlags) == 0x74F, "daItemBase_c.mDrawFlags");
