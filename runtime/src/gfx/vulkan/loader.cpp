// Vulkan entry points loaded at run time (loader.h).
#include "../../exception_report.h"
#include "loader.h"
#include "compile_journal.h"
#include <unordered_map>
#include <vector>
#include <algorithm>
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

// ---- compile journal (compile_journal.h): every shader module and graphics pipeline of the renderer is created here
PFN_vkCreateGraphicsPipelines rawGraphicsPipelines;
PFN_vkCreateShaderModule rawCreateShaderModule;
PFN_vkDestroyShaderModule rawDestroyShaderModule;
std::mutex moduleIdsMutex;
std::unordered_map<VkShaderModule, uint64_t> moduleIds;  // the SPIR-V id of each live module
VKAPI_ATTR VkResult VKAPI_CALL journal_create_module(VkDevice device, const VkShaderModuleCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkShaderModule* module) {
  namespace cj = compile_journal;
  const uint64_t id = cj::module_id(info->pCode, info->codeSize);
  if (cj::left_out(cj::kModule, id)) {
    // left out: no module (the callers treat creation as infallible); a pipeline with it fails as usual
    *module = VK_NULL_HANDLE;
    return VK_SUCCESS;
  }
  VkResult result;
  {
    cj::Scope scope(cj::kModule, id);
    result = rawCreateShaderModule(device, info, allocator, module);
  }
  if (result == VK_SUCCESS && *module) {
    std::lock_guard lock(moduleIdsMutex);
    moduleIds[*module] = id;
  }
  return result;
}
VKAPI_ATTR void VKAPI_CALL journal_destroy_module(VkDevice device, VkShaderModule module, const VkAllocationCallbacks* allocator) {
  if (!module) return;
  {
    std::lock_guard lock(moduleIdsMutex);
    moduleIds.erase(module);
  }
  rawDestroyShaderModule(device, module, allocator);
}
// the pipeline's id from its stages' modules; false: a stage's module was left out
bool pipeline_id(const VkGraphicsPipelineCreateInfo& info, uint64_t& id) {
  uint64_t ids[8]{};
  const uint32_t n = std::min<uint32_t>(info.stageCount, 8);
  std::lock_guard lock(moduleIdsMutex);
  for (uint32_t i = 0; i < n; ++i) {
    const VkShaderModule m = info.pStages[i].module;
    if (!m) return false;
    auto it = moduleIds.find(m);
    ids[i] = it == moduleIds.end() ? 0 : it->second;
  }
  id = compile_journal::pipeline_id(ids, n);
  return true;
}
VKAPI_ATTR VkResult VKAPI_CALL journal_graphics(VkDevice device, VkPipelineCache cache, uint32_t count,
    const VkGraphicsPipelineCreateInfo* info, const VkAllocationCallbacks* allocator, VkPipeline* out) {
  namespace cj = compile_journal;
  uint64_t first = 0;
  for (uint32_t i = 0; i < count; ++i) {
    uint64_t id = 0;
    if (!pipeline_id(info[i], id) || cj::left_out(cj::kPipeline, id)) {
      for (uint32_t k = 0; k < count; ++k) out[k] = VK_NULL_HANDLE;
      return VK_ERROR_INITIALIZATION_FAILED;  // an ordinary pipeline failure (pipeline_failure.h): its draws are left out
    }
    if (!i) first = id;
  }
  cj::Scope scope(cj::kPipeline, first);
  return rawGraphicsPipelines(device, cache, count, info, allocator, out);
}

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
  if (compile_journal::active()) {  // (opened by the backend before the device; tests leave it closed)
    rawGraphicsPipelines = vkCreateGraphicsPipelines;
    rawCreateShaderModule = vkCreateShaderModule;
    rawDestroyShaderModule = vkDestroyShaderModule;
    vkCreateGraphicsPipelines = journal_graphics;
    vkCreateShaderModule = journal_create_module;
    vkDestroyShaderModule = journal_destroy_module;
  }
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
