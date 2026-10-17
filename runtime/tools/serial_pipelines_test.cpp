#include "gfx/vulkan/loader.h"
#include "gfx/vulkan/serial_pipelines.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <vector>

namespace {
std::atomic<int> active{}, maximum{}, calls{}, renderingLookups{};
VkResult work() {
  int n = ++active;
  int old = maximum.load();
  while (old < n && !maximum.compare_exchange_weak(old, n)) {}
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  ++calls;
  --active;
  return VK_PIPELINE_COMPILE_REQUIRED;
}
VKAPI_ATTR VkResult VKAPI_CALL graphics(VkDevice, VkPipelineCache, uint32_t,
    const VkGraphicsPipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline*) { return work(); }
VKAPI_ATTR VkResult VKAPI_CALL compute(VkDevice, VkPipelineCache, uint32_t,
    const VkComputePipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline*) { return work(); }
VKAPI_ATTR VkResult VKAPI_CALL cache(VkDevice, VkPipelineCache, size_t*, void*) { return work(); }
VKAPI_ATTR VkResult VKAPI_CALL merge(VkDevice, VkPipelineCache, uint32_t, const VkPipelineCache*) { return work(); }
VKAPI_ATTR VkResult VKAPI_CALL create_cache(VkDevice, const VkPipelineCacheCreateInfo*,
    const VkAllocationCallbacks*, VkPipelineCache*) { return work(); }
VKAPI_ATTR void VKAPI_CALL unused() {}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL get_proc(VkDevice, const char* name) {
  if (std::strstr(name,"vkCmdBeginRendering")==name || std::strstr(name,"vkCmdEndRendering")==name)++renderingLookups;
  if (!std::strcmp(name, "vkCreateGraphicsPipelines")) return reinterpret_cast<PFN_vkVoidFunction>(graphics);
  if (!std::strcmp(name, "vkCreateComputePipelines")) return reinterpret_cast<PFN_vkVoidFunction>(compute);
  if (!std::strcmp(name, "vkGetPipelineCacheData")) return reinterpret_cast<PFN_vkVoidFunction>(cache);
  if (!std::strcmp(name, "vkMergePipelineCaches")) return reinterpret_cast<PFN_vkVoidFunction>(merge);
  if (!std::strcmp(name, "vkCreatePipelineCache")) return reinterpret_cast<PFN_vkVoidFunction>(create_cache);
  return unused;
}
}
int main() {
  using namespace gfxvk;
  // Driver version decoding as the drivers report it: release in bits 12..21.
  // Snapdragon 855/865/870 stock firmware ships v502/v615 (affected); v700+
  // and newer phones (e.g. Snapdragon 8 Gen 3, v800-series) are not.
  struct Case { uint32_t driverVersion; uint32_t release; bool serial; };
  for (const Case c : {Case{(512u << 22) | (502u << 12) | 0, 502, true},
                       Case{(512u << 22) | (615u << 12) | 3, 615, true},
                       Case{(512u << 22) | (699u << 12) | 0, 699, true},
                       Case{(512u << 22) | (700u << 12) | 0, 700, false},
                       Case{(512u << 22) | (762u << 12) | 9, 762, false},
                       Case{(512u << 22) | (800u << 12) | 0, 800, false},
                       Case{(512u << 22) | (1023u << 12) | 4095, 1023, false}}) {
    assert(qualcomm_driver_release(c.driverVersion) == c.release);
    assert(serial_pipelines_wanted(VK_DRIVER_ID_QUALCOMM_PROPRIETARY, c.driverVersion, nullptr) == c.serial);
    assert(serial_pipelines_wanted(VK_DRIVER_ID_QUALCOMM_PROPRIETARY, c.driverVersion, "") == c.serial);
    // Other drivers on the same version numbers never take the rule.
    for (VkDriverId other : {VK_DRIVER_ID_MESA_TURNIP, VK_DRIVER_ID_ARM_PROPRIETARY, VK_DRIVER_ID_MOLTENVK,
                             VK_DRIVER_ID_NVIDIA_PROPRIETARY, VK_DRIVER_ID_AMD_PROPRIETARY, VkDriverId(0)})
      assert(!serial_pipelines_wanted(other, c.driverVersion, nullptr));
    assert(serial_pipelines_wanted(VK_DRIVER_ID_MESA_TURNIP, c.driverVersion, "1"));
    assert(!serial_pipelines_wanted(VK_DRIVER_ID_QUALCOMM_PROPRIETARY, c.driverVersion, "0"));
  }
  for (unsigned release : {0, 502, 699, 700, 762}) {
    auto v = VK_MAKE_VERSION(1, release, 42);
    assert(serial_pipelines_wanted(VK_DRIVER_ID_QUALCOMM_PROPRIETARY, v, nullptr) == (release < 700));
    assert(!serial_pipelines_wanted(VK_DRIVER_ID_MESA_TURNIP, v, nullptr));
    assert(!serial_pipelines_wanted(VK_DRIVER_ID_QUALCOMM_PROPRIETARY, v, "0"));
    assert(serial_pipelines_wanted(VK_DRIVER_ID_MESA_TURNIP, v, "1"));
  }
  assert(!serial_pipelines_wanted(VK_DRIVER_ID_NVIDIA_PROPRIETARY, 0, nullptr));
  assert(!serial_pipelines_wanted(VkDriverId(0), 0, nullptr));
  assert(!serial_pipelines_wanted(VK_DRIVER_ID_MESA_TURNIP, 0, "auto"));
  vkGetDeviceProcAddr = get_proc;
  // Reinitialization must not retain a wrapper or the previous device's setting.
  for (int round = 0; round < 4; ++round) {
    // The first load deliberately takes the classic early return: its raw
    // pipeline/cache entry points must still be captured before wrappers start.
    const bool classic=(round%2)==0;
    renderingLookups=0;
    load_device_functions(VK_NULL_HANDLE, round>=2, classic);
    assert(renderingLookups==(classic?0:2));
    assert((vkCmdBeginRendering==nullptr)==classic);
    assert((vkCmdEndRendering==nullptr)==classic);
    assert(vkCreateGraphicsPipelines == graphics);
    serialize_pipeline_creation(true);
    active = maximum = calls = 0;
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) threads.emplace_back([i] {
      for (int j = 0; j < 4; ++j) {
        VkResult result;
        switch (i % 4) {
          case 0: result = vkCreateGraphicsPipelines(VK_NULL_HANDLE, VK_NULL_HANDLE, 0, nullptr, nullptr, nullptr); break;
          case 1: result = vkCreateComputePipelines(VK_NULL_HANDLE, VK_NULL_HANDLE, 0, nullptr, nullptr, nullptr); break;
          case 2: result = j % 2 ? vkGetPipelineCacheData(VK_NULL_HANDLE, VK_NULL_HANDLE, nullptr, nullptr)
                                 : vkCreatePipelineCache(VK_NULL_HANDLE, nullptr, nullptr, nullptr); break;
          default: {
            // A held serial lock excludes the wrapped calls and nests with them.
            auto held = pipeline_serial_lock();
            assert(held.owns_lock());
            result = vkMergePipelineCaches(VK_NULL_HANDLE, VK_NULL_HANDLE, 0, nullptr);
            break;
          }
        }
        assert(result == VK_PIPELINE_COMPILE_REQUIRED);
      }
    });
    for (auto& t : threads) t.join();
    assert(maximum == 1 && calls == 32);
    serialize_pipeline_creation(false);
    assert(!pipeline_serial_lock().owns_lock());
    assert(vkCreateGraphicsPipelines == graphics);
    assert(vkCreateComputePipelines == compute);
    assert(vkGetPipelineCacheData == cache);
    assert(vkMergePipelineCaches == merge);
    assert(vkCreatePipelineCache == create_cache);
    // A new device starts with direct entry points and no lock.
    serialize_pipeline_creation(true);
    load_device_functions(VK_NULL_HANDLE, false, true);
    assert(!pipeline_serial_lock().owns_lock() && vkCreatePipelineCache == create_cache);
  }
}
