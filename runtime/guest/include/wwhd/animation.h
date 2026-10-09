/* Generated from public ZeldaWWHDDecomp/wwhd 47e1dbc3886cfd8233859dffd73efc41a04a9130.
 * Source: wwhd_src/include/m_Do/m_Do_ext.h; CC0-1.0 (public-wwhd-LICENSE).
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
typedef union J3DFrameCtrl {
    u8 bytes[0x10];
    struct __attribute__((packed)) { f32 mRate; };
    struct __attribute__((packed)) { u8 _pad_mFrame[0x4]; f32 mFrame; };
    struct __attribute__((packed)) { u8 _pad_mStart[0x8]; s16 mStart; };
    struct __attribute__((packed)) { u8 _pad_mEnd[0xA]; s16 mEnd; };
    struct __attribute__((packed)) { u8 _pad_mLoop[0xC]; s16 mLoop; };
    struct __attribute__((packed)) { u8 _pad_mAttribute[0xE]; u8 mAttribute; };
    struct __attribute__((packed)) { u8 _pad_mState[0xF]; u8 mState; };
} J3DFrameCtrl;
WWHD_SDK_ASSERT(sizeof(J3DFrameCtrl) == 0x10, "J3DFrameCtrl size");
WWHD_SDK_ASSERT(__builtin_offsetof(J3DFrameCtrl, mRate) == 0x0, "J3DFrameCtrl.mRate");
WWHD_SDK_ASSERT(__builtin_offsetof(J3DFrameCtrl, mFrame) == 0x4, "J3DFrameCtrl.mFrame");
WWHD_SDK_ASSERT(__builtin_offsetof(J3DFrameCtrl, mStart) == 0x8, "J3DFrameCtrl.mStart");
WWHD_SDK_ASSERT(__builtin_offsetof(J3DFrameCtrl, mEnd) == 0xA, "J3DFrameCtrl.mEnd");
WWHD_SDK_ASSERT(__builtin_offsetof(J3DFrameCtrl, mLoop) == 0xC, "J3DFrameCtrl.mLoop");
WWHD_SDK_ASSERT(__builtin_offsetof(J3DFrameCtrl, mAttribute) == 0xE, "J3DFrameCtrl.mAttribute");
WWHD_SDK_ASSERT(__builtin_offsetof(J3DFrameCtrl, mState) == 0xF, "J3DFrameCtrl.mState");

typedef union mDoExt_McaMorf {
    u8 bytes[0xC8];
    struct __attribute__((packed)) { u8 _pad_mpModel[0x90]; u32 mpModel; };
    struct __attribute__((packed)) { u8 _pad_mFrameCtrl[0x98]; J3DFrameCtrl mFrameCtrl; };
} mDoExt_McaMorf;
WWHD_SDK_ASSERT(sizeof(mDoExt_McaMorf) == 0xC8, "mDoExt_McaMorf size");
WWHD_SDK_ASSERT(__builtin_offsetof(mDoExt_McaMorf, mpModel) == 0x90, "mDoExt_McaMorf.mpModel");
WWHD_SDK_ASSERT(__builtin_offsetof(mDoExt_McaMorf, mFrameCtrl) == 0x98, "mDoExt_McaMorf.mFrameCtrl");
