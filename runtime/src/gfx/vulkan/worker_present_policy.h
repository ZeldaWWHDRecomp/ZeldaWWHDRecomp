#pragma once
#include <vulkan/vulkan.h>
namespace gfxvk::worker_present_policy {
enum class Mode { ConservativeProducer, NativeWorker };
// Explicit, deliberately conservative allowlist. New/unknown IDs remain producer.
inline bool known_native(VkDriverId id) {
  switch (id) {
  case VK_DRIVER_ID_AMD_PROPRIETARY:
  case VK_DRIVER_ID_AMD_OPEN_SOURCE:
  case VK_DRIVER_ID_MESA_RADV:
  case VK_DRIVER_ID_NVIDIA_PROPRIETARY:
  case VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS:
  case VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA:
  case VK_DRIVER_ID_IMAGINATION_PROPRIETARY:
  case VK_DRIVER_ID_QUALCOMM_PROPRIETARY:
  case VK_DRIVER_ID_ARM_PROPRIETARY:
  case VK_DRIVER_ID_GOOGLE_SWIFTSHADER:
  case VK_DRIVER_ID_BROADCOM_PROPRIETARY:
  case VK_DRIVER_ID_MESA_LLVMPIPE:
  case VK_DRIVER_ID_COREAVI_PROPRIETARY:
  case VK_DRIVER_ID_VERISILICON_PROPRIETARY:
  case VK_DRIVER_ID_MESA_TURNIP:
  case VK_DRIVER_ID_MESA_V3DV:
  case VK_DRIVER_ID_MESA_PANVK:
  case VK_DRIVER_ID_SAMSUNG_PROPRIETARY:
  case VK_DRIVER_ID_MESA_NVK:
  case VK_DRIVER_ID_IMAGINATION_OPEN_SOURCE_MESA:
  case static_cast<VkDriverId>(26):  // VK_DRIVER_ID_MESA_HONEYKRISP; older Vulkan headers (Linux CI) lack the name
    return true;
  default: return false;
  }
}
template<class Query>
Mode resolve(bool requested, uint32_t actualApi, bool driverPropertiesExtension,
             bool properties2Available, bool appleHost, Query query) {
  if (!requested || !properties2Available ||
      (actualApi < VK_API_VERSION_1_2 && !driverPropertiesExtension))
    return Mode::ConservativeProducer;
  const auto id = query();
  // All Apple hosts remain conservative: future Metal translators must not
  // accidentally opt in through a known vendor ID. Honeykrisp is allowed on
  // non-Apple hosts; MoltenVK/KosmicKrisp never enter this native allowlist.
  return !appleHost && known_native(id) ? Mode::NativeWorker : Mode::ConservativeProducer;
}
}
