/* Generated from public ZeldaWWHDDecomp/wwhd 47e1dbc3886cfd8233859dffd73efc41a04a9130.
 * Source: wwhd_src/include/d/actor/d_a_tag_msg.h; CC0-1.0 (public-wwhd-LICENSE).
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

typedef union daTag_Msg_c {
    u8 bytes[0x3B0];
    struct __attribute__((packed)) { u8 _pad_mAction[0x3AC]; u8 mAction; };
} daTag_Msg_c;
WWHD_SDK_ASSERT(sizeof(daTag_Msg_c) == 0x3B0, "daTag_Msg_c size");
WWHD_SDK_ASSERT(__builtin_offsetof(daTag_Msg_c, mAction) == 0x3AC, "daTag_Msg_c.mAction");
