/* SPDX-License-Identifier: MPL-2.0 */
/* Checked access to the game's persistent event bits/registers. No story events are dispatched.
 * Layout: generated save.h (save prefix and event offset) and pinned public d_save_3.cpp (native
 * event methods), CC0-1.0.
 * This handwritten adapter is separate from the generated save.h and bindings.h. */
#pragma once
#include "save.h"
#include "bindings.h"

#define WWHD_SAVE_FLAGS_INVALID (-1)
#define WWHD_SAVE_FLAGS_NOT_READY (-2)

/* Call on the game thread, after save allocation (for example from a new-file initialization
 * hook or during play). A non-null allocation does not mean that a player's file is loaded.
 * Writes persist when the game next saves; callers are responsible for story order and backups.
 * A selector's high byte is the event-byte index, its low byte the mask. Register values use the
 * mask's original bit positions: selector 0x12F0 reads/writes 0x00, 0x10, ... 0xF0, not 0..15. */
static inline u32 wwhd_save_flags_detail_event_object(void) {
    const u32 base = WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer, u32);
    const u32 offset = WWHD_OFFSET_save_info + WWHD_OFFSET_save_event;
    if (base < 0x10000 || (base & 3) || base > 0xFFFFFFFFu - offset - 0xFF) return 0;
    return base + offset;
}

static inline s32 wwhd_save_flags_detail_selector_valid(u32 selector) {
    return selector <= 0xFFFF && (selector & 0xFF) != 0;
}

/* Masked byte (0..255), or a negative status. */
static inline s32 wwhd_save_event_read(u32 selector) {
    if (!wwhd_save_flags_detail_selector_valid(selector)) return WWHD_SAVE_FLAGS_INVALID;
    const u32 event = wwhd_save_flags_detail_event_object();
    if (!event) return WWHD_SAVE_FLAGS_NOT_READY;
    return wwhd_dSv_event_c_getEventReg(event, selector);
}

/* Zero on success, or a negative status. Reject values outside the mask: the native setter
 * ORs its value without masking it, so passing such a value would change adjacent flags. */
static inline s32 wwhd_save_event_write(u32 selector, u32 bits) {
    if (!wwhd_save_flags_detail_selector_valid(selector) || (bits & ~(selector & 0xFF)))
        return WWHD_SAVE_FLAGS_INVALID;
    const u32 event = wwhd_save_flags_detail_event_object();
    if (!event) return WWHD_SAVE_FLAGS_NOT_READY;
    wwhd_dSv_event_c_setEventReg(event, selector, bits);
    return 0;
}

/* A bit selector must contain exactly one mask bit. Test returns 0/1 or a negative status;
 * set returns zero on success. enabled == 0 clears the bit; other values set it. */
static inline s32 wwhd_save_event_test(u32 flag) {
    const u32 mask = flag & 0xFF;
    if (!wwhd_save_flags_detail_selector_valid(flag) || (mask & (mask - 1))) return WWHD_SAVE_FLAGS_INVALID;
    const u32 event = wwhd_save_flags_detail_event_object();
    if (!event) return WWHD_SAVE_FLAGS_NOT_READY;
    return wwhd_dSv_event_c_isEventBit(event, flag) != 0;
}

static inline s32 wwhd_save_event_set(u32 flag, s32 enabled) {
    const u32 mask = flag & 0xFF;
    if (!wwhd_save_flags_detail_selector_valid(flag) || (mask & (mask - 1))) return WWHD_SAVE_FLAGS_INVALID;
    const u32 event = wwhd_save_flags_detail_event_object();
    if (!event) return WWHD_SAVE_FLAGS_NOT_READY;
    if (enabled) wwhd_dSv_event_c_onEventBit(event, flag);
    else wwhd_dSv_event_c_offEventBit(event, flag);
    return 0;
}
