/* Generated from public ZeldaWWHDDecomp/wwhd 47e1dbc3886cfd8233859dffd73efc41a04a9130.
 * Source: wwhd_src/include/d/actor/d_a_npc_md.h; CC0-1.0 (public-wwhd-LICENSE).
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
typedef union daNpc_Md_c {
    u8 bytes[0x43DC];
    struct __attribute__((packed)) { u8 _pad_mpMorf[0x618]; u32 mpMorf; };
} daNpc_Md_c;
WWHD_SDK_ASSERT(sizeof(daNpc_Md_c) == 0x43DC, "daNpc_Md_c size");
WWHD_SDK_ASSERT(__builtin_offsetof(daNpc_Md_c, mpMorf) == 0x618, "daNpc_Md_c.mpMorf");
