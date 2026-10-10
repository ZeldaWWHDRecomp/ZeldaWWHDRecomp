/* Generated from public ZeldaWWHDDecomp/wwhd 47e1dbc3886cfd8233859dffd73efc41a04a9130.
 * Source: wwhd_src/d/actor/d_a_dr.cpp; CC0-1.0 (public-wwhd-LICENSE).
 * Partial views: named scalar and curated aggregate fields; unknown fields remain bytes.
 * Source offset qualifications still apply; see the public source. */
#pragma once
#include "../wwhd_guest.h"
#include "vectors.h"
#ifndef WWHD_SDK_ASSERT
#ifdef __cplusplus
#define WWHD_SDK_ASSERT(x, message) static_assert(x, message)
#else
#define WWHD_SDK_ASSERT(x, message) _Static_assert(x, message)
#endif
#endif
WWHD_SDK_ASSERT(sizeof(void*) == 4, "SDK layouts require a 32-bit guest target");

#include "ptmf.h"
typedef union dr_class {
    u8 bytes[0x3E8];
    struct __attribute__((packed)) { u8 _pad_mpMorf[0x3D0]; u32 mpMorf; };
    struct __attribute__((packed)) { u8 _pad_mMode[0x3D4]; u8 mMode; };
    struct __attribute__((packed)) { u8 _pad_mCurrBckIdx[0x3DC]; s32 mCurrBckIdx; };
} dr_class;
WWHD_SDK_ASSERT(sizeof(dr_class) == 0x3E8, "dr_class size");
WWHD_SDK_ASSERT(__builtin_offsetof(dr_class, mpMorf) == 0x3D0, "dr_class.mpMorf");
WWHD_SDK_ASSERT(__builtin_offsetof(dr_class, mMode) == 0x3D4, "dr_class.mMode");
WWHD_SDK_ASSERT(__builtin_offsetof(dr_class, mCurrBckIdx) == 0x3DC, "dr_class.mCurrBckIdx");
