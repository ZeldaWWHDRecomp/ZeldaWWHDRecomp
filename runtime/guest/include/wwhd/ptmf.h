/* Generated from public ZeldaWWHDDecomp/wwhd 47e1dbc3886cfd8233859dffd73efc41a04a9130.
 * Source: wwhd_src/include/d/actor/d_a_npc_ba1.h; CC0-1.0 (public-wwhd-LICENSE).
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

typedef union ProcFunc_l {
    u8 bytes[8];
    struct __attribute__((packed)) { s16 d; };
    struct __attribute__((packed)) { u8 _pad_i[0x2]; s16 i; };
    struct __attribute__((packed)) { u8 _pad_f[0x4]; u32 f; };
} ProcFunc_l;
WWHD_SDK_ASSERT(sizeof(ProcFunc_l) == 8, "ProcFunc_l size");
WWHD_SDK_ASSERT(__builtin_offsetof(ProcFunc_l, d) == 0x0, "ProcFunc_l.d");
WWHD_SDK_ASSERT(__builtin_offsetof(ProcFunc_l, i) == 0x2, "ProcFunc_l.i");
WWHD_SDK_ASSERT(__builtin_offsetof(ProcFunc_l, f) == 0x4, "ProcFunc_l.f");
