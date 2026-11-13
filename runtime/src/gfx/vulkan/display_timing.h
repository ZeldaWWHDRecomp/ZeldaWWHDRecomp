// VK_GOOGLE_display_timing for the TV swapchain (Android; decomp build, fast mode: cmake WWHD_NATIVE_DRAW only):
// - observation for the frame breakdown (frame_breakdown.h: fb_display rows) and the counter line
//   "[vulkan display timing]" (WWHD_VK_STATS): on-screen intervals in refreshes;
// - WWHD_VK_DISPLAY_PACING=bits (off by default): 1 each TV present asks for an on-screen time on the refresh grid,
//   one frame interval after the previous frame's (display_pacing_policy.h); with 1: 2 one more swapchain image,
//   4 adaptive latency target (half a refresh lower after 120 frames without a missed slot, half higher on a miss;
//   without 4 only higher). WWHD_VK_DISPLAY_PACING_Q=0..1: the quantile of the unpaced latency it starts from
//   (default 0.5).
// Without the extension (or off) nothing changes: the present chain is left as it is.
#pragma once
#ifdef WWHD_NATIVE_DRAW
#include "loader.h"
#include <cstdint>

namespace gfxvk::display_timing {
bool pacing();   // WWHD_VK_DISPLAY_PACING=1 (Android)
bool wanted();   // pacing or the frame breakdown (Android): ask for the extension
void device_created(VkDevice device, bool extensionEnabled);
// the even-pacing frame interval of the frame being swapped (backend.cpp set_present_interval; 0: none)
void set_interval(uint32_t ns);
// the TV swapchain's image count (WWHD_VK_DISPLAY_PACING bit 2: one more, so a frame waiting in the compositor for
// its slot does not take the image the next frame needs)
uint32_t image_count(uint32_t count, uint32_t maxCount);
// the TV present's pNext with a present-time entry (STORAGE lives until vkQueuePresentKHR returns)
struct PresentTimes { VkPresentTimesInfoGOOGLE info{VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE}; VkPresentTimeGOOGLE time{}; };
const void* chain_present(const void* pNext, PresentTimes& storage, uint64_t frame);
// render thread, once a frame: the finished presentations of the TV swapchain
void poll(VkDevice device, VkSwapchainKHR swapchain);
void report(double frames);
}  // namespace gfxvk::display_timing
#endif
