// Vulkan entry points loaded at run time (loader.h).
#include "../../exception_report.h"
#include "loader.h"
#include <stdexcept>
#include <string>
#include <atomic>
#include <mutex>
#ifdef __ANDROID__
#include "android_driver.h"
#include "result_observer.h"
#endif

namespace gfxvk {
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion = nullptr;
PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties = nullptr;
#define WWHD_VK_DEFINE(name) PFN_##name name = nullptr;
WWHD_VK_GLOBAL_FUNCTIONS(WWHD_VK_DEFINE)
WWHD_VK_INSTANCE_FUNCTIONS(WWHD_VK_DEFINE)
WWHD_VK_DEVICE_FUNCTIONS(WWHD_VK_DEFINE)
WWHD_VK_RENDERING_FUNCTIONS(WWHD_VK_DEFINE)
#undef WWHD_VK_DEFINE

namespace {
// Old Qualcomm proprietary drivers can hang when different renderer threads
// compile simultaneously, even with separate caches. Cache creation from saved
// data, snapshots and merges use the same lock. Install these wrappers once at
// device initialization only. Recursive: pipeline_serial_lock() holds it across
// several wrapped calls (cache size + data).
std::recursive_mutex pipelineMutex;
std::atomic<bool> serialEnabled{false};
PFN_vkCreateGraphicsPipelines graphicsPipelines;
PFN_vkCreateComputePipelines computePipelines;
PFN_vkGetPipelineCacheData cacheData;
PFN_vkMergePipelineCaches mergeCaches;
PFN_vkCreatePipelineCache createCache;

VKAPI_ATTR VkResult VKAPI_CALL serial_graphics(VkDevice device, VkPipelineCache cache, uint32_t count,
    const VkGraphicsPipelineCreateInfo* info, const VkAllocationCallbacks* allocator, VkPipeline* out) {
  std::lock_guard lock(pipelineMutex);
  return graphicsPipelines(device, cache, count, info, allocator, out);
}
VKAPI_ATTR VkResult VKAPI_CALL serial_compute(VkDevice device, VkPipelineCache cache, uint32_t count,
    const VkComputePipelineCreateInfo* info, const VkAllocationCallbacks* allocator, VkPipeline* out) {
  std::lock_guard lock(pipelineMutex);
  return computePipelines(device, cache, count, info, allocator, out);
}
VKAPI_ATTR VkResult VKAPI_CALL serial_cache_data(VkDevice device, VkPipelineCache cache, size_t* size, void* data) {
  std::lock_guard lock(pipelineMutex);
  return cacheData(device, cache, size, data);
}
VKAPI_ATTR VkResult VKAPI_CALL serial_merge(VkDevice device, VkPipelineCache cache, uint32_t count,
    const VkPipelineCache* sources) {
  std::lock_guard lock(pipelineMutex);
  return mergeCaches(device, cache, count, sources);
}
VKAPI_ATTR VkResult VKAPI_CALL serial_create_cache(VkDevice device, const VkPipelineCacheCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkPipelineCache* cache) {
  std::lock_guard lock(pipelineMutex);
  return createCache(device, info, allocator, cache);
}
template<auto Slot,typename Function> Function observe(Function function) {
#ifdef __ANDROID__
  return detail::ResultObserver<Slot,Function,VkResult,VK_ERROR_DEVICE_LOST,drivers::note_device_lost>::bind(function);
#else
  return function;
#endif
}
[[noreturn]] void missing(const char *name) {
  exception_report::raise(std::string("the Vulkan driver does not provide ") + name +
                           "; update the graphics driver");
}
}  // namespace

void load_global_functions(PFN_vkGetInstanceProcAddr gipa) {
  if (!gipa)
    exception_report::raise("the Vulkan loader has no vkGetInstanceProcAddr");
  vkGetInstanceProcAddr = gipa;
#define WWHD_VK_LOAD(name) \
  if (!(name = observe<&name>(reinterpret_cast<PFN_##name>(gipa(nullptr, #name))))) missing(#name);
  WWHD_VK_GLOBAL_FUNCTIONS(WWHD_VK_LOAD)
#undef WWHD_VK_LOAD
  // Vulkan 1.1 loaders and newer; a 1.0 loader has none (the caller reports it)
  vkEnumerateInstanceVersion =
      observe<&vkEnumerateInstanceVersion>(reinterpret_cast<PFN_vkEnumerateInstanceVersion>(gipa(nullptr, "vkEnumerateInstanceVersion")));
  // only for the log (the layers in the process); a loader without it is not refused
  vkEnumerateInstanceLayerProperties = observe<&vkEnumerateInstanceLayerProperties>(reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(
      gipa(nullptr, "vkEnumerateInstanceLayerProperties")));
}

void load_instance_functions(VkInstance instance) {
#define WWHD_VK_LOAD(name) \
  if (!(name = observe<&name>(reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name))))) missing(#name);
  WWHD_VK_INSTANCE_FUNCTIONS(WWHD_VK_LOAD)
#undef WWHD_VK_LOAD
}

void serialize_pipeline_creation(bool enabled) {
  serialEnabled = enabled;
  vkCreateGraphicsPipelines = enabled ? serial_graphics : graphicsPipelines;
  vkCreateComputePipelines = enabled ? serial_compute : computePipelines;
  vkGetPipelineCacheData = enabled ? serial_cache_data : cacheData;
  vkMergePipelineCaches = enabled ? serial_merge : mergeCaches;
  vkCreatePipelineCache = enabled ? serial_create_cache : createCache;
}
std::unique_lock<std::recursive_mutex> pipeline_serial_lock() {
  return serialEnabled ? std::unique_lock(pipelineMutex) : std::unique_lock<std::recursive_mutex>();
}

void load_device_functions(VkDevice device, bool khrDynamicRendering, bool renderPasses) {
#define WWHD_VK_LOAD(name) \
  if (!(name = observe<&name>(reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name))))) missing(#name);
  WWHD_VK_DEVICE_FUNCTIONS(WWHD_VK_LOAD)
#undef WWHD_VK_LOAD
  graphicsPipelines = vkCreateGraphicsPipelines;
  computePipelines = vkCreateComputePipelines;
  cacheData = vkGetPipelineCacheData;
  mergeCaches = vkMergePipelineCaches;
  createCache = vkCreatePipelineCache;
  serialEnabled = false; // a new device starts unwrapped until serialize_pipeline_creation
  if (renderPasses) { vkCmdBeginRendering=nullptr; vkCmdEndRendering=nullptr; return; }
  // the core 1.3 function or the extension's: same parameters (VkRenderingInfoKHR = VkRenderingInfo)
#define WWHD_VK_LOAD(name)                                                                          \
  {                                                                                                 \
    const char *n = khrDynamicRendering ? #name "KHR" : #name;                                     \
    if (!(name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, n)))) missing(n);        \
  }
  WWHD_VK_RENDERING_FUNCTIONS(WWHD_VK_LOAD)
#undef WWHD_VK_LOAD
}
}  // namespace gfxvk
