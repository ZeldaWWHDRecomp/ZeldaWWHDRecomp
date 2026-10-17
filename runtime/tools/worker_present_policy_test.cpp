// Driver policy only; real native presentation requires device validation.
// Driver ids 26 (Honeykrisp), 27 (Vulkan SC emulation) and 28 (Kosmickrisp) by value: older Vulkan headers lack the names.
#include "gfx/vulkan/worker_present_policy.h"
#include <cassert>
int main() {
 using namespace gfxvk::worker_present_policy;
 int queries=0;VkDriverId id=VK_DRIVER_ID_MESA_RADV;
 auto query=[&]{++queries;return id;};
 assert(resolve(false,VK_API_VERSION_1_3,true,true,false,query)==Mode::ConservativeProducer);
 assert(resolve(true,VK_API_VERSION_1_1,false,true,false,query)==Mode::ConservativeProducer);
 assert(resolve(true,VK_API_VERSION_1_2,false,false,false,query)==Mode::ConservativeProducer);
 assert(queries==0);
 assert(resolve(true,VK_API_VERSION_1_1,true,true,false,query)==Mode::NativeWorker);
 assert(resolve(true,VK_API_VERSION_1_2,false,true,false,query)==Mode::NativeWorker);
 const VkDriverId native[] = {VK_DRIVER_ID_AMD_PROPRIETARY,VK_DRIVER_ID_AMD_OPEN_SOURCE,VK_DRIVER_ID_MESA_RADV,VK_DRIVER_ID_NVIDIA_PROPRIETARY,VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS,VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA,VK_DRIVER_ID_IMAGINATION_PROPRIETARY,VK_DRIVER_ID_QUALCOMM_PROPRIETARY,VK_DRIVER_ID_ARM_PROPRIETARY,VK_DRIVER_ID_GOOGLE_SWIFTSHADER,VK_DRIVER_ID_BROADCOM_PROPRIETARY,VK_DRIVER_ID_MESA_LLVMPIPE,VK_DRIVER_ID_COREAVI_PROPRIETARY,VK_DRIVER_ID_VERISILICON_PROPRIETARY,VK_DRIVER_ID_MESA_TURNIP,VK_DRIVER_ID_MESA_V3DV,VK_DRIVER_ID_MESA_PANVK,VK_DRIVER_ID_SAMSUNG_PROPRIETARY,VK_DRIVER_ID_MESA_NVK,VK_DRIVER_ID_IMAGINATION_OPEN_SOURCE_MESA,static_cast<VkDriverId>(26)};
 for(auto value:native) {
   id=value;
   assert(resolve(true,VK_API_VERSION_1_3,false,true,false,query)==Mode::NativeWorker);
   assert(resolve(true,VK_API_VERSION_1_3,false,true,true,query)==Mode::ConservativeProducer);
 }
 const VkDriverId conservative[] = {VK_DRIVER_ID_MOLTENVK,static_cast<VkDriverId>(28),
   VK_DRIVER_ID_MESA_VENUS,VK_DRIVER_ID_MESA_DOZEN,VK_DRIVER_ID_JUICE_PROPRIETARY,
   VK_DRIVER_ID_GGP_PROPRIETARY,static_cast<VkDriverId>(27),
   static_cast<VkDriverId>(0),static_cast<VkDriverId>(0x7fffffff)};
 for(auto value:conservative) {
   id=value;
   assert(resolve(true,VK_API_VERSION_1_3,false,true,false,query)==Mode::ConservativeProducer);
   assert(resolve(true,VK_API_VERSION_1_3,false,true,true,query)==Mode::ConservativeProducer);
 }
}
