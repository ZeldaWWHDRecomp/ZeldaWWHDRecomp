#pragma once
#include <vulkan/vulkan.h>
#include <cstring>

namespace gfxvk {
// Qualcomm's proprietary version uses Vulkan's major/minor/patch packing;
// its driver release (e.g. 502 on Snapdragon 855/865/870 stock firmware) is
// the minor field: (driverVersion >> 12) & 0x3FF.
inline uint32_t qualcomm_driver_release(uint32_t version) { return (version >> 12) & 0x3FF; }
// Releases below v700 can hang the GPU when pipelines are created on several
// threads at once. Mesa Turnip and other drivers are excluded.
// WWHD_SERIAL_PIPELINES=0/1 forces it off/on; other values keep the rule.
inline bool serial_pipelines_wanted(VkDriverId driver, uint32_t version, const char* overrideValue) {
  if (overrideValue && !std::strcmp(overrideValue, "0")) return false;
  if (overrideValue && !std::strcmp(overrideValue, "1")) return true;
  return driver == VK_DRIVER_ID_QUALCOMM_PROPRIETARY && qualcomm_driver_release(version) < 700;
}
} // namespace gfxvk
