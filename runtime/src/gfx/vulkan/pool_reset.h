// Submission pool resets off the render thread (WWHD_VK_ASYNC_POOL_RESET).
// Decomp build, fast mode only (cmake WWHD_NATIVE_DRAW); nothing here depends on the decomp, so the hooks can later
// be taken into every build.
//
// A submission slot's command pool and descriptor pool are reset when the slot is retired (backend.cpp
// cleanup_submission), on the render thread, just before the slot is recorded again; on the stock Adreno driver
// vkResetCommandPool alone costs about 1 ms of the render thread per frame.
//   bit 1: after the render thread submits a slot, a reset thread waits for its fence and resets both pools (flags 0,
//          as before); cleanup_submission then waits for that slot's reset (normally long done) and checks its results.
//          Same pools, same flags, after the same fence, before the same next recording. The record worker's
//          submissions keep the inline reset.
//   bit 2: the slots' command pools are created with VK_COMMAND_POOL_CREATE_TRANSIENT_BIT instead of
//          RESET_COMMAND_BUFFER_BIT (no command buffer is ever reset on its own: a pool reset precedes every
//          recording); not with the record worker.
// WWHD_VK_ASYNC_POOL_RESET=0|1|2|3; unset: 1 (the reset thread). 0: every reset inline on the render thread, as in
// every other build. Counter line "[vulkan pool reset]" (report).
#pragma once
#ifdef WWHD_NATIVE_DRAW
#include "loader.h"

namespace gfxvk::pool_reset {
int mode();
// the flags a slot's command pool is created with (DEFAULT_FLAGS when bit 2 is off)
VkCommandPoolCreateFlags pool_flags(VkCommandPoolCreateFlags defaultFlags);
// render thread, right after it submitted SLOT with FENCE (bit 1: the reset thread resets the pools once it signals)
void submitted(const void* slot, VkDevice device, VkFence fence, VkCommandPool commands, VkDescriptorPool descriptors);
// render thread, the slot retired (its fence signalled): its pools reset, by the reset thread (waited for) or here
void reset(const void* slot, VkDevice device, VkCommandPool commands, VkDescriptorPool descriptors);
// the counter line, every stats period of FRAMES frames
void report(double frames);
}  // namespace gfxvk::pool_reset
#endif
