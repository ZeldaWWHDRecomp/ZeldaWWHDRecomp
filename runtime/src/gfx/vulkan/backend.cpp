#include "background_shaders.h"
#include "vertex_cpu_policy.h"
#include "dynres.h"
#include "platform/keycodes.h"
#include "input_map.h"
#include "atomic_temporary.h"
#include "validation_diagnostics.h"
#include "../../audio_out.h"
#include "../../interp_pacing.h"
#ifndef VK_ENABLE_BETA_EXTENSIONS
#define VK_ENABLE_BETA_EXTENSIONS
#endif
#ifndef __ANDROID__
#define WWHD_CREATE_WINDOW_SURFACE SDL_Vulkan_CreateSurface
#else
#define WWHD_CREATE_WINDOW_SURFACE drivers::create_surface
#endif
#if defined(__APPLE__) && !defined(WWHD_SDL_HOST)
#define VK_USE_PLATFORM_METAL_EXT  // VK_EXT_metal_surface: AppKit views' CAMetalLayers
#endif
#include "../../exception_report.h"
#include "platform/present_observer.h"
#ifdef __ANDROID__
#define VK_USE_PLATFORM_ANDROID_KHR
#include <android/native_window_jni.h>
#include <jni.h>
#include "platform/quest_theater.h"
#endif
#include "backend.h"
#include "../../dsweep.h"
#include "../hidden_presentation.h"
#include "pass.h"
#include "rendering_path.h"
#include "stereo.h"
#include "../hdr.h"
#include "serial_pipelines.h"
#include "bc_decoder_choice.h"
#include "attachment_view_usage.h"
#include "background_cache_readiness.h"
#include "../../build_info.h"
#include "deferred_commands.h"
#include "record_worker.h"
#include "submission_cpu_ack.h"
#include "submission_pool.h"
#include "worker_present_policy.h"
#include "handoff_trace.h"
#include <optional>
#include "deferred_dispatch.h"
#include "pipeline_cache_path.h"
#include "bc_decode.h"
#include "shader_compiler.h"
#include "android_driver.h"
#include "stutter_trace.h"
#include "pipeline_worker.h"
#include "pipeline_demand.h"
#include "buffer_cache.h"
#include "render_prof.h"
#include "perf_metrics.h"
#include "report_header.h"
#include "present.h"
#include "present_worker.h"
#include "android_isolated_presenter.h"
#include "presentation_queue.h"
#include "gfx/display.h"
#include "gfx/display_modes.h"
#include "gx2/gx2.h"
#include "input.h"
#include "mods/mods.h"
#include "overlay/hostui.h"
#include "overlay/overlay.h"
#include "overlay/guest_hud.h"
#ifdef WWHD_SDL_HOST
#include "platform/input_sdl.h"
#include "overlay/window_target.h"
#include <SDL3/SDL_vulkan.h>
#endif
#include "platform/display_rate.h"
#include "platform/host.h"
#include "platform/perf_hint.h"
#include "platform/gamepad_touch.h"
#include "platform/dual_display.h"
#include "runtime.h"
#include "screenshot.h"
#include "shaders.h"
#include "settings.h"
#include "savestate.h"
#include "gfx/capture_schedule.h"
#include "sparse_hash_memo.h"
#include "write_watch.h"
#include <algorithm>
#include <functional>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <future>
#include <mutex>
#ifdef __APPLE__
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#endif
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#ifndef _WIN32
#include <time.h>
#endif
#if defined(__linux__)
#include <unistd.h>
#endif
#include <unordered_set>
#include <vulkan/vulkan_beta.h>

// Opt-in only: no process queries or cache traversal on the normal frame path.
// The regular LOG ring includes these aggregate diagnostics in crash reports.
namespace {
uint64_t memory_log_interval() {
  static const uint64_t interval=[] {
    const char* value=std::getenv("WWHD_VK_MEMORY_LOG_FRAMES");
    if (!value || !*value || *value=='-') return uint64_t{0};
    char* end=nullptr;
    const auto parsed=std::strtoull(value,&end,10);
    return end!=value && !*end ? uint64_t(parsed) : uint64_t{0};
  }();
  return interval;
}
uint64_t resident_bytes() {
#if defined(__APPLE__)
  mach_task_basic_info_data_t info{};
  mach_msg_type_number_t count=MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(),MACH_TASK_BASIC_INFO,
                reinterpret_cast<task_info_t>(&info),&count)==KERN_SUCCESS)
    return info.resident_size;
#elif defined(__linux__)
  if (FILE* file=std::fopen("/proc/self/statm","r")) {
    unsigned long long total=0,resident=0;
    const bool ok=std::fscanf(file,"%llu %llu",&total,&resident)==2;
    std::fclose(file);
    const long page=sysconf(_SC_PAGESIZE);
    if (ok && page>0) return resident*uint64_t(page);
  }
#endif
  return 0; // unavailable, not a peak-RSS measurement
}
}

namespace gx2 { uint64_t flips_presented(); void checkpoint_vulkan_caches(); void shutdown_vulkan_caches(); void request_background_vulkan_cache_checkpoint(); }
#include "../../interp.h"
#include "../../fixed_seed.h"

extern uint32_t g_latteCpuVertexClasses;
extern bool g_latteCpuVertexDecode;  // Cemu GLSL attribute decoder (LatteDecompiler.h), issue #207 diagnostic
namespace gfxvk {
Renderer R;
namespace {
// Opt-in TV-only observation. IDs never derive from a mutable drawable-acquire
// ticket: command encoding may acquire the Metal drawable on another thread.
struct ObservedPresent { uint64_t epoch, frame; };
bool presentObservation = false;
PFN_vkGetPastPresentationTimingGOOGLE observedHistory = nullptr;
uint32_t nextObservedPresent = 0;
constexpr size_t MaxObservedPresents = 65536;
std::unordered_map<uint32_t, ObservedPresent> pendingObservedPresents;
}
void poll_present_observer() {
  if (!presentObservation || !observedHistory || !R.tv.swapchain) return;
  uint32_t count = 0;
  VkResult result = observedHistory(R.device, R.tv.swapchain, &count, nullptr);
  if (result != VK_SUCCESS || count > MaxObservedPresents) {
    present_observer::fail("GOOGLE history count query failed/overflowed");
    return;
  }
  if (!count) return;
  std::vector<VkPastPresentationTimingGOOGLE> values(count);
  result = observedHistory(R.device, R.tv.swapchain, &count, values.data());
  if ((result != VK_SUCCESS && result != VK_INCOMPLETE) || count > values.size()) {
    present_observer::fail("GOOGLE history data query failed/overflowed");
    return;
  }
  // VK_INCOMPLETE still returns and consumes the first count records.
  for (uint32_t i = 0; i < count; ++i) {
    const auto& value = values[i];
    auto found = pendingObservedPresents.find(value.presentID);
    if (found == pendingObservedPresents.end() ||
        found->second.epoch != R.tv.swapchainGeneration) {
      present_observer::fail("unknown/duplicate/wrong-epoch GOOGLE present ID");
      continue;
    }
    const auto immutable = found->second;
    present_observer::google_history(immutable.epoch, value.presentID, immutable.frame,
        value.desiredPresentTime, value.actualPresentTime,
        value.earliestPresentTime, value.presentMargin);
    pendingObservedPresents.erase(found);
  }
}
static void drain_observed_history() {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  do {
    poll_present_observer();
    if (pendingObservedPresents.empty()) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  } while (std::chrono::steady_clock::now() < deadline);
  if (!pendingObservedPresents.empty())
    present_observer::fail("missing GOOGLE history at bounded scripted endpoint");
}
void wait_idle();
void flush_present_observer() {
  if (fallback_stats::storage) {
    fallback_stats::begin_frame(R.frame);
    drain_record_commands(fallback_stats::Boundary::Endpoint);
    const bool complete = fallback_stats::flush();
    LOG("[vulkan fallback stats] %s", complete ? "complete" : "failed");
  }
  if (handoff_trace::enabled()) {
    drain_record_commands();
    const bool complete = handoff_trace::flush();
    LOG("[vulkan handoff trace] %s", complete ? "complete" : "failed");
  }
  if (!present_observer::enabled()) return;
  if (presentObservation && R.device && observedHistory) {
    // Caller owns the renderer execution mutex. No default-path queue wait.
    wait_idle();
    drain_observed_history();
  } else {
    present_observer::fail("present observation unavailable at endpoint");
  }
  present_observer::flush();
}
namespace { BackgroundCacheReadiness backgroundCacheReadiness; }
const VkFormatProperties& format_properties(VkFormat format) {
  auto [entry, inserted] = R.formatProperties.try_emplace(format);
  if (inserted) vkGetPhysicalDeviceFormatProperties(R.physicalDevice,format,&entry->second);
  return entry->second;
}
bool warm_prune_enabled() {
  static const bool on = [] {
    const char* prune = std::getenv("WWHD_VK_WARM_PRUNE");
    const char* warming = std::getenv("WWHD_WARMING_SHADERS");
    return prune && !std::strcmp(prune, "1") && warming && !std::strcmp(warming, "1");
  }();
  return on;
}
namespace {
bool perf_enabled() {
  static const bool enabled = std::getenv("WWHD_VK_STATS") != nullptr;
  return enabled;
}
bool cpu_only_stats_enabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("WWHD_VK_CPU_ONLY_STATS");
    return value && !std::strcmp(value, "1");
  }();
  return enabled;
}
struct WaitTiming { uint64_t count = 0, ns = 0; };
struct PresentTiming { WaitTiming acquire, compose, present, idle; };
WaitTiming submitWait, presentSubmitWait, queueSubmitTiming;
// activate_submission(): the render thread waits for the next ring slot's fence (submission_pool.h)
WaitTiming slotReuseWait;
// sampled at each swap with WWHD_VK_STATS: submissions/frames the GPU has not finished
struct InFlightStats { uint64_t samples=0,submissions=0,frames=0; unsigned maxSubmissions=0,maxFrames=0; };
InFlightStats inFlight;
unsigned submissionPoolSlots=0;
WaitTiming fenceStatusTiming, fenceResetTiming, retirementCleanupTiming;
WaitTiming commandEndTiming;
std::array<WaitTiming,5> swapPhaseTiming;
#ifdef __ANDROID__
VkSemaphore secondarySnapshotSignal=VK_NULL_HANDLE,secondarySnapshotWait=VK_NULL_HANDLE;
std::unique_ptr<AndroidIsolatedPresenter> isolatedSecondary;
bool isolatedSourceLinear=false;
std::optional<std::pair<size_t,uint64_t>> isolatedSnapshotSubmission;
#endif
PresentTiming screenTiming[2];
constexpr size_t maxPipelineCacheBytes = 64u * 1024u * 1024u;
std::string pipelineCachePath;
uint64_t pipelineCacheAttemptFrame = 0;
bool compatible_pipeline_cache(const std::vector<uint8_t>& bytes) {
  // Vulkan cache header fields are stored little-endian, independent of host.
  if (bytes.size() < 16 + VK_UUID_SIZE) return false;
  auto word = [&](size_t offset) {
    return uint32_t(bytes[offset]) | uint32_t(bytes[offset+1]) << 8 |
           uint32_t(bytes[offset+2]) << 16 | uint32_t(bytes[offset+3]) << 24;
  };
  uint32_t headerSize=word(0);
  return headerSize >= 16+VK_UUID_SIZE && headerSize <= bytes.size() &&
         word(4)==VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
         word(8)==R.properties.vendorID && word(12)==R.properties.deviceID &&
         !std::memcmp(bytes.data()+16,R.properties.pipelineCacheUUID,VK_UUID_SIZE);
}
// strictMul changes translated shaders; discard old driver pipelines before the 64 MB cap.
constexpr std::array<uint8_t,8> pipelineCacheMagic{'W','W','V','K','P','C','0','2'};
constexpr size_t pipelineCacheWrapperSize=24;
uint64_t pipeline_cache_checksum(const uint8_t* bytes,size_t size) {
  uint64_t hash=14695981039346656037ull;
  for (size_t i=0;i<size;++i) hash=(hash^bytes[i])*1099511628211ull;
  return hash;
}
std::array<uint8_t,pipelineCacheWrapperSize> pipeline_cache_wrapper(const std::vector<uint8_t>& bytes) {
  std::array<uint8_t,pipelineCacheWrapperSize> header{};
  std::copy(pipelineCacheMagic.begin(),pipelineCacheMagic.end(),header.begin());
  uint64_t size=bytes.size(),checksum=pipeline_cache_checksum(bytes.data(),bytes.size());
  for (unsigned i=0;i<8;++i) {
    header[8+i]=uint8_t(size>>(i*8));header[16+i]=uint8_t(checksum>>(i*8));
  }
  return header;
}
bool unpack_pipeline_cache(std::vector<uint8_t>& file) {
  if (file.size()<pipelineCacheWrapperSize ||
      !std::equal(pipelineCacheMagic.begin(),pipelineCacheMagic.end(),file.begin())) return false;
  auto word64=[&](size_t offset) {
    uint64_t value=0;
    for (unsigned i=0;i<8;++i) value|=uint64_t(file[offset+i])<<(i*8);
    return value;
  };
  uint64_t size=word64(8);
  if (size<16+VK_UUID_SIZE || size>maxPipelineCacheBytes ||
      size!=file.size()-pipelineCacheWrapperSize) return false;
  if (word64(16)!=pipeline_cache_checksum(file.data()+pipelineCacheWrapperSize,size)) return false;
  file.erase(file.begin(),file.begin()+pipelineCacheWrapperSize);
  return compatible_pipeline_cache(file);
}

// The device's memory budget for the warm pipeline pool (pipeline_worker.cpp): VK_EXT_memory_budget's budget of the
// largest device-local heap when the device has the extension, else that heap's size (MoltenVK: Metal's
// recommendedMaxWorkingSetSize); 0 when unknown.
static uint64_t device_memory_budget();
}  // namespace
uint64_t vulkan_device_memory_budget() {  // (surfaces.cpp texture budget)
  static const uint64_t value = device_memory_budget();
  return value;
}
namespace {
static uint64_t device_memory_budget() {
  VkPhysicalDeviceMemoryProperties p{};
  vkGetPhysicalDeviceMemoryProperties(R.physicalDevice, &p);
  uint32_t heap = UINT32_MAX;
  for (uint32_t i = 0; i < p.memoryHeapCount; ++i)
    if ((p.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) &&
        (heap == UINT32_MAX || p.memoryHeaps[i].size > p.memoryHeaps[heap].size)) heap = i;
  if (heap == UINT32_MAX) return 0;
  uint64_t budget = p.memoryHeaps[heap].size;
  uint32_t n = 0;
  vkEnumerateDeviceExtensionProperties(R.physicalDevice, nullptr, &n, nullptr);
  std::vector<VkExtensionProperties> extensions(n);
  if (n) vkEnumerateDeviceExtensionProperties(R.physicalDevice, nullptr, &n, extensions.data());
  const bool hasBudget = std::any_of(extensions.begin(), extensions.end(), [](const VkExtensionProperties& e) {
    return !std::strcmp(e.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
  });
  auto properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties2>(
      vkGetInstanceProcAddr(R.instance, "vkGetPhysicalDeviceMemoryProperties2"));
  if (hasBudget && properties2) {
    VkPhysicalDeviceMemoryBudgetPropertiesEXT b{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
    VkPhysicalDeviceMemoryProperties2 q{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
    q.pNext = &b;
    properties2(R.physicalDevice, &q);
    if (b.heapBudget[heap]) budget = b.heapBudget[heap];
  }
  LOG("[vulkan warmup] device memory budget %.0f MB (%s)", budget / 1e6, hasBudget && properties2 ? "VK_EXT_memory_budget" : "heap size");
  return budget;
}

void init_pipeline_cache() try {
  const auto explicitStorage=path_environment_utf8("WWHD_VK_PIPELINE_CACHE");
  const auto shaderStorage=path_environment_utf8("WWHD_SHADER_CACHE");
  const auto directoryStorage=path_environment_utf8("WWHD_VK_PIPELINE_CACHE_DIR");
  if (const char* explicitPath=explicitStorage ? explicitStorage->c_str() : nullptr) {
    if (std::strcmp(explicitPath,"0")) {
      pipelineCachePath=explicitPath;
#ifdef __ANDROID__
      // Keep overrides in the driver's cache namespace so removing a driver removes every cache.
      char key[32];
      std::snprintf(key,sizeof key,"override-%016llx.bin",static_cast<unsigned long long>(
          pipeline_cache_checksum(reinterpret_cast<const uint8_t*>(explicitPath),std::strlen(explicitPath))));
      pipelineCachePath = drivers::pipeline_directory()+"/"+key;
#endif
    }
  } else if (directoryStorage) {
    std::array<uint8_t,16> uuid{};
    std::copy(std::begin(R.properties.pipelineCacheUUID),std::end(R.properties.pipelineCacheUUID),uuid.begin());
    pipelineCachePath=*pipeline_cache_override(std::nullopt,directoryStorage,
        R.properties.vendorID,R.properties.deviceID,uuid);
    if (!R.renderPasses) adopt_legacy_pipeline_cache(pipelineCachePath,*pipeline_cache_override(std::nullopt,directoryStorage,
        R.properties.vendorID,R.properties.deviceID,uuid,true));
  } else if (const char* shaderPath=shaderStorage ? shaderStorage->c_str() : nullptr;
             !shaderPath || std::strcmp(shaderPath,"0")) {
    std::array<uint8_t,16> uuid{};
    std::copy(std::begin(R.properties.pipelineCacheUUID),std::end(R.properties.pipelineCacheUUID),uuid.begin());
    // a WWHD_SHADER_CACHE file (test runs, separate setups) keeps the pipeline cache next to it,
    // so such runs never write the user's own cache in the config folder
    auto place=[&](const std::string& name) {
      std::string path=shaderPath ? std::string(shaderPath)+"."+name
                                  :
#ifdef __ANDROID__
                                    drivers::pipeline_directory()+"/"+name;
#else
                                    host::config_dir()+"/shadercache/"+name;
#endif
#ifdef __ANDROID__
      if (shaderPath) {
        char key[32];
        std::snprintf(key,sizeof key,"shaders-%016llx-",static_cast<unsigned long long>(
            pipeline_cache_checksum(reinterpret_cast<const uint8_t*>(shaderPath),std::strlen(shaderPath))));
        path = drivers::pipeline_directory()+"/"+key+std::filesystem::path(path).filename().string();
      }
#endif
      return path;
    };
    const auto vendor=R.properties.vendorID, device=R.properties.deviceID;
    pipelineCachePath=place(device_pipeline_cache_filename(vendor,device,uuid));
    if (!R.renderPasses) adopt_legacy_pipeline_cache(pipelineCachePath,place(legacy_device_pipeline_cache_filename(vendor,device,uuid)));
  }
  if (R.renderPasses && !pipelineCachePath.empty()) pipelineCachePath += "-rp";
  std::vector<uint8_t> bytes;
  if (!pipelineCachePath.empty()) {
    std::error_code ec;
    uintmax_t size=std::filesystem::file_size(std::filesystem::u8path(pipelineCachePath),ec);
    if (!ec && size>=pipelineCacheWrapperSize+16+VK_UUID_SIZE &&
        size<=maxPipelineCacheBytes+pipelineCacheWrapperSize) {
      bytes.resize(size);
      FILE* file=open_utf8_file(pipelineCachePath,"rb");
      if (file) {
        if (std::fread(bytes.data(),1,bytes.size(),file)!=bytes.size() ||
            std::fgetc(file)!=EOF || std::ferror(file)) bytes.clear();
        std::fclose(file);
        if (!unpack_pipeline_cache(bytes)) bytes.clear();
      } else bytes.clear();
    }
  }
  // Pruning (WWHD_VK_WARM_PRUNE=1, setup's preparation only): the driver cache starts empty and the preparation
  // compiles every recipe of the database into it (headstart.cpp), so entries no recipe uses any more (older builds,
  // pipelines dropped from the database) are not carried over. Idea after aurora's driver cache pruning (MIT;
  // lib/webgpu/gpu_cache.cpp); no code taken.
  if (!bytes.empty() && warm_prune_enabled()) {
    LOG("[vulkan cache] pruning: %zu bytes of the old driver cache left out; the preparation rebuilds it from the recipes",
        bytes.size());
    bytes.clear();
  }
  VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
  ci.initialDataSize=bytes.size();ci.pInitialData=bytes.empty()?nullptr:bytes.data();
  VkResult result=vkCreatePipelineCache(R.device,&ci,nullptr,&R.pipelineCache);
  if (result!=VK_SUCCESS && !bytes.empty()) {
    ci.initialDataSize=0;ci.pInitialData=nullptr;
    result=vkCreatePipelineCache(R.device,&ci,nullptr,&R.pipelineCache);
    bytes.clear();
  }
  if (result!=VK_SUCCESS) {
    R.pipelineCache=VK_NULL_HANDLE;
    LOG("[vulkan cache] creation failed (%d); continuing without cache",result);
  } else if (perf_enabled()) {
    LOG("[vulkan cache] loaded %zu bytes; persistence %s",bytes.size(),
        pipelineCachePath.empty()?"disabled":pipelineCachePath.c_str());
  }
  if (async_pipeline_enabled()) start_pipeline_demand(R.device, bytes);
  if (!pipelineCachePath.empty()) {
    PipelineRecipeFingerprint fingerprint{};
    auto put = [&](size_t at, uint32_t value) {
      for (int i = 0; i < 4; ++i) fingerprint[at + i] = uint8_t(value >> (i * 8));
    };
    put(0, R.properties.vendorID); put(4, R.properties.deviceID);
    put(8, R.properties.driverVersion);
    std::memcpy(fingerprint.data() + 12, R.properties.pipelineCacheUUID, VK_UUID_SIZE);
    uint64_t features = pipeline_cache_checksum(reinterpret_cast<const uint8_t*>(&R.enabledFeatures), sizeof R.enabledFeatures);
    // Local recipes are runtime-build-specific as well as device-specific.
    // The full current fixed state is independently checked at handoff.
    for (const char* tag : {build::commit(), build::version(), WWHD_PIPELINE_RUNTIME_ID}) {
      for (const unsigned char* c = reinterpret_cast<const unsigned char*>(tag); *c; ++c)
        features = (features ^ *c) * 1099511628211ull;
      features = (features ^ 0xffu) * 1099511628211ull;
    }
    for (int i = 0; i < 8; ++i) fingerprint[28 + i] = uint8_t(features >> (8 * i));
    put(36, R.properties.limits.maxDescriptorSetUniformBuffersDynamic);
    put(40, 3); // Runtime-build identity, portable content IDs, exact descriptors and fixed-state recipe schema.
    put(44, (R.portabilitySubset ? 1u : 0u) | (R.separateStencilMaskRef ? 2u : 0u) |
        (R.constantAlphaColorBlendFactors ? 4u : 0u) | (R.vertexAttributeAccessBeyondStride ? 8u : 0u) |
        (R.vertexFormats.fallbackMask << 4) | (R.vertexDiagnostics.cpuVertices ? 0x100u : 0u) |
        (R.vertexDiagnostics.fullPrecision ? 0x200u : 0u) | (R.vertexDiagnostics.cpuClasses << 10));
    const char* warmup = std::getenv("WWHD_VK_PIPELINE_WARMUP");
    if (!warmup || std::strcmp(warmup, "0")) try {
      const auto headstart=path_environment_utf8("WWHD_HEADSTART");
      set_memory_budget(device_memory_budget());
      start_pipeline_worker(R.device, fingerprint, pipelineCachePath + ".recipes", bytes,
          headstart ? *headstart : config::game_dir + "/shadercache/headstart.bin");
    } catch (const std::exception& error) {
      LOG("[vulkan warmup] could not start worker: %s", error.what());
    }
  }
} catch (const std::exception& error) {
  pipelineCachePath.clear();
  VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
  if (!R.pipelineCache && vkCreatePipelineCache(R.device,&ci,nullptr,&R.pipelineCache)!=VK_SUCCESS)
    R.pipelineCache=VK_NULL_HANDLE;
  if (async_pipeline_enabled()) start_pipeline_demand(R.device, {});
  LOG("[vulkan cache] load disabled after error: %s",error.what());
}
static ValidationDiagnostics validationDiagnostics;
static void log_vulkan_result(VkResult result, const char* operation) {
  static std::atomic<unsigned> errors{0};
  if (result < 0 && errors.fetch_add(1) < 32)
    LOG("[vulkan result] %s: %d%s", operation, int(result), result == VK_ERROR_DEVICE_LOST ? " DEVICE_LOST" : "");
  if (result == VK_ERROR_DEVICE_LOST) {
    static std::atomic<bool> reported{false};
    if (!reported.exchange(true))
      for (const auto& message : validationDiagnostics.snapshot())
        LOG("[vulkan retained validation] %s", message.c_str());
  }
}
// profWait: also report the wait to the render-thread profiler (render_prof.h)
template<class F> VkResult timed_call(WaitTiming& timing, F&& call, int profWait = -1) {
  const bool profiled = profWait >= 0 && rprof::enabled();
  if (!perf_enabled() && !profiled) {
    const auto result = call();
    log_vulkan_result(result, "queue/presentation/wait");
    return result;
  }
  auto start = std::chrono::steady_clock::now();
  VkResult result = call();
  log_vulkan_result(result, "queue/presentation/wait");
  const uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - start).count();
  if (perf_enabled()) { ++timing.count; timing.ns += ns; }
  if (profiled) rprof::add_wait(rprof::Wait(profWait), ns);
  return result;
}
} // namespace
namespace {
struct PipelineCacheWriteResult {
  bool written = false;
  uint64_t revision = 0, changedFrame = 0;
  size_t size = 0;
  double elapsedMs = 0, checksumMs = 0;
  std::string error;
};
std::shared_future<PipelineCacheWriteResult> pipelineCacheWrite;
uint64_t pipelineCacheMergeRevision = 0;
uint64_t cache_revision() { return R.pipelineCreates + pipelineCacheMergeRevision; }
void merge_live_pipeline_caches(bool wait) {
  if (!R.pipelineCache || pipelineCachePath.empty()) return;
  const bool demand = merge_pipeline_demand_cache(R.pipelineCache, wait);
  const bool warm = merge_pipeline_worker_cache(R.pipelineCache, wait);
  if (demand || warm) { ++pipelineCacheMergeRevision; R.pipelineCacheDirty = true; }
}
// File work owns immutable bytes and a copied path. It never accesses Vulkan
// objects, renderer state, or render-thread logging.
PipelineCacheWriteResult write_pipeline_cache(std::vector<uint8_t> bytes,
                                              std::string path,
                                              uint64_t revision,
                                              uint64_t changedFrame) {
  PipelineCacheWriteResult result;
  result.revision=revision;result.changedFrame=changedFrame;result.size=bytes.size();
  auto start=std::chrono::steady_clock::now();
  std::string temporary;
  try {
    auto wrapper=pipeline_cache_wrapper(bytes);
    result.checksumMs=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-start).count();
    std::error_code ec;
    auto parent=std::filesystem::u8path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent,ec);
    if (ec) exception_report::raise(ec.message());
    FILE* file=open_atomic_temporary(path,temporary);
    if (file) {
      bool written=std::fwrite(wrapper.data(),1,wrapper.size(),file)==wrapper.size();
      if (written) written=std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();
      if (std::fclose(file)) written=false;
      if (written) result.written=host::replace_file(temporary,path);
    }
    if (!result.written) {
      std::filesystem::remove(std::filesystem::u8path(temporary),ec);
      result.error="file write or replacement failed";
    }
  } catch (const std::exception& error) {
    result.error=error.what();
    if (!temporary.empty()) {
      std::error_code ec;std::filesystem::remove(std::filesystem::u8path(temporary),ec);
    }
  }
  result.elapsedMs=std::chrono::duration<double,std::milli>(
      std::chrono::steady_clock::now()-start).count();
  return result;
}
void poll_pipeline_cache_write(bool wait) {
  if (!pipelineCacheWrite.valid()) return;
  if (!wait && pipelineCacheWrite.wait_for(std::chrono::seconds(0)) !=
                   std::future_status::ready) return;
  auto completed = std::move(pipelineCacheWrite);
  pipelineCacheWrite = {};
  auto result=completed.get();
  // A newer pipeline, even in the same guest frame, must remain dirty.
  if (result.written && result.revision==cache_revision() &&
      result.changedFrame==R.pipelineCacheChangedFrame)
    R.pipelineCacheDirty=false;
  if (result.written && perf_enabled())
    LOG("[vulkan cache] persisted %zu payload bytes in %.2f ms (checksum %.2f ms)",
        result.size,result.elapsedMs,result.checksumMs);
  else if (!result.written)
    LOG("[vulkan cache] save skipped: %s",result.error.c_str());
}
void start_pipeline_cache_write() {
  if (pipelineCacheWrite.valid() || !R.pipelineCache ||
      !R.pipelineCacheDirty || pipelineCachePath.empty()) return;
  // This is the only Vulkan cache read, serialized with pipeline creation on
  // the render thread. Worker serialization/checksum never holds that thread.
  // With serial pipeline creation, workers cannot change the cache between the
  // size query and the copy.
  const auto serial=pipeline_serial_lock();
  size_t size=0;
  if (vkGetPipelineCacheData(R.device,R.pipelineCache,&size,nullptr)!=VK_SUCCESS ||
      size<16+VK_UUID_SIZE || size>maxPipelineCacheBytes) return;
  std::vector<uint8_t> bytes(size);
  if (vkGetPipelineCacheData(R.device,R.pipelineCache,&size,bytes.data())!=VK_SUCCESS) return;
  bytes.resize(size);
  if (!compatible_pipeline_cache(bytes)) return;
  pipelineCacheWrite=std::async(std::launch::async,write_pipeline_cache,
      std::move(bytes),pipelineCachePath,cache_revision(),R.pipelineCacheChangedFrame).share();
}
void checkpoint_pipeline_cache() try {
  if (!R.pipelineCache || pipelineCachePath.empty()) return;
  poll_pipeline_cache_write(false);
  if (R.frame-pipelineCacheAttemptFrame>=120 && !pipelineCacheWrite.valid()) {
    const bool quiet = R.frame-R.pipelineCacheChangedFrame>=120;
    const bool overdue = R.frame-pipelineCacheAttemptFrame>=1800;
    // Probe worker caches even if utility pipelines never dirtied R's cache.
    // Do not stall rendering on an in-flight native pipeline compile.
    if (quiet || overdue) {
      pipelineCacheAttemptFrame=R.frame;
      merge_live_pipeline_caches(false);
      start_pipeline_cache_write();
    }
  }
} catch (const std::exception& error) {
  LOG("[vulkan cache] checkpoint skipped after error: %s",error.what());
}
} // namespace
void save_pipeline_cache(bool shutdown) try {
  // Explicit shutdown/checkpoint waits before _Exit, then persists any newer
  // revision created while the periodic job was running.
  drain_pending_guest_pipelines();
  if(shutdown) save_pipeline_recipes();
  else checkpoint_pipeline_recipes();
  if (shutdown && finish_pipeline_demand(R.pipelineCache)) {
    ++pipelineCacheMergeRevision; R.pipelineCacheDirty = true;
  }
  merge_live_pipeline_caches(true);
  poll_pipeline_cache_write(true);
  start_pipeline_cache_write();
  poll_pipeline_cache_write(true);
} catch (const std::exception& error) {
  LOG("[vulkan cache] save skipped after error: %s",error.what());
}
class VulkanFailure : public std::runtime_error {
public:
  VkResult result;
  VulkanFailure(VkResult value,const char* operation)
      : std::runtime_error(std::string(operation)+": Vulkan result "+std::to_string(value)),result(value) {}
};
void vk_check(VkResult r, const char *op) {
  if (r != VK_SUCCESS) {
#ifdef __ANDROID__
    if(r==VK_ERROR_DEVICE_LOST)drivers::note_device_lost();
#endif
    log_vulkan_result(r, op);
    VulkanFailure failure(r, op);
    exception_report::record(failure.what());  // logged like every runtime error (exception_report.h)
    throw failure;
  }
}
uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
  VkPhysicalDeviceMemoryProperties p;
  vkGetPhysicalDeviceMemoryProperties(R.physicalDevice, &p);
  for (uint32_t i = 0; i < p.memoryTypeCount; i++)
    if ((bits & (1u << i)) && (p.memoryTypes[i].propertyFlags & flags) == flags)
      return i;
  exception_report::raise("No compatible Vulkan memory type");
}
Buffer create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags flags, VkMemoryPropertyFlags preferred) {
  Buffer b;
  b.size = std::max<VkDeviceSize>(size, 16);
  VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  ci.size = b.size;
  ci.usage = usage;
  ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  vk_check(vkCreateBuffer(R.device, &ci, nullptr, &b.buffer), "create buffer");
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(R.device, b.buffer, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = memory_type(req.memoryTypeBits, flags);
  if (preferred) {
    VkPhysicalDeviceMemoryProperties p;
    vkGetPhysicalDeviceMemoryProperties(R.physicalDevice, &p);
    for (uint32_t i = 0; i < p.memoryTypeCount; i++)
      if ((req.memoryTypeBits & (1u << i)) &&
          (p.memoryTypes[i].propertyFlags & (flags | preferred)) == (flags | preferred)) {
        ai.memoryTypeIndex = i;
        break;
      }
  }
  {
    VkPhysicalDeviceMemoryProperties p;
    vkGetPhysicalDeviceMemoryProperties(R.physicalDevice, &p);
    b.properties = p.memoryTypes[ai.memoryTypeIndex].propertyFlags;
  }
  vk_check(vkAllocateMemory(R.device, &ai, nullptr, &b.memory),
           "allocate buffer memory");
  vk_check(vkBindBufferMemory(R.device, b.buffer, b.memory, 0),
           "bind buffer memory");
  if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    vk_check(vkMapMemory(R.device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped),
             "map buffer");
  return b;
}
// GPU -> CPU copies (captures): the CPU reads these, so host-cached memory where
// the device has it (uncached reads of the plain host-visible type are very slow on discrete GPUs).
Buffer create_readback_buffer(VkDeviceSize size) {
  return create_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
}
// The per-submission upload arena (and the buffer cache's blocks, buffer_cache.cpp) is written by the
// CPU and read by the GPU only. The memory is host-visible but usually not host-cached: uncached or
// write-combined system memory, or device-local BAR memory on discrete GPUs, where CPU reads are about
// 100 times slower than cached ones. Rule: the CPU never reads mapped upload memory. Reuse checks and
// index scans use CPU copies kept beside the slices (vertex_snapshot_history.h, uniform_snapshot.h,
// draw.cpp's index paths, the buffer cache's index shadows); see docs/vulkan.md. The one exception is
// the buffer cache's opt-in verify mode (WWHD_VK_BUFFER_CACHE_VERIFY=1, a diagnostic).
// Unless the memory is host-cached: if the arena's memory type is HOST_CACHED and HOST_COHERENT (Apple
// silicon/MoltenVK has only cached types; many UMA drivers too), reads cost what heap reads cost and the
// copies only add time, so R.uploadReadsDirect lets the reuse caches and the native index scan read the
// slices. CACHED without COHERENT never counts: the arena requires COHERENT (no flush/invalidate).
// WWHD_VK_UPLOAD_READS=auto (default) | shadow (always keep CPU copies) | direct (always read slices).
namespace {
enum class UploadReads { Auto, Shadow, Direct };
UploadReads upload_reads_mode() {
  static const UploadReads mode = [] {
    const char* e = std::getenv("WWHD_VK_UPLOAD_READS");
    if (!e || !*e || !std::strcmp(e, "auto")) return UploadReads::Auto;
    if (!std::strcmp(e, "shadow")) return UploadReads::Shadow;
    if (!std::strcmp(e, "direct")) return UploadReads::Direct;
    LOG("[vulkan] WWHD_VK_UPLOAD_READS=%s unknown (auto|shadow|direct): auto", e);
    return UploadReads::Auto;
  }();
  return mode;
}
void note_upload_block(const Buffer& buffer) {
  const VkMemoryPropertyFlags readable =
      VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  const bool first = R.uploadAllocations == 0;
  const bool cached = (buffer.properties & readable) == readable && (first || R.uploadCached);
  const auto mode = upload_reads_mode();
  const bool direct = mode == UploadReads::Direct || (mode == UploadReads::Auto && cached);
  if (first || direct != R.uploadReadsDirect)
    LOG("[vulkan] upload arena memory: %s; reuse checks %s (WWHD_VK_UPLOAD_READS=%s)",
        cached ? "host-cached" : "not host-cached", direct ? "read the slices" : "keep CPU copies",
        mode == UploadReads::Auto ? "auto" : mode == UploadReads::Shadow ? "shadow" : "direct");
  R.uploadCached = cached;
  R.uploadReadsDirect = direct;
}
}  // namespace
static UploadSlice upload_slice(VkDeviceSize& size, VkDeviceSize& alignment, bool grow);
UploadSlice allocate_upload(VkDeviceSize size, VkDeviceSize alignment) { return upload_slice(size,alignment,true); }
UploadSlice allocate_upload_no_grow(VkDeviceSize size, VkDeviceSize alignment) { return upload_slice(size,alignment,false); }
static UploadSlice upload_slice(VkDeviceSize& size, VkDeviceSize& alignment, bool grow) {
  size = std::max<VkDeviceSize>(size,16);
  alignment = std::max<VkDeviceSize>(alignment,4);
  auto slice = [&](Renderer::UploadBlock& block) {
    VkDeviceSize offset = ((block.used + alignment - 1) / alignment) * alignment;
    if (offset > block.buffer.size || size > block.buffer.size - offset)
      return UploadSlice{};
    block.used = offset + size;
    R.uploadBytes += size;
    rprof::add_upload(size);
    return UploadSlice{block.buffer.buffer,offset,size,
                       static_cast<uint8_t*>(block.buffer.mapped)+offset};
  };
  for (auto& block : R.uploadBlocks) {
    auto result = slice(block);
    if (result.buffer) return result;
  }
  if (!grow) return UploadSlice{};
  if (handoff_trace::enabled()) handoff_trace::emit(handoff_trace::Lane::Producer, handoff_trace::Kind::UploadGrowBegin, 0, R.frame, size, R.uploadBlocks.size());
  auto buffer = create_buffer(std::max<VkDeviceSize>(32ull<<20,size),
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (handoff_trace::enabled()) handoff_trace::emit(handoff_trace::Lane::Producer, handoff_trace::Kind::UploadGrowEnd, 0, R.frame, buffer.size, R.uploadBlocks.size());
  note_upload_block(buffer);
  R.uploadBlocks.push_back({buffer,0});
  ++R.uploadAllocations;
  return slice(R.uploadBlocks.back());
}
void defer_buffer(Buffer b) { R.garbageBuffers.push_back(b); }
void defer_surface_image(VkImage image, VkDeviceMemory memory,
                         std::vector<VkImageView> views) {
  forget_framebuffers(views.data(), views.size(), true);
  R.garbageImages.push_back({image, memory, std::move(views)});
}
void forget_texture_views() {
} // descriptor sets are rebuilt for every draw and retired on submit
namespace {
struct GpuScopeBucket {
  GpuScopeMetadata metadata{};
  double intervalNs=0,maxNs=0;
  uint64_t count=0,draws=0;
};
std::array<GpuScopeBucket,256> gpuScopeBuckets{};
uint32_t gpuScopeBucketCount=0;
struct GpuScopeTotals { uint64_t available=0,unavailable=0,zero=0,dropped=0,bucketDrops=0; } gpuScopeTotals;
bool same_gpu_scope(const GpuScopeMetadata& a,const GpuScopeMetadata& b) {
  return a.kind==b.kind && a.colors==b.colors && a.depth==b.depth &&
      a.extent.width==b.extent.width && a.extent.height==b.extent.height && a.extent.depth==b.extent.depth &&
      a.mips==b.mips && a.layers==b.layers && a.aspects==b.aspects && a.colorCount==b.colorCount &&
      a.slices==b.slices && a.depthSlice==b.depthSlice && a.depthOnly==b.depthOnly;
}
void add_gpu_scope(const Renderer::Submission::GpuScope& scope,double ns) {
  ++gpuScopeTotals.available;
  gpuScopeTotals.zero += ns==0;
  uint32_t i=0;
  while(i<gpuScopeBucketCount && !same_gpu_scope(gpuScopeBuckets[i].metadata,scope.metadata)) ++i;
  if(i==gpuScopeBucketCount) {
    if(i==gpuScopeBuckets.size()) { ++gpuScopeTotals.bucketDrops; return; }
    gpuScopeBuckets[i]={};gpuScopeBuckets[i].metadata=scope.metadata;++gpuScopeBucketCount;
  }
  auto& bucket=gpuScopeBuckets[i];
  bucket.intervalNs+=ns;bucket.maxNs=std::max(bucket.maxNs,ns);
  ++bucket.count;bucket.draws+=scope.draws;
}
} // namespace
static bool timestamp_capable(uint32_t bits, double period) {
  return bits > 0 && bits <= 64 && period > 0 && std::isfinite(period);
}
static uint64_t timestamp_elapsed_ticks(uint64_t start, uint64_t end, uint32_t bits) {
  const uint64_t mask = bits == 64 ? UINT64_MAX : (uint64_t{1} << bits) - 1;
  return (end - start) & mask;
}
// Overlay queries live independently of reusable submission slots: DrawDone can recycle a slot
// several times in the SAME frame. Keep its query pool until a later frame; never wait for a result.
struct OverlayGpuQuery {
  VkQueryPool pool = VK_NULL_HANDLE;
  uint64_t frame = 0, token = 0;
  bool pending = false;
};
static std::array<OverlayGpuQuery, 256> overlayGpuQueries{};
static void destroy_overlay_gpu_queries() {
  // Called only after the existing submission drain at shutdown.
  for (auto& q : overlayGpuQueries) {
    if (q.pool) vkDestroyQueryPool(R.device, q.pool, nullptr);
    q = {};
  }
}
static bool overlay_timestamp_capable() {
  return timestamp_capable(R.gpuTimestampValidBits, R.properties.limits.timestampPeriod);
}
static void begin_overlay_gpu_queries(VkCommandBuffer cmd, Renderer::Submission& submission) {
  submission.perfQuery = UINT32_MAX;
  if (!perf::enabled() || !overlay_timestamp_capable()) return;
  static uint64_t lastPoll = UINT64_MAX;
  if (lastPoll != R.frame) {
    lastPoll = R.frame;
    for (auto& q : overlayGpuQueries) {
      if (!q.pending || q.frame >= R.frame) continue;
      struct Result { uint64_t ticks, available; } values[2]{};
      perf::timestamp_read(R.frame - q.frame);
      VkResult result = vkGetQueryPoolResults(R.device, q.pool, 0, 2, sizeof(values), values,
          sizeof(Result), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
      if (result == VK_NOT_READY || (result == VK_SUCCESS &&
          (!values[0].available || !values[1].available))) continue;
      double ns = result == VK_SUCCESS
          ? double(timestamp_elapsed_ticks(values[0].ticks, values[1].ticks, R.gpuTimestampValidBits)) *
                double(R.properties.limits.timestampPeriod) : -1;
      perf::gpu_complete(q.frame, q.token, ns);
      q.pending = false;
    }
  }
  perf::gpu_advance(R.frame);
  uint64_t token = perf::gpu_begin(R.frame);
  if (!token) return;
  for (uint32_t i = 0; i < overlayGpuQueries.size(); ++i) {
    auto& q = overlayGpuQueries[i];
    if (q.pending) continue;
    if (!q.pool) {
      VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
      info.queryType = VK_QUERY_TYPE_TIMESTAMP; info.queryCount = 2;
      if (vkCreateQueryPool(R.device, &info, nullptr, &q.pool) != VK_SUCCESS) {
        perf::gpu_complete(R.frame, token, -1); return; // optional measurement
      }
    }
    q.frame = R.frame; q.token = token; q.pending = true;
    submission.perfQuery = i;
    record::vkCmdResetQueryPool(cmd, q.pool, 0, 2);
    record::vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, q.pool, 0);
    perf::timestamp_write();
    return;
  }
  // Bounded ring saturated: omit the entire frame rather than report a partial GPU interval.
  perf::gpu_complete(R.frame, token, -1);
}
static void destroy_gpu_timestamp_queries() {
  // Caller has already drained submissions; never destroy in-flight queries.
  destroy_overlay_gpu_queries();
  R.gpuTimestampsEnabled = false;
  R.gpuPassTimestampsEnabled = false;
  for (auto& slot : R.submissions) {
    if (slot.timestampQueries) vkDestroyQueryPool(R.device, slot.timestampQueries, nullptr);
    slot.timestampQueries = VK_NULL_HANDLE;
    slot.timestampRecorded = false;
    slot.gpuScopeCount = 0; slot.activeRenderScope = UINT32_MAX;
  }
}
static void init_gpu_timestamp_queries() {
  const char* requested = std::getenv("WWHD_VK_GPU_TIMESTAMPS");
  const char* passes = std::getenv("WWHD_VK_GPU_PASS_TIMESTAMPS");
  const bool passRequested = passes && !std::strcmp(passes, "1");
  if ((!requested || std::strcmp(requested, "1")) && !passRequested) return;
  if (!timestamp_capable(R.gpuTimestampValidBits, R.properties.limits.timestampPeriod)) {
    LOG("[vulkan GPU timestamps] unsupported: valid bits %u, period %.9g ns",
        R.gpuTimestampValidBits, double(R.properties.limits.timestampPeriod));
    return;
  }
  for (auto& slot : R.submissions) {
    VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = passRequested ? 2 + 2 * Renderer::Submission::maxGpuScopes : 2;
    const VkResult result = vkCreateQueryPool(R.device, &info, nullptr, &slot.timestampQueries);
    if (result != VK_SUCCESS) {
      // Initialization has submitted no work, so partial pools are safe to destroy.
      slot.timestampQueries = VK_NULL_HANDLE;
      bc_decode_shutdown();
  destroy_gpu_timestamp_queries();
      LOG("[vulkan GPU timestamps] optional query pools unavailable (%d); disabled", int(result));
      return;
    }
  }
  R.gpuTimestampsEnabled = true;
  R.gpuPassTimestampsEnabled = passRequested;
  if (passRequested) LOG("[vulkan GPU pass timestamps] enabled: 256 bounded scopes/submission; stage intervals may overlap or include stalls; NOT GPU busy time");
  LOG("[vulkan GPU timestamps] enabled: %u valid bits, %.9g ns/tick; submission intervals, NOT GPU busy time",
      R.gpuTimestampValidBits, double(R.properties.limits.timestampPeriod));
}
static void collect_gpu_timestamp_queries(Renderer::Submission& slot) {
  if (!slot.timestampRecorded) return;
  slot.timestampRecorded = false; // Consume this submission exactly once.
  const bool dynresTiming = slot.dynresTiming;
  slot.dynresTiming = false;
  if (dynresTiming && !R.gpuTimestampsEnabled) {
    // dynamic resolution only: the command buffer's GPU interval, without the diagnostics' statistics
    struct Result { uint64_t ticks, available; } values[2]{};
    const VkResult result = vkGetQueryPoolResults(R.device, slot.timestampQueries, 0, 2, sizeof(values), values,
        sizeof(Result), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (result != VK_SUCCESS || !values[0].available || !values[1].available) return;
    const uint64_t elapsed = timestamp_elapsed_ticks(values[0].ticks, values[1].ticks, R.gpuTimestampValidBits);
    const double period = double(R.properties.limits.timestampPeriod);
    const uint64_t mask = R.gpuTimestampValidBits >= 64 ? ~0ull : (1ull << R.gpuTimestampValidBits) - 1;
    const double start = double(values[0].ticks & mask) * period;
    dynres::gpu_interval(start, start + double(elapsed) * period);
    return;
  }
  if (!R.gpuTimestampsEnabled) return;
  struct Result { uint64_t ticks, available; } values[2]{};
  const VkResult result = vkGetQueryPoolResults(R.device, slot.timestampQueries, 0, 2,
      sizeof(values), values, sizeof(Result),
      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
  if (result == VK_NOT_READY || (result == VK_SUCCESS &&
      (!values[0].available || !values[1].available))) {
    ++R.gpuTimestampStats.unavailable;
    return;
  }
  if (result != VK_SUCCESS) {
    ++R.gpuTimestampStats.unavailable;
    R.gpuTimestampsEnabled = false; // Retain pools until the shutdown drain.
    LOG("[vulkan GPU timestamps] optional results failed (%d); disabled", int(result));
    return;
  }
  const double ns = double(timestamp_elapsed_ticks(values[0].ticks, values[1].ticks,
      R.gpuTimestampValidBits)) * double(R.properties.limits.timestampPeriod);
  if (rprof::enabled()) {  // the performance report's GPU timeline (busy, idle between submissions)
    const uint64_t mask = R.gpuTimestampValidBits >= 64 ? ~0ull : (1ull << R.gpuTimestampValidBits) - 1;
    const double start = double(values[0].ticks & mask) * double(R.properties.limits.timestampPeriod);
    rprof::add_gpu_span(start, start + ns);
  }
  if (dynresTiming) {
    const uint64_t mask = R.gpuTimestampValidBits >= 64 ? ~0ull : (1ull << R.gpuTimestampValidBits) - 1;
    const double start = double(values[0].ticks & mask) * double(R.properties.limits.timestampPeriod);
    dynres::gpu_interval(start, start + ns);
  }
  // Reporting resets the rolling window. Acceptance measurements need counters
  // that remain monotonic across those reports and across timing phases.
  for (auto* stats : {&R.gpuTimestampStats, &R.gpuTimestampLifetimeStats}) {
    ++stats->submissions;
    stats->intervalNs += ns;
    stats->maxIntervalNs = std::max(stats->maxIntervalNs, ns);
    stats->zeroIntervals += ns == 0;
  }
}
static void collect_gpu_pass_queries(Renderer::Submission& slot) {
  const uint32_t count=slot.gpuScopeCount;
  slot.gpuScopeCount=0;slot.activeRenderScope=UINT32_MAX;
  if(!count || !R.gpuTimestampsEnabled || !R.gpuPassTimestampsEnabled) return;
  struct Result { uint64_t ticks,available; };
  std::array<Result,2*Renderer::Submission::maxGpuScopes> values{};
  const VkResult result=vkGetQueryPoolResults(R.device,slot.timestampQueries,2,2*count,
      2*count*sizeof(Result),values.data(),sizeof(Result),
      VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
  if(result!=VK_SUCCESS && result!=VK_NOT_READY) {
    gpuScopeTotals.unavailable+=count;R.gpuTimestampsEnabled=false;
    LOG("[vulkan GPU pass timestamps] optional results failed (%d); disabled",int(result));
    return;
  }
  for(uint32_t i=0;i<count;++i) {
    if(!slot.gpuScopes[i].ended || !values[2*i].available || !values[2*i+1].available) {
      ++gpuScopeTotals.unavailable;continue;
    }
    const double ns=double(timestamp_elapsed_ticks(values[2*i].ticks,values[2*i+1].ticks,
        R.gpuTimestampValidBits))*double(R.properties.limits.timestampPeriod);
    add_gpu_scope(slot.gpuScopes[i],ns);
  }
}
static void report_gpu_pass_timestamps(double frames) {
  if(!R.gpuPassTimestampsEnabled) return;
  LOG("[vulkan GPU pass timestamps] %llu available; %llu unavailable; %llu zero; %llu capacity drops; %llu bucket drops; stage intervals NOT GPU busy time%s",
      (unsigned long long)gpuScopeTotals.available,(unsigned long long)gpuScopeTotals.unavailable,
      (unsigned long long)gpuScopeTotals.zero,(unsigned long long)gpuScopeTotals.dropped,
      (unsigned long long)gpuScopeTotals.bucketDrops,
      gpuScopeTotals.available && gpuScopeTotals.available==gpuScopeTotals.zero
          ? "; all-zero/emulated timing UNUSABLE" : "");
  std::array<uint32_t,256> order{};
  for(uint32_t i=0;i<gpuScopeBucketCount;++i) order[i]=i;
  std::sort(order.begin(),order.begin()+gpuScopeBucketCount,[](uint32_t a,uint32_t b) {
    return gpuScopeBuckets[a].intervalNs>gpuScopeBuckets[b].intervalNs;
  });
  for(uint32_t rank=0;rank<std::min(gpuScopeBucketCount,8u);++rank) {
    const auto& b=gpuScopeBuckets[order[rank]];const auto& m=b.metadata;
    char formats[128]{};size_t used=0;
    for(uint32_t i=0;i<m.colors.size();++i) if(m.colors[i]!=VK_FORMAT_UNDEFINED)
      used+=size_t(std::snprintf(formats+used,sizeof(formats)-used,"%s%u:%u",used?",":"",i,uint32_t(m.colors[i])));
    LOG("[vulkan GPU scope %u] %s %ux%ux%u; colors [%s] depth %u; mips %u layers %u aspects 0x%x depth-only %u; %.3f ms/frame %.3f mean %.3f max ms; %.2f scopes/frame %.2f draws/frame",
        rank+1,m.kind?"feedback":"render",m.extent.width,m.extent.height,m.extent.depth,
        formats,uint32_t(m.depth),m.mips,m.layers,m.aspects,m.depthOnly,
        b.intervalNs/1e6/frames,b.intervalNs/1e6/double(b.count),b.maxNs/1e6,
        b.count/frames,b.draws/frames);
  }
  gpuScopeBuckets={};gpuScopeBucketCount=0;gpuScopeTotals={};
}
static void report_gpu_timestamps() {
  static uint64_t previousFrame = 0;
  if (!R.gpuTimestampsEnabled || R.frame - previousFrame < 120) return;
  const auto& stats = R.gpuTimestampStats;
  const double frames = double(R.frame - previousFrame);
  LOG("[vulkan GPU timestamps] submission interval sum %.3f ms/frame; max %.3f ms/submission; %llu submissions; %llu unavailable; %llu zero; NOT GPU busy time%s",
      stats.intervalNs / 1e6 / frames, stats.maxIntervalNs / 1e6,
      (unsigned long long)stats.submissions, (unsigned long long)stats.unavailable,
      (unsigned long long)stats.zeroIntervals,
      stats.submissions && stats.zeroIntervals == stats.submissions
          ? "; all-zero/emulated timing UNUSABLE" : "");
  report_gpu_pass_timestamps(frames);
  R.gpuTimestampStats = {};
  previousFrame = R.frame;
}
VkCommandBuffer command_buffer() {
  fallback_stats::begin_frame(R.frame);
  if (handoff_trace::enabled()) handoff_trace::set_producer_frame(R.frame);
  if (!R.recording) {
    RendererCommands rendererCommands;  // the submission's timestamps open it, never eye 1's commands
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (auto* stream = record_worker_commands()) {
      if (!stream->beginCommandBuffer(R.cmd, bi.flags)) {
        drain_record_commands(fallback_stats::Boundary::EncoderFallback, 28, stream->failure_reason());
        vk_check(vkBeginCommandBuffer(R.cmd, &bi), "begin command buffer fallback");
      }
    } else vk_check(vkBeginCommandBuffer(R.cmd, &bi), "begin command buffer");
    R.recording = true;
    begin_overlay_gpu_queries(R.cmd, R.submissions[R.activeSubmission]);
    // dynamic resolution (dynres.h) times every command buffer on the GPU while it is on
    bool dynresTiming = false;
    if (dynres::on() && timestamp_capable(R.gpuTimestampValidBits, R.properties.limits.timestampPeriod)) {
      auto& slot = R.submissions[R.activeSubmission];
      if (!slot.timestampQueries) {
        VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = 2;
        if (vkCreateQueryPool(R.device, &info, nullptr, &slot.timestampQueries) != VK_SUCCESS) slot.timestampQueries = VK_NULL_HANDLE;
      }
      dynresTiming = slot.timestampQueries != VK_NULL_HANDLE;
      if (dynresTiming && !R.gpuTimestampsEnabled) {
        record::vkCmdResetQueryPool(R.cmd, slot.timestampQueries, 0, 2);
        record::vkCmdWriteTimestamp(R.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, slot.timestampQueries, 0);
        slot.timestampRecorded = true;
        slot.timestampFrame = R.frame;
      }
      slot.dynresTiming = dynresTiming;
    }
    if (R.gpuTimestampsEnabled) {
      auto& slot = R.submissions[R.activeSubmission];
      record::vkCmdResetQueryPool(R.cmd, slot.timestampQueries, 0,
          R.gpuPassTimestampsEnabled ? 2 + 2 * Renderer::Submission::maxGpuScopes : 2);
      record::vkCmdWriteTimestamp(R.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, slot.timestampQueries, 0);
      slot.timestampRecorded = true;
      slot.timestampFrame = R.frame;
    }
  }
  return R.cmd;
}
static GpuScopeToken begin_gpu_scope(const GpuScopeMetadata& metadata) {
  if(!R.gpuTimestampsEnabled || !R.gpuPassTimestampsEnabled) return {};
  if(R.rendering) { ++gpuScopeTotals.dropped;return {}; }
  const VkCommandBuffer cmd=command_buffer();
  auto& slot=R.submissions[R.activeSubmission];
  if(slot.gpuScopeCount==Renderer::Submission::maxGpuScopes) { ++gpuScopeTotals.dropped;return {}; }
  const uint32_t index=slot.gpuScopeCount++;
  slot.gpuScopes[index]={metadata,0,R.submissionGeneration,false};
  record::vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,slot.timestampQueries,2+2*index);
  return {R.submissionGeneration,index};
}
static void end_gpu_scope(GpuScopeToken token) {
  if(token.index==UINT32_MAX || token.generation!=R.submissionGeneration) return;
  auto& slot=R.submissions[R.activeSubmission];
  if(token.index>=slot.gpuScopeCount || slot.gpuScopes[token.index].ended ||
      slot.gpuScopes[token.index].generation!=token.generation) return;
  if(R.rendering) { ++gpuScopeTotals.dropped;return; }
  record::vkCmdWriteTimestamp(R.cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,slot.timestampQueries,3+2*token.index);
  slot.gpuScopes[token.index].ended=true;
}
void gpu_begin_render_scope(const std::array<Surface*,8>& colors,Surface* depth,
                            const uint32_t* slices,uint32_t depthSlice,uint32_t width,uint32_t height) {
  if(!R.gpuTimestampsEnabled || !R.gpuPassTimestampsEnabled) return;
  GpuScopeMetadata metadata;metadata.extent={width,height,1};metadata.depthOnly=true;
  Surface* target=depth;
  for(uint32_t i=0;i<colors.size();++i) if(colors[i]) {
    metadata.colors[i]=colors[i]->fmt.pixel;metadata.slices[i]=slices[i];
    ++metadata.colorCount;metadata.depthOnly=false;if(target==depth) target=colors[i];
  }
  if(depth) { metadata.depth=depth->fmt.pixel;metadata.depthSlice=depthSlice; }
  if(target) { metadata.mips=target->mips;metadata.layers=target->arrayLayers;metadata.aspects=target->aspect; }
  auto token=begin_gpu_scope(metadata);
  R.submissions[R.activeSubmission].activeRenderScope=token.index;
}
void gpu_count_render_draw() {
  if(!R.gpuTimestampsEnabled || !R.gpuPassTimestampsEnabled) return;
  auto& slot=R.submissions[R.activeSubmission];
  if(slot.activeRenderScope<slot.gpuScopeCount) {
    auto& scope=slot.gpuScopes[slot.activeRenderScope];
    if(scope.generation==R.submissionGeneration && !scope.ended) ++scope.draws;
  }
}
GpuScopeToken gpu_begin_feedback_scope(const Surface& source) {
  if(!R.gpuTimestampsEnabled || !R.gpuPassTimestampsEnabled) return {};
  GpuScopeMetadata metadata;metadata.kind=1;metadata.extent=source.extent;
  metadata.mips=source.mips;metadata.layers=source.arrayLayers;metadata.aspects=source.aspect;
  if(source.fmt.depth) { metadata.depth=source.fmt.pixel;metadata.depthOnly=true; }
  else { metadata.colors[0]=source.fmt.pixel;metadata.colorCount=1; }
  return begin_gpu_scope(metadata);
}
void gpu_end_feedback_scope(GpuScopeToken token) { end_gpu_scope(token); }
void end_encoder() {
  if (R.rendering) {
    end_pass(R.cmd);
    R.rendering = false;
    R.passTracked = false;
    if(R.gpuPassTimestampsEnabled) {
      auto& slot=R.submissions[R.activeSubmission];
      end_gpu_scope({R.submissionGeneration,slot.activeRenderScope});
      slot.activeRenderScope=UINT32_MAX;
    }
  }
}
bool narrow_barriers() {
  static const bool enabled = [] {
    const char* e = std::getenv("WWHD_VK_NARROW_BARRIERS");
    // Precise resource dependencies: opt-in everywhere (WWHD_VK_NARROW_BARRIERS=1). Measured on an
    // OnePlus 8 Pro (Adreno 650, Mesa Turnip, issue #104): no speed difference, but device-lost in 4 of
    // 10 runs with them on and 0 of 18 with them off or with Turnip's sysmem rendering; neutral on
    // MoltenVK. WWHD_VK_NARROW_BARRIERS=1|0 overrides the default.
    if (e && *e) return std::strcmp(e, "0") != 0;
    return false;
  }();
  return enabled;
}
void transition_buffer(Buffer& buffer, VkPipelineStageFlags stage, VkAccessFlags access) {
  auto d = derive_dependency(buffer.use, stage, access);
  if (!d.needed) return;
  end_encoder();
  VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  b.srcAccessMask = d.sourceAccess;
  b.dstAccessMask = access;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.buffer = buffer.buffer;
  b.size = VK_WHOLE_SIZE;
  record::vkCmdPipelineBarrier(command_buffer(), d.source, d.destination, 0, 0, nullptr, 1, &b, 0, nullptr);
}
void transition_image(Surface *s, VkImageLayout layout,
                      VkPipelineStageFlags stage, VkAccessFlags access) {
  const bool conservativePassBreak = s->layout != layout ||
      layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || access != VK_ACCESS_SHADER_READ_BIT;
  if (conservativePassBreak) stereo_release_texture(s);  // a texture both eyes sample
  if (narrow_barriers() && stage == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT &&
      access == (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT) &&
      layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    access = VK_ACCESS_SHADER_READ_BIT;
  }
  const bool changed = s->layout != layout;
  const auto dependency = derive_dependency(s->use, stage, access, changed);
  if (narrow_barriers()) {
    if (!dependency.needed) {
      // Barrier elision must not also change dynamic-rendering pass boundaries.
      if (conservativePassBreak) end_encoder();
      return;
    }
  } else if (!changed && layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
             access == VK_ACCESS_SHADER_READ_BIT) return;
  end_encoder();
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.oldLayout = s->layout;
  b.newLayout = layout;
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = s->image;
  b.subresourceRange = {s->aspect, 0, s->mips, 0, s->arrayLayers};
  b.srcAccessMask = narrow_barriers() ? dependency.sourceAccess :
      (s->layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
  b.dstAccessMask = access;
  record::vkCmdPipelineBarrier(command_buffer(), narrow_barriers() ? dependency.source :
      (s->layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT),
      stage, 0, 0, nullptr, 0, nullptr, 1, &b);
  s->layout = layout;
}
static VkResult initialize_submission_queries(Renderer::Submission& slot,bool passes) {
  VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  info.queryType=VK_QUERY_TYPE_TIMESTAMP;
  info.queryCount=passes ? 2+2*Renderer::Submission::maxGpuScopes : 2;
  return vkCreateQueryPool(R.device,&info,nullptr,&slot.timestampQueries);
}
bool begin_exact_preparation() {
  if (record_worker_active()) return false; // resolve pipeline handles on the producer; no lambda queue
  if(!active_deferred_commands)deferred_commands().start_capture();
  return active_deferred_commands!=nullptr;
}
static void destroy_unsubmitted_slot(Renderer::Submission& slot) {
  if(slot.timestampQueries) vkDestroyQueryPool(R.device,slot.timestampQueries,nullptr);
  const auto destroyDescriptors=reinterpret_cast<PFN_vkDestroyDescriptorPool>(vkGetDeviceProcAddr(R.device,"vkDestroyDescriptorPool"));
  const auto destroyFence=reinterpret_cast<PFN_vkDestroyFence>(vkGetDeviceProcAddr(R.device,"vkDestroyFence"));
  const auto destroyCommands=reinterpret_cast<PFN_vkDestroyCommandPool>(vkGetDeviceProcAddr(R.device,"vkDestroyCommandPool"));
  if(slot.descriptorPool && destroyDescriptors) destroyDescriptors(R.device,slot.descriptorPool,nullptr);
  if(slot.fence && destroyFence) destroyFence(R.device,slot.fence,nullptr);
  if(slot.commandPool && destroyCommands) destroyCommands(R.device,slot.commandPool,nullptr);
}
static VkResult try_initialize_submission(Renderer::Submission& slot) {
  VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool.queueFamilyIndex=R.queueFamily;
  pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  VkResult result=vkCreateCommandPool(R.device,&pool,nullptr,&slot.commandPool);
  if(result!=VK_SUCCESS)return result;
  VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ca.commandPool=slot.commandPool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;
  result=vkAllocateCommandBuffers(R.device,&ca,&slot.cmd);
  if(result!=VK_SUCCESS)return result;
  VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};fi.flags=VK_FENCE_CREATE_SIGNALED_BIT;
  result=vkCreateFence(R.device,&fi,nullptr,&slot.fence);
  if(result!=VK_SUCCESS)return result;
  VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,65536},
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,32768},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,32768},
    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,65536}};
  VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dp.maxSets=32768;dp.poolSizeCount=std::size(sizes);dp.pPoolSizes=sizes;
  return vkCreateDescriptorPool(R.device,&dp,nullptr,&slot.descriptorPool);
}
void initialize_submission(Renderer::Submission& slot) {
  vk_check(try_initialize_submission(slot),"initialize submission slot");
}
static void wait_record_submission(Renderer::Submission& slot) {
  // A still-signaled old fence is not evidence for a queued worker submission.
  // Wait until the CPU worker actually ended/reset/submitted this exact slot.
  acknowledge_submission_cpu(slot.recordWorkerSerial, [&](uint64_t serial) {
    if (handoff_trace::enabled()) handoff_trace::emit(handoff_trace::Lane::Producer, handoff_trace::Kind::DrainBegin, slot.recordWorkerSerial, R.frame, uint64_t(handoff_trace::Reason::CpuSubmissionAck), slot.serial);
    wait_record_commands(serial, handoff_trace::Reason::CpuSubmissionAck);
    if (handoff_trace::enabled()) handoff_trace::emit(handoff_trace::Lane::Producer, handoff_trace::Kind::DrainEnd, slot.recordWorkerSerial, R.frame, uint64_t(handoff_trace::Reason::CpuSubmissionAck), slot.serial);
  });
}
static bool try_record_submission(Renderer::Submission& slot) {
  if (!record_commands_emitted(slot.recordWorkerSerial)) return false;
  slot.recordWorkerSerial = 0;
  return true;
}
static void resolve_retained_submission(Renderer::Submission& slot) {
  wait_record_submission(slot);
  if(slot.retainedUnsubmitted) {
    if(!slot.deferredOwner)throw std::logic_error("retained submission has no command owner");
    slot.deferredOwner->finish_exact();
    if(slot.retainedUnsubmitted)throw std::logic_error("submission boundary was not emitted");
  }
}
static void cleanup_submission(Renderer::Submission& slot) {
  for (auto& complete : slot.completions) complete();
  slot.completions.clear();
  // The submit fence has completed; slices can now be overwritten safely.
  for (auto& block : slot.uploadBlocks) block.used = 0;
  for (auto b : slot.garbageBuffers) {
    if (b.mapped)
      vkUnmapMemory(R.device, b.memory);
    vkDestroyBuffer(R.device, b.buffer, nullptr);
    vkFreeMemory(R.device, b.memory, nullptr);
  }
  slot.garbageBuffers.clear();
  buffer_cache_free(slot.garbageCacheRegions);
  slot.garbageCacheRegions.clear();
  for (auto framebuffer : slot.garbageFramebuffers) vkDestroyFramebuffer(R.device, framebuffer, nullptr);
  slot.garbageFramebuffers.clear();
  for (auto &i : slot.garbageImages) {
    for (auto v : i.views)
      if (v)
        vkDestroyImageView(R.device, v, nullptr);
    if (i.image)
      vkDestroyImage(R.device, i.image, nullptr);
    if (i.memory)
      vkFreeMemory(R.device, i.memory, nullptr);
  }
  slot.garbageImages.clear();
  vk_check(vkResetCommandPool(R.device, slot.commandPool, 0),
           "reset command pool");
  vk_check(vkResetDescriptorPool(R.device, slot.descriptorPool, 0),
           "reset descriptor pool");
  ++R.submissionGeneration;
}
static void retire_submission(Renderer::Submission& slot, WaitTiming& timing=submitWait) {
  resolve_retained_submission(slot);
  if (!slot.pending) return;
  VkResult status=timed_call(fenceStatusTiming,[&] { return vkGetFenceStatus(R.device,slot.fence); });
  if (status==VK_NOT_READY) {
    vk_check(timed_call(timing,[&] {
      // Submission serial identifies the fence; deque slots have no contiguous index.
      if (handoff_trace::enabled()) handoff_trace::emit(handoff_trace::Lane::Producer, handoff_trace::Kind::FenceWaitBegin, 0, R.frame, slot.serial, 0);
      const auto result = vkWaitForFences(R.device,1,&slot.fence,VK_TRUE,UINT64_MAX);
      if (handoff_trace::enabled()) handoff_trace::emit(handoff_trace::Lane::Producer, handoff_trace::Kind::FenceWaitEnd, 0, R.frame, slot.serial, uint64_t(int64_t(result)));
      return result;
    },rprof::kWaitGpu),"wait submission retirement");
  } else vk_check(status,"submission fence status");
  timed_call(retirementCleanupTiming,[&] {
    collect_gpu_timestamp_queries(slot);
    if(R.gpuPassTimestampsEnabled) collect_gpu_pass_queries(slot);
    cleanup_submission(slot);
    return VK_SUCCESS;
  });
  slot.pending=false;
}
static void activate_submission(size_t index) {
  auto& slot=R.submissions[index];
  retire_submission(slot,slotReuseWait);
  R.activeSubmission=index;
  slot.gpuScopeCount=0;slot.activeRenderScope=UINT32_MAX;
  ++R.submissionGeneration;
  R.commandPool=slot.commandPool;R.cmd=slot.cmd;R.fence=slot.fence;
  R.descriptorPool=slot.descriptorPool;
  R.uploadBlocks=std::move(slot.uploadBlocks);
  R.textureStagingBytes=0;
  R.garbageBuffers=std::move(slot.garbageBuffers);
  R.garbageImages=std::move(slot.garbageImages);
  R.garbageFramebuffers=std::move(slot.garbageFramebuffers);
  R.garbageCacheRegions=std::move(slot.garbageCacheRegions);
  R.recording=false;R.rendering=false;R.passTracked=false;
}
// depth peeks (depth_peek.h): the submission that carries the latest peek copy; retired on demand
static uint64_t peekSubmission = 0, lastSubmissionSerial = 0;
static void drain_submissions() {
  if(active_deferred_commands)active_deferred_commands->finish_exact();
  for(auto& slot:R.submissions)resolve_retained_submission(slot);
  // A fence signal covers earlier submissions on this same graphics queue.
  // Wait once at the newest pending fence rather than waking at each older
  // fence in ring-array order. Keep each slot's status check and fenced cleanup.
  Renderer::Submission* newest=nullptr;
  for (auto& slot:R.submissions)
    if (slot.pending && (!newest || slot.serial>newest->serial)) newest=&slot;
  if (newest) retire_submission(*newest);
  for (auto& slot:R.submissions) retire_submission(slot);
}
// Overflow is allocated only when a captured tail prevents ring reuse. Slot
// addresses and screenshot indices stay stable; the total is strictly bounded.
static size_t next_submission_slot() {
  const auto choice=subpool::next_slot(R.activeSubmission,R.submissions.size(),
      R.submissions[(R.activeSubmission+1)%R.submissions.size()].retainedUnsubmitted);
  const size_t next=(R.activeSubmission+1)%R.submissions.size();
  if(choice.step==subpool::Step::Reuse)return choice.index;
  if(choice.step==subpool::Step::Grow) {
    bool appended=false;
    try {
      R.submissions.emplace_back();appended=true;
      auto& slot=R.submissions.back();
      VkResult result=try_initialize_submission(slot);
      if(result==VK_SUCCESS && R.gpuTimestampsEnabled)
        result=initialize_submission_queries(slot,R.gpuPassTimestampsEnabled);
      if(result==VK_SUCCESS)return R.submissions.size()-1;
      destroy_unsubmitted_slot(slot);R.submissions.pop_back();appended=false;
      LOG("[vulkan deferred] overflow slot unavailable (%d); resolving Exact",int(result));
    } catch(const std::bad_alloc&) {
      if(appended) { destroy_unsubmitted_slot(R.submissions.back());R.submissions.pop_back(); }
    }
  }
  resolve_retained_submission(R.submissions[next]);
  return next;
}
// Resolved once from the selected driver, only when the worker is requested.
// Unknown drivers conservatively retain producer presentation on every OS.
static bool nativeWorkerPresentation = false;
static void publish_or_finish_present_prefix(DeferredCommands& owner) {
  if (nativeWorkerPresentation && record_worker_active()) {
    if (publish_record_commands()) fallback_stats::native_prefix_publication();
  } else owner.finish_exact();
}
static void submit(VkSemaphore wait = VK_NULL_HANDLE,
                   VkSemaphore signal = VK_NULL_HANDLE, bool asynchronous=false) {
  if (!R.recording)return;
  stereo_flush(); // stereo eye 1's held commands belong to this submission
  RendererCommands rendererCommands;
  // Presentation owns externally synchronized semaphores: finish the entire
  // ordered CPU tail before handing those semaphores to Vulkan.
  bool externalSync=wait || signal;
#ifdef __ANDROID__
  externalSync=externalSync || secondarySnapshotWait || secondarySnapshotSignal;
#endif
  if(externalSync && active_deferred_commands) {
    if (handoff_trace::enabled()) handoff_trace::emit(handoff_trace::Lane::Producer, handoff_trace::Kind::DrainBegin, 0, R.frame, uint64_t(handoff_trace::Reason::ExternalSyncPrefix));
    publish_or_finish_present_prefix(*active_deferred_commands);
    if (handoff_trace::enabled()) handoff_trace::emit(handoff_trace::Lane::Producer, handoff_trace::Kind::DrainEnd, 0, R.frame, uint64_t(handoff_trace::Reason::ExternalSyncPrefix));
  }
  end_encoder();
  auto& slot=R.submissions[R.activeSubmission];
  if (slot.perfQuery != UINT32_MAX) {
    auto& q = overlayGpuQueries[slot.perfQuery];
    record::vkCmdWriteTimestamp(R.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, q.pool, 1);
    perf::timestamp_write();slot.perfQuery=UINT32_MAX;
  }
  if (slot.timestampRecorded)
    record::vkCmdWriteTimestamp(R.cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,slot.timestampQueries,1);
  auto* owner=active_deferred_commands;
  static uint64_t nextSubmissionSerial=0;
  const auto seal=[&slot,owner]() noexcept {
    slot.serial=++nextSubmissionSerial;
    slot.recordedFrame=R.frame;
    lastSubmissionSerial=slot.serial;
    slot.uploadBlocks=std::move(R.uploadBlocks);
    slot.completions=std::move(R.completions);
    slot.garbageBuffers=std::move(R.garbageBuffers);
    slot.garbageImages=std::move(R.garbageImages);
    slot.garbageFramebuffers=std::move(R.garbageFramebuffers);
    slot.garbageCacheRegions=std::move(R.garbageCacheRegions);
    slot.retainedUnsubmitted=true;slot.deferredOwner=owner;
    R.recording=false;
  };
  const size_t slotIndex=R.activeSubmission;
#ifdef __ANDROID__
  const auto snapshotWait=secondarySnapshotWait,snapshotSignal=secondarySnapshotSignal;
  secondarySnapshotWait=secondarySnapshotSignal=VK_NULL_HANDLE;
#endif
  const auto execute=[&slot,wait,signal,slotIndex
#ifdef __ANDROID__
    ,snapshotWait,snapshotSignal
#endif
  ] {
    // Use the sealed slot, never the aliases of a newer active recording.
    vk_check(timed_call(commandEndTiming,[&]{return vkEndCommandBuffer(slot.cmd);}),"end command buffer");
    vk_check(timed_call(fenceResetTiming,[&]{return vkResetFences(R.device,1,&slot.fence);}),"reset fence");
    std::array<VkPipelineStageFlags,2> stages{VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT};
    std::array<VkSemaphore,2> waits{},signals{};
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount=1;si.pCommandBuffers=&slot.cmd;
    if(wait)waits[si.waitSemaphoreCount++]=wait;
    if(signal)signals[si.signalSemaphoreCount++]=signal;
#ifdef __ANDROID__
    if(snapshotWait)waits[si.waitSemaphoreCount++]=snapshotWait;
    if(snapshotSignal)signals[si.signalSemaphoreCount++]=snapshotSignal;
#endif
    si.pWaitSemaphores=waits.data();si.pWaitDstStageMask=stages.data();si.pSignalSemaphores=signals.data();
    vk_check(timed_call(queueSubmitTiming,[&]{return vkQueueSubmit(R.queue,1,&si,slot.fence);}),"submit graphics");
#ifdef __ANDROID__
    if(snapshotSignal && isolatedSecondary && isolatedSecondary->prepared()) {
      isolatedSecondary->primary_submitted();
      isolatedSnapshotSubmission=std::pair{slotIndex,slot.serial};
    }
#endif
    slot.pending=true;slot.retainedUnsubmitted=false;slot.deferredOwner=nullptr;
  };
  if (record_worker_active()) {
    if (producer_presentation_recording()) {
      // The scope drained every earlier worker prefix. No worker may touch this
      // command buffer or queue until the producer's ordered present is complete.
      seal();
      slot.recordWorkerSerial = 0;
      execute(); // same end/reset/submit, semaphore and snapshot bookkeeping
    } else {
      seal();
      // Only opaque Vulkan handles and copied POD arguments cross threads. All
      // Renderer aliases, layouts, cache state and slot flags stay producer-owned.
      slot.recordWorkerSerial = submit_record_commands(R.device, R.queue, slot.cmd,
          slot.fence, wait, signal
  #ifdef __ANDROID__
          , snapshotWait, snapshotSignal
  #endif
      );
      slot.pending = true; slot.retainedUnsubmitted = false; slot.deferredOwner = nullptr;
      // Presentation/isolated DRC handshakes share queue/semaphore host lifetime.
      // Publish them only after the worker has actually submitted the batch.
      if (externalSync) resolve_retained_submission(slot);
  #ifdef __ANDROID__
      if (snapshotSignal && isolatedSecondary && isolatedSecondary->prepared()) {
        isolatedSecondary->primary_submitted();
        isolatedSnapshotSubmission = std::pair{slotIndex, slot.serial};
      }
  #endif
    }
  } else if(owner) owner->owned_boundary(seal,execute);
  else { seal();execute(); }
  if(asynchronous)activate_submission(next_submission_slot());
  else {
    retire_submission(slot,wait ? presentSubmitWait : submitWait);
    activate_submission(R.activeSubmission);
  }
}
unsigned submission_pool_slots() { return submissionPoolSlots; }
void peek_submitted() { peekSubmission = lastSubmissionSerial; }
void peek_retire() {
  if (!peekSubmission) return;
  // submissions sealed but still held by the ordered command stream are submitted first (as in
  // drain_submissions), then retired oldest first up to the peek's (its fence covers the earlier ones)
  for (auto& slot : R.submissions)
    if (slot.serial <= peekSubmission) resolve_retained_submission(slot);
  for (;;) {
    Renderer::Submission* oldest = nullptr;
    for (auto& slot : R.submissions)
      if (slot.pending && slot.serial <= peekSubmission && (!oldest || slot.serial < oldest->serial)) oldest = &slot;
    if (!oldest) break;
    retire_submission(*oldest);
  }
  peekSubmission = 0;
}
void flush_async() {
  // Deferred objects may reference earlier queued work even if this slot has
  // no draw commands. Submit an empty command buffer to retire them in order.
  if (!R.recording && (!R.garbageBuffers.empty() || !R.garbageImages.empty() || !R.garbageFramebuffers.empty() ||
                       !R.garbageCacheRegions.empty())) command_buffer();
  submit(VK_NULL_HANDLE,VK_NULL_HANDLE,true);
}
void flush() {
  if(active_deferred_commands)active_deferred_commands->finish_exact();
  if (!R.recording && (!R.garbageBuffers.empty() || !R.garbageImages.empty() || !R.garbageFramebuffers.empty() ||
                       !R.garbageCacheRegions.empty())) command_buffer();
  submit();
  drain_submissions();
}
#ifdef __ANDROID__
static void wait_secondary_present();
#endif
// ---------------------------------------------------------------- even frame delivery
// Android by default, WWHD_INTERP_EVEN_PACING=0/1 (interp_pacing.h EvenSchedule): the GX2 swap carries the frame's
// intended distance to the previous one (set_present_interval); present() submits the TV frame's
// work as always but holds the present call of a frame that is ready before its slot. The render
// thread goes on with the next frame's commands and presents the held frame at an op boundary or
// while it waits for commands (gx2_core.cpp), at the latest when the next frame presents or the
// swapchain changes. frames_completed() advances as before, so the game never waits for a held frame.
// WWHD_VK_PRESENT_TRACE=file writes a line per TV frame of a swap: frame, in-between frame (1),
// interval, ready, planned and actual present call (steady clock, ns).
struct HeldPresent {
  bool pending = false;
  int64_t due = 0;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  uint32_t index = 0;
  VkSemaphore wait = VK_NULL_HANDLE;
  uint32_t observedPresentId = 0;
  uint64_t frame = 0, generation = 0;
};
static HeldPresent heldPresent;
static interp::pacing::EvenSchedule evenSchedule;
static uint32_t evenPresentInterval = 0;  // of the frame being swapped (ns; 0: no schedule)
static bool evenPresentHold = false, evenPresentSwap = false;
static uint64_t evenFrames = 0, evenHeldPresents = 0, evenForcedPresents = 0, evenHeldNs = 0;
static int64_t even_now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
static FILE* even_trace_file() {
  static FILE* out = getenv("WWHD_VK_PRESENT_TRACE") ? fopen(getenv("WWHD_VK_PRESENT_TRACE"), "w") : nullptr;
  return out;
}
struct EvenTraceLine { uint64_t frame; int hold; uint32_t interval; int64_t ready, due; };
static EvenTraceLine evenTracePending{};
static void trace_even_present(uint64_t frame, bool hold, uint32_t interval, int64_t ready, int64_t due) {
  if (!even_trace_file()) return;
  evenTracePending = {frame, hold ? 1 : 0, interval, ready, due};
}
static void trace_even_presented(int64_t at) {
  FILE* out = even_trace_file();
  if (!out || !evenTracePending.ready) return;
  const auto& l = evenTracePending;
  fprintf(out, "%llu %d %u %lld %lld %lld\n", (unsigned long long)l.frame, l.hold, l.interval, (long long)l.ready,
          (long long)l.due, (long long)at);
  evenTracePending = {};
}
static void present_held(bool forced);
static bool async_present();
static VkResult device_wait_idle() {
  present_held(true);
  drain_record_commands();
#ifdef __ANDROID__
  wait_secondary_present(); // synchronize host access to the dedicated queue
#endif
  return vkDeviceWaitIdle(R.device);
}
void wait_idle() {
  flush();
  vk_check(device_wait_idle(), "device idle");
}
void with_autorelease_pool(void (*fn)()) { host::with_autorelease_pool(fn); }
uint64_t frames_completed() { return R.completed.load(); }
void set_present_interval(uint32_t ns, bool hold) {
  // tuning aid: WWHD_INTERP_EVEN_PACING_LEAK=n sets the schedule's leak to 1/n of the interval
  static const bool leakSet = [] {
    if (const char* e = getenv("WWHD_INTERP_EVEN_PACING_LEAK")) evenSchedule.leakDivisor = std::max(1, atoi(e));
    return true;
  }();
  (void)leakSet;
  evenPresentInterval = async_present() ? ns : 0;  // (synchronous presentation waits for the GPU anyway)
  evenPresentHold = hold;
  if (!evenPresentInterval) evenSchedule.reset();
}
int64_t present_due() { return heldPresent.pending ? heldPresent.due : 0; }
void service_present() {
  if (heldPresent.pending && even_now_ns() >= heldPresent.due) present_held(false);
}
uint64_t frame_count() { return std::atomic_ref<uint64_t>(R.frame).load(); }
static bool has_extension(const std::vector<VkExtensionProperties> &es,
                          const char *name) {
  return std::any_of(es.begin(), es.end(),
                     [&](auto &e) { return !strcmp(e.extensionName, name); });
}
#ifdef __ANDROID__
static void retire_secondary_swapchain(bool surface);
static bool secondary_retirement_available();
#endif
static bool async_present();
static void make_swapchain(Screen &s) {
  if (&s == &R.tv) present_held(true);  // even frame delivery: a held frame goes out first
  if(active_deferred_commands)active_deferred_commands->finish_exact();
#ifdef __ANDROID__
  if (&s == &R.tv) {
    // Primary views only reference graphics-queue work. Do not synchronize
    // the separate WSI queue just to resize the main display.
    drain_record_commands();
  vk_check(vkQueueWaitIdle(R.queue), "primary resize graphics idle");
  } else if (!R.secondaryPresentFences) {
    ++R.secondaryIdleWaits;
    vk_check(device_wait_idle(), "resize device idle");
  }
#else
  vk_check(device_wait_idle(), "resize device idle");
#endif
#ifdef __ANDROID__
  if(&s==&R.tv && R.primaryInjectQueryLoss.exchange(false))
    vk_check(VK_ERROR_SURFACE_LOST_KHR,"authored primary surface capability loss");
  if(&s==&R.drc && R.secondaryInjectQueryLoss.exchange(false))
    vk_check(VK_ERROR_SURFACE_LOST_KHR,"authored secondary surface capability loss");
#endif
  VkSurfaceCapabilitiesKHR caps;
  vk_check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(R.physicalDevice,
                                                     s.surface, &caps),
           "surface capabilities");
  uint32_t n = 0;
  vk_check(vkGetPhysicalDeviceSurfaceFormatsKHR(R.physicalDevice, s.surface, &n,
                                       nullptr),"surface format count");
  std::vector<VkSurfaceFormatKHR> fs(n);
  const VkResult formats=vkGetPhysicalDeviceSurfaceFormatsKHR(R.physicalDevice,s.surface,&n,fs.data());
  if(formats!=VK_INCOMPLETE)vk_check(formats,"surface formats");
  fs.resize(n);
  if (fs.empty())
    exception_report::raise("No presentation formats");
  bool wantHdr=false;
  if(&s==&R.tv) {
    bool supported=false;
    for(const auto& f:fs)if(f.format==VK_FORMAT_R16G16B16A16_SFLOAT &&
        f.colorSpace==VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT) {
      VkFormatProperties props;vkGetPhysicalDeviceFormatProperties(R.physicalDevice,f.format,&props);
      supported=(caps.supportedUsageFlags&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&
        (props.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) &&
        (props.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT);
    }
    hdr::formatSupported=supported;
#if defined(_WIN32) && defined(WWHD_SDL_HOST)
    hdr::displayEnabled=SDL_GetBooleanProperty(SDL_GetWindowProperties(s.window),SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN,false);
#endif
    wantHdr=hdr::resolve_output();
  }
  // Presentation uses image blits, so both advertised surface support and
  // optimal-tiling BLIT_DST format support must hold.
  if (fs.size() == 1 && fs[0].format == VK_FORMAT_UNDEFINED)
    fs[0].format = VK_FORMAT_B8G8R8A8_SRGB;
  VkSurfaceFormatKHR format{};
  bool found = false;
#ifdef WWHD_SDL_HOST
  const VkFormat preferred = VK_FORMAT_B8G8R8A8_SRGB;
#else
  // AppKit windows: like the Metal layer, an sRGB drawable only for an sRGB scan buffer (the
  // composition, overlay blending included, then matches the Metal renderer's)
  const VkFormat preferred = s.scanSrgb ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM;
#endif
  for (auto f : fs) {
    if(wantHdr) {
      if(f.format!=VK_FORMAT_R16G16B16A16_SFLOAT || f.colorSpace!=VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT)continue;
    } else if(f.colorSpace!=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)continue;
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(R.physicalDevice, f.format, &props);
    if (!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
      continue;
    if (!found || f.format == preferred) {
      format = f;
      found = true;
    }
    if (f.format == preferred &&
        f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
      break;
  }
  if (!found)
    exception_report::raise("No swapchain format supports image blits");
  VkExtent2D extent = caps.currentExtent;
  if (extent.width == UINT32_MAX)
    extent = {std::clamp<uint32_t>(std::max(s.width.load(), 1),
                                   caps.minImageExtent.width,
                                   caps.maxImageExtent.width),
              std::clamp<uint32_t>(std::max(s.height.load(), 1),
                                   caps.minImageExtent.height,
                                   caps.maxImageExtent.height)};
#ifdef __ANDROID__
  // Phones report the extent in the display's natural (portrait) orientation together with a 90°
  // or 270° current transform. The pictures are laid out for the landscape window, so the
  // swapchain gets the window's own size and an identity transform: the compositor rotates it.
  // (The window can also still be portrait while the activity turns to landscape: its size
  // changes then and the swapchain is made again for the new size.)
  {
    int pw = 0, ph = 0;
    if (&s == &R.tv && quest_theater::active())
      extent = {quest_theater::width,quest_theater::height};
    else if (s.window && SDL_GetWindowSizeInPixels(s.window, &pw, &ph) && pw > 0 && ph > 0)
      extent = {uint32_t(pw), uint32_t(ph)};
    else if (&s == &R.drc && s.width > 0 && s.height > 0)
      extent = {uint32_t(s.width.load()), uint32_t(s.height.load())};
    else if (caps.currentTransform & (VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR |
                                      VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR))
      std::swap(extent.width, extent.height);
  }
  const VkSurfaceTransformFlagBitsKHR transform =
      (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
          ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;
  LOG("[vulkan] swapchain %ux%u (surface reports %ux%u, transform %u)", extent.width, extent.height,
      caps.currentExtent.width, caps.currentExtent.height, unsigned(caps.currentTransform));
#else
  const VkSurfaceTransformFlagBitsKHR transform = caps.currentTransform;
#endif
  if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
    exception_report::raise("Swapchain cannot receive scan-buffer blits");
  VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  ci.surface = s.surface;
  ci.minImageCount = std::max(caps.minImageCount, 2u);
  if (caps.maxImageCount)
    ci.minImageCount = std::min(ci.minImageCount, caps.maxImageCount);
  ci.imageFormat = format.format;
  ci.imageColorSpace = format.colorSpace;
  ci.imageExtent = extent;
  ci.imageArrayLayers = 1;
  VkFormatProperties presentationFormat;
  vkGetPhysicalDeviceFormatProperties(R.physicalDevice,format.format,&presentationFormat);
  const bool shaderPresentation = (caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&
      (presentationFormat.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
  const bool captureTransfer = present_capture_requested() &&
      (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) &&
      (presentationFormat.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT);
  ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
      (shaderPresentation ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0) |
      (captureTransfer ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
  ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
#ifdef __ANDROID__
  // Captures/the explicit waiting path draw on the primary queue, while the
  // ordinary worker draws on its own family. Both may access these images.
  const uint32_t presentationFamilies[]={R.queueFamily,R.secondaryQueueFamily};
  if(&s==&R.drc && R.secondaryQueue && R.secondaryQueueFamily!=R.queueFamily) {
    ci.imageSharingMode=VK_SHARING_MODE_CONCURRENT;
    ci.queueFamilyIndexCount=2;
    ci.pQueueFamilyIndices=presentationFamilies;
  }
#endif
  ci.preTransform = transform;
  ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  for (auto flag : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                    VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                    VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                    VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
    if (caps.supportedCompositeAlpha & flag) {
      ci.compositeAlpha = flag;
      break;
    }
  uint32_t modeCount = 0;
  vk_check(vkGetPhysicalDeviceSurfacePresentModesKHR(R.physicalDevice,
      s.surface, &modeCount, nullptr), "presentation mode count");
  std::vector<VkPresentModeKHR> modes(modeCount);
  vk_check(vkGetPhysicalDeviceSurfacePresentModesKHR(R.physicalDevice,
      s.surface, &modeCount, modes.data()), "presentation modes");
  // Presentation (Graphics > Presentation in the settings overlay, WWHD_VK_PRESENT_MODE): FIFO (vsync)
  // by default; mailbox (low latency) or immediate (may tear) when chosen and the surface offers them.
  // Guest GX2 pacing still controls game flips in every mode.
  static const VkPresentModeKHR kModes[kPresentModes] = {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR,
                                                         VK_PRESENT_MODE_IMMEDIATE_KHR};
  unsigned offered = 0;
  std::string offeredNames;
  for (int m = 0; m < kPresentModes; m++)
    if (std::find(modes.begin(), modes.end(), kModes[m]) != modes.end()) {
      offered |= 1u << m;
      offeredNames += std::string(offeredNames.empty() ? "" : ", ") + present_mode_name(m);
    }
  if (&s == &R.tv) set_present_modes_offered(offered);
  const int wanted = effective_present_mode();
  const int chosen = offered >> wanted & 1 ? wanted : kPresentFifo;
  ci.presentMode = kModes[chosen];
  if (chosen != s.presentMode || wanted != s.presentWanted)
    LOG("[vulkan] %s present mode %s (available: %s)%s", &s == &R.tv ? "TV" : "GamePad", present_mode_name(chosen),
        offeredNames.c_str(), chosen != wanted ? " - the requested mode is not offered" : "");
  s.presentMode = chosen;
  s.presentWanted = wanted;
  // frame interpolation caps 120/240 fps to the display's refresh rate only when presenting waits
  // for it (interp::output_fps)
  if (&s == &R.tv) interp::set_present_vsync(chosen == kPresentFifo);
  ci.clipped = VK_TRUE;
  if (presentObservation && &s == &R.tv && s.swapchain) {
    drain_observed_history();
    pendingObservedPresents.clear();
  }
  ci.oldSwapchain = s.swapchain;
  VkSwapchainKHR sc;
  vk_check(vkCreateSwapchainKHR(R.device, &ci, nullptr, &sc),
           "create swapchain");
#ifdef __ANDROID__
  if (&s == &R.drc && R.secondaryPresentFences) retire_secondary_swapchain(false);
  else
#endif
  {
    reset_present_screen(s); // The compatibility path drained the device above.
    if (s.swapchain) vkDestroySwapchainKHR(R.device, s.swapchain, nullptr);
  }
  s.swapchain = sc;
  ++s.swapchainGeneration;
  s.swapFormat = format.format;
  s.hdrOutput = wantHdr;
  if(&s==&R.tv)LOG("[HDR] output %s; scRGB format %s; Windows HDR %s",wantHdr?"scRGB FP16":"SDR",hdr::formatSupported.load()?"supported":"unavailable",hdr::displayEnabled.load()?"enabled":"disabled");
  s.swapExtent = extent;
  vkGetSwapchainImagesKHR(R.device, sc, &n, nullptr);
  s.images.resize(n);
  vkGetSwapchainImagesKHR(R.device, sc, &n, s.images.data());
  if (presentObservation && &s == &R.tv)
    present_observer::presentation_topology(s.swapchainGeneration, n, async_present() ? 3u : 1u,
        uint32_t(R.submissions.size()), uint32_t(ci.presentMode), async_present());
  s.layouts.assign(n, VK_IMAGE_LAYOUT_UNDEFINED);
  s.imageUses.assign(n, {});
  prepare_present_screen(s,shaderPresentation,captureTransfer);
  s.resize = false;
}
#ifdef __ANDROID__
// Android destroys an app's surface when it goes to the background (Home, another app) and gives
// it a new one when it comes back. Meanwhile nothing is presented (the game keeps running); then
// the Vulkan surface and swapchain are created again for the window's new native surface.
static std::atomic<bool> surfaceLost{false}, surfaceRecreate{false};
static void recover_primary_surface() {
  ++R.primarySurfaceLosses;
  surfaceLost = true;
  surfaceRecreate = true;
}

static void recreate_surface(Screen &s) {
  if (&s == &R.tv) present_held(true);
  if(active_deferred_commands)active_deferred_commands->finish_exact();
  if (!SDL_GetPointerProperty(SDL_GetWindowProperties(s.window),
                              SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr)) {
    surfaceRecreate = true;  // the new native surface is not there yet: next frame
    return;
  }
  drain_record_commands();
  vk_check(vkQueueWaitIdle(R.queue), "primary surface recreation graphics idle");
  reset_present_screen(s);
  if (s.swapchain)
    vkDestroySwapchainKHR(R.device, s.swapchain, nullptr);
  s.swapchain = VK_NULL_HANDLE;
  if (s.surface)
    vkDestroySurfaceKHR(R.instance, s.surface, nullptr);
  s.surface = VK_NULL_HANDLE;
  if (!WWHD_CREATE_WINDOW_SURFACE(s.window, R.instance, nullptr, &s.surface)) {
    LOG("[vulkan] surface recreation: %s (retrying)", SDL_GetError());
    s.surface = VK_NULL_HANDLE;
    surfaceRecreate = true;
    return;
  }
  surfaceLost = false;
  display_rate::surface_changed();
  s.resize = true;
  LOG("[vulkan] presentation surface recreated");
}
// App lifecycle: called by SDL as the events happen, on the thread that sends them; SDL's
// documentation asks for an event watch here (the queue is not read while the app is paused).
static bool SDLCALL lifecycle_watch(void *, SDL_Event *event) {
  if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND) {
    try { gx2::request_background_vulkan_cache_checkpoint(); }
    catch (const std::exception& error) { LOG("[vulkan cache] background request skipped: %s", error.what()); }
    // the "Last session" state while the game still runs and presents (Android may end a background
    // app without notice); this thread is the app's UI thread, so the wait is kept short
    ss::save_last_session(1000);
  }
  if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND || event->type == SDL_EVENT_DID_ENTER_BACKGROUND) {
    if (!surfaceLost.exchange(true))
      LOG("[vulkan] app in the background: presentation paused");
  } else if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND) {
    LOG("[vulkan] app in the foreground: new presentation surface");
    // (the window may have reported itself minimized meanwhile); a hidden run (setup's models step under its
    // progress screen) stays unpresented (gfx/hidden_presentation.h)
    R.tv.visible = gfx::hidden::mode().presents();
    surfaceRecreate = true;
  }
  return true;
}
#endif
// Asynchronous presentation (the default on every platform since 2026-10-07; WWHD_VK_ASYNC_PRESENT=0
// restores the waiting path): the presentation submission goes into the submission ring (submission_pool.h) like GX2Flush
// work instead of waiting for the GPU, and swap() does not drain the queue, so the render thread
// records frame N+1 while the GPU draws frame N. Each frame in flight has its own acquire semaphore
// (reused only after the submission that waited on it retired) and each swapchain image its own
// render-finished semaphore. Captures and frame dumps keep the waiting path.
static bool async_present() {
  static const bool on = [] {
    const char *e = std::getenv("WWHD_VK_ASYNC_PRESENT");
    return !e || std::atoi(e) != 0;
  }();
  return on;
}
struct AsyncPresentState {
  std::array<VkSemaphore, 3> acquire{};
  std::array<uint64_t, 3> serial{};  // submission that waited on acquire[k]
  std::array<size_t, 3> slot{};
  unsigned next = 0;
  std::vector<VkSemaphore> finished;  // per swapchain image
};
static AsyncPresentState asyncPresent[2];  // TV, GamePad
static VkSemaphore new_semaphore() {
  VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkSemaphore sem;
  vk_check(vkCreateSemaphore(R.device, &si, nullptr, &sem), "create presentation semaphore");
  return sem;
}
#ifdef __ANDROID__
namespace {
std::mutex secondaryMutex;
ANativeWindow *pendingSecondary = nullptr, *secondaryWindow = nullptr;
bool secondaryChanged = false;
int secondaryWidth = 0, secondaryHeight = 0;
int64_t secondaryGeneration = -1;
int64_t activeSecondaryGeneration = -1;
gamepad_touch::Viewport secondaryTouchBox;
gamepad_touch::Viewport primaryTouchBox;
struct SecondaryPresentFence { VkFence fence=VK_NULL_HANDLE; bool pending=false; };
std::array<SecondaryPresentFence,3> secondaryPresentFences;
unsigned nextSecondaryPresentFence=0;
struct RetiredSecondarySwapchain {
  VkSwapchainKHR swapchain=VK_NULL_HANDLE;
  VkSurfaceKHR surface=VK_NULL_HANDLE;
  ANativeWindow* window=nullptr;
  bool ownsSurface=false;
  std::vector<VkImageView> views;
  std::vector<VkFramebuffer> framebuffers;
  AsyncPresentState presentation;
  std::array<SecondaryPresentFence,3> presents;
  std::vector<std::pair<size_t,uint64_t>> submissions;
  VkSemaphore acquired=VK_NULL_HANDLE,finished=VK_NULL_HANDLE;
};
std::vector<RetiredSecondarySwapchain> retiredSecondarySwapchains;
struct SecondaryPresentResult { VkResult status; uint64_t elapsedNs; bool acquisition=false; uint32_t image=0; bool fencePending=false; };
std::unique_ptr<PresentWorker<SecondaryPresentResult>> secondaryPresentWorker;
std::optional<SecondaryPresentResult> secondaryAcquired;
unsigned secondaryAcquireIndex=0;
VkFence secondaryAcquireFence=VK_NULL_HANDLE;
bool secondaryAcquireFencePending=false;
struct SecondaryDrawing {
  Surface snapshot;
  VkCommandPool pool=VK_NULL_HANDLE;
  VkCommandBuffer cmd=VK_NULL_HANDLE;
  VkDescriptorPool descriptors=VK_NULL_HANDLE;
  VkFence fence=VK_NULL_HANDLE;
  VkSemaphore ready=VK_NULL_HANDLE;
  bool pending=false; // accessed only by sequential worker operations
} secondaryDrawing;
std::optional<PresentImageDraw> secondaryPreparedDraw;

static void prepare_secondary_drawing(const Surface& source) {
  auto& draw=secondaryDrawing;
  if(!draw.pool) {
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.queueFamilyIndex=R.secondaryQueueFamily;
    vk_check(vkCreateCommandPool(R.device,&pool,nullptr,&draw.pool),"secondary command pool");
    VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocation.commandPool=draw.pool;allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocation.commandBufferCount=1;
    vk_check(vkAllocateCommandBuffers(R.device,&allocation,&draw.cmd),"secondary command buffer");
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1};
    VkDescriptorPoolCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptors.maxSets=1;descriptors.poolSizeCount=1;descriptors.pPoolSizes=&size;
    vk_check(vkCreateDescriptorPool(R.device,&descriptors,nullptr,&draw.descriptors),"secondary descriptor pool");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vk_check(vkCreateFence(R.device,&fence,nullptr,&draw.fence),"secondary drawing fence");
    draw.ready=new_semaphore();
  }
  auto& snapshot=draw.snapshot;
  if(snapshot.image && snapshot.fmt.pixel==source.fmt.pixel &&
      snapshot.extent.width==source.extent.width && snapshot.extent.height==source.extent.height)return;
  // Successful acquisition is published only after the previous worker draw
  // completed. The snapshot is never overwritten while that queue reads it.
  if(snapshot.image)destroy_surface_image(&snapshot);
  snapshot.width=source.extent.width;snapshot.height=source.extent.height;
  snapshot.slices=1;snapshot.mips=1;snapshot.dim=1;
  snapshot.format=source.format;snapshot.fmt=source.fmt;
  create_surface_image(&snapshot,false,source.extent,R.secondaryQueueFamily);
}

static void prepare_secondary_snapshot() {
  if(isolatedSecondary) {
    if(!isolatedSecondary->acquired() || isolatedSecondary->prepared())return;
    auto& source=presentation_source_screen(R.drc);
    auto* scan=source.scan.get();if(!scan || !scan->image)return;
    end_encoder();
    transition_image(scan,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    const auto snapshotCommandBuffer = command_buffer();
    // This primary snapshot uses raw barriers/copy/blit. Complete its queued
    // End/transition/Begin prefix before producer recording touches the buffer.
    if (record_worker_active()) drain_record_commands(fallback_stats::Boundary::RawPrimarySnapshot);
    isolatedSecondary->record_snapshot(snapshotCommandBuffer,scan->image,scan->fmt.pixel,{scan->extent.width,scan->extent.height});
    isolatedSourceLinear=source.scanSrgb || scan->fmt.pixel==VK_FORMAT_R8G8B8A8_SRGB ||
        scan->fmt.pixel==VK_FORMAT_B8G8R8A8_SRGB || scan->fmt.pixel==VK_FORMAT_A8B8G8R8_SRGB_PACK32;
    secondarySnapshotWait=isolatedSecondary->snapshot_wait();
    secondarySnapshotSignal=isolatedSecondary->snapshot_signal();
    return;
  }
  if(secondaryPreparedDraw || !secondaryPresentWorker || !secondaryAcquired ||
      !async_present() || present_capture_requested() || !R.drc.visible || !gfx::g_has_drc_window)return;
  if(secondaryAcquired->status!=VK_SUCCESS && secondaryAcquired->status!=VK_SUBOPTIMAL_KHR)return;
  auto& sourceScreen=presentation_source_screen(R.drc);
  auto* scan=sourceScreen.scan.get();
  if(!scan || !scan->image)return;
  prepare_secondary_drawing(*scan);
  auto draw=prepare_present_image(R.drc,secondaryAcquired->image,secondaryDrawing.snapshot,
      sourceScreen.scanSrgb,scale_filter(),fxaa_enabled());
  if(!draw)return;
  auto& snapshot=secondaryDrawing.snapshot;
  transition_image(&snapshot,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
  transition_image(scan,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
  VkImageCopy copy{};copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
  copy.dstSubresource=copy.srcSubresource;copy.extent=scan->extent;
  record::vkCmdCopyImage(command_buffer(),scan->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      snapshot.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
  transition_image(&snapshot,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
  secondaryPreparedDraw=*draw;
  // Whichever graphics submission consumes this command buffer (normally TV
  // presentation, otherwise the end-of-frame flush) signals snapshot readiness.
  secondarySnapshotSignal=secondaryDrawing.ready;
}


std::atomic<uint64_t> touchLayoutGeneration{0};
// The GamePad screen Off value Java last decided with (publishAvailability); -1 before the first.
std::atomic<int> javaSeenOff{-1};
}
// The Java UI thread transfers one retained native-window reference to the
// mailbox. Coalesce rapid resize/destroy events without touching Vulkan here.
extern "C" JNIEXPORT jboolean JNICALL
Java_org_wwhdrecomp_wwhd_GamePadDisplay_publishAvailability(JNIEnv*, jclass, jboolean available) {
  gfx::g_second_display_available = available;
  const bool off = gfx::drc_screen_off();
  javaSeenOff = off;
  return off;
}
// Before onResume acquires a second display: the saved Off choice (#222).
extern "C" JNIEXPORT void JNICALL
Java_org_wwhdrecomp_wwhd_GamePadDisplay_seedSavedOff(JNIEnv*, jclass, jboolean off) {
  gfx::seed_second_screen_off(off);
}
extern "C" JNIEXPORT void JNICALL
Java_org_wwhdrecomp_wwhd_GamePadDisplay_publishSurface(
    JNIEnv* env, jclass, jobject surface, jint width, jint height, jlong generation) {
  ANativeWindow* window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
  std::lock_guard<std::mutex> guard(secondaryMutex);
  if (generation < secondaryGeneration) {
    if (window) ANativeWindow_release(window);
    return;
  }
  secondaryGeneration = generation;
  if (pendingSecondary) ANativeWindow_release(pendingSecondary);
  pendingSecondary = window;
  secondaryWidth = width;
  secondaryHeight = height;
  secondaryChanged = true;
  secondaryTouchBox = {};
  primaryTouchBox = {};
  ++touchLayoutGeneration;
  input::set_touch(false, 0, 0);
}
extern "C" JNIEXPORT jboolean JNICALL
Java_org_wwhdrecomp_wwhd_GamePadDisplay_touch(
    JNIEnv*, jclass, jfloat x, jfloat y, jboolean down, jboolean dragging, jlong generation) {
  std::lock_guard<std::mutex> guard(secondaryMutex);
  if (generation != secondaryGeneration || generation != activeSecondaryGeneration)
    return JNI_FALSE;
  if (dual_display::active_swap) return JNI_FALSE; // this surface is showing TV
  if (gfx::drc_screen_off()) return JNI_FALSE;     // GamePad screen released: no touches (#222)
  if (!down) { input::set_touch(false, 0, 0); return JNI_TRUE; }
  float tx, ty;
  if (!gamepad_touch::map(secondaryTouchBox, x, y, dragging, tx, ty)) return JNI_FALSE;
  input::set_touch(true, tx, ty);
  return JNI_TRUE;
}
static void secondary_surface_lost() {
  R.drc.visible=false;gfx::g_has_drc_window=false;
  int64_t generation;
  {
    std::lock_guard<std::mutex> guard(secondaryMutex);
    generation=activeSecondaryGeneration;
    activeSecondaryGeneration=-1;
    secondaryTouchBox=primaryTouchBox={};
    ++touchLayoutGeneration;
    input::set_touch(false,0,0);
  }
  if(generation<0)return; // coalesce repeated errors from the same old surface
  ++R.secondarySurfaceLosses;
  // Queue host replacement without waiting for the Android UI thread. A newer
  // Java generation rejects this request, including pause/recreation/removal.
  auto* env=static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  if(!env)return;
  jobject activity=static_cast<jobject>(SDL_GetAndroidActivity());
  if(!activity)return;
  jclass cls=env->GetObjectClass(activity);
  if(cls) {
    if(jmethodID method=env->GetMethodID(cls,"recoverGamePadSurface","(J)V"))
      env->CallVoidMethod(activity,method,static_cast<jlong>(generation));
    env->DeleteLocalRef(cls);
  }
  if(env->ExceptionCheck()) {
    env->ExceptionClear();
    LOG("[vulkan] secondary surface recovery notification unavailable");
  }
  env->DeleteLocalRef(activity);
}
static VkResult secondary_present_result(VkResult status) {
  // A real present must enqueue its semaphore wait and completion fence even
  // in this fault test; simulate only the returned WSI rejection afterward.
  if(R.secondaryInjectPresentLoss.exchange(false) &&
      (status==VK_SUCCESS || status==VK_SUBOPTIMAL_KHR))return VK_ERROR_SURFACE_LOST_KHR;
  return status;
}
static void finish_secondary_present(SecondaryPresentResult result) {
  R.secondaryPresentPending=false;
  if(result.acquisition) {
    R.secondaryAcquirePending=false;
    auto& timing=screenTiming[1].acquire;
    ++timing.count;timing.ns+=result.elapsedNs;
    secondaryAcquireFencePending=result.fencePending || result.status==VK_SUCCESS || result.status==VK_SUBOPTIMAL_KHR;
    secondaryAcquired=result;
    return;
  }
  auto& timing=screenTiming[1].present;
  ++timing.count;timing.ns+=result.elapsedNs;
  if(result.status==VK_SUCCESS || result.status==VK_SUBOPTIMAL_KHR)++R.drc.presented;
  else if(result.status==VK_ERROR_OUT_OF_DATE_KHR)R.drc.resize=true;
  else if(result.status==VK_ERROR_SURFACE_LOST_KHR) {
    secondary_surface_lost();
  } else vk_check(result.status,"secondary worker presentation");
}
static void poll_secondary_present() {
  if(R.secondaryPresentPending) {
    if(auto result=secondaryPresentWorker->poll())finish_secondary_present(*result);
  }
}
static void finish_isolated_secondary(AndroidIsolatedPresenter::Result result);
static void wait_secondary_present() {
  if(isolatedSecondary) {
    if(isolatedSecondary->busy())finish_isolated_secondary(isolatedSecondary->wait());
    R.secondaryPresentPending=R.secondaryAcquirePending=false;
    return;
  }
  // Explicit drain/shutdown paths may wait; normal frames only poll.
  if(R.secondaryPresentPending)finish_secondary_present(secondaryPresentWorker->wait());
  if(secondaryAcquireFencePending) {
    vk_check(vkWaitForFences(R.device,1,&secondaryAcquireFence,VK_TRUE,UINT64_MAX),"drain secondary acquisition");
    secondaryAcquireFencePending=false;
  }
}
static void shutdown_secondary_worker() {
  isolatedSecondary.reset();isolatedSnapshotSubmission.reset();R.secondaryIsolated=false;
  // Caller has drained graphics, acquisition and the secondary queue. No
  // worker request or GPU operation may retain these private resources now.
  secondaryPresentWorker.reset();
  secondaryAcquired.reset();secondaryPreparedDraw.reset();
  auto& drawing=secondaryDrawing;
  if(drawing.snapshot.image) {
    destroy_surface_image(&drawing.snapshot);
    flush(); // retire its deferred image/view/memory ownership
  }
  if(drawing.descriptors)vkDestroyDescriptorPool(R.device,drawing.descriptors,nullptr);
  if(drawing.pool)vkDestroyCommandPool(R.device,drawing.pool,nullptr);
  if(drawing.fence)vkDestroyFence(R.device,drawing.fence,nullptr);
  if(drawing.ready)vkDestroySemaphore(R.device,drawing.ready,nullptr);
  if(secondaryAcquireFence)vkDestroyFence(R.device,secondaryAcquireFence,nullptr);
  secondaryDrawing={};secondaryAcquireFence=VK_NULL_HANDLE;
  secondaryAcquireFencePending=false;secondarySnapshotSignal=VK_NULL_HANDLE;
  R.secondaryAcquirePending=false;R.secondaryPresentPending=false;R.secondaryQueue=VK_NULL_HANDLE;
  LOG("[vulkan] secondary worker resources released");
}
static bool secondary_retirement_available() { return retiredSecondarySwapchains.size() < 4; }
static void retire_secondary_swapchain(bool surface) {
  Screen& screen=R.drc;
  if (!screen.swapchain && !surface) return;
  if (surface && !screen.swapchain && !screen.surface && !secondaryWindow && !screen.acquired && !screen.finished) return;
  RetiredSecondarySwapchain old;
  old.swapchain=std::exchange(screen.swapchain,VK_NULL_HANDLE);
  old.surface=screen.surface;
  old.ownsSurface=surface;
  old.views=take_present_screen_views(screen);
  if(R.renderPasses)old.framebuffers=pass_caches().forget_views(old.views.data(),old.views.size());
  old.presentation=std::move(asyncPresent[1]);
  asyncPresent[1]={};
  old.presents=secondaryPresentFences;
  secondaryPresentFences={};
  nextSecondaryPresentFence=0;
  for(size_t i=0;i<R.submissions.size();++i)
    if(R.submissions[i].pending)old.submissions.emplace_back(i,R.submissions[i].serial);
  if(surface) {
    screen.surface=VK_NULL_HANDLE;
    old.window=std::exchange(secondaryWindow,nullptr);
    old.acquired=std::exchange(screen.acquired,VK_NULL_HANDLE);
    old.finished=std::exchange(screen.finished,VK_NULL_HANDLE);
  }
  retiredSecondarySwapchains.push_back(std::move(old));
  R.secondaryRetirementPending=retiredSecondarySwapchains.size();
}
static void collect_secondary_swapchains() {
  if (R.secondaryRetirementHeld) return; // only set by the authored display diagnostic
  auto ready=[](VkFence fence) {
    VkResult status=timed_call(fenceStatusTiming,[&] { return vkGetFenceStatus(R.device,fence); });
    if(status==VK_NOT_READY)return false;
    vk_check(status,"secondary retirement fence");return true;
  };
  for(auto it=retiredSecondarySwapchains.begin();it!=retiredSecondarySwapchains.end();) {
    bool complete=true;
    for(auto [index,serial]:it->submissions) {
      auto& slot=R.submissions[index];
      // Never force a legacy deferred tail from this retirement iterator.
      // A not-yet-emitted POD submit is simply not ready for nonblocking poll.
      if (slot.pending && slot.serial==serial &&
          (!try_record_submission(slot) || !ready(slot.fence))) complete=false;
    }
    for(auto present:it->presents)if(present.pending && !ready(present.fence))complete=false;
    // A replaced surface remains owned until all older resize generations of
    // that same surface have retired their swapchains too.
    if(it->ownsSurface)for(auto& other:retiredSecondarySwapchains)
      if(&other!=&*it && other.surface==it->surface && it->surface)complete=false;
    if(!complete){++it;continue;}
    for(auto framebuffer:it->framebuffers)vkDestroyFramebuffer(R.device,framebuffer,nullptr);
    for(auto view:it->views)vkDestroyImageView(R.device,view,nullptr);
    if(it->swapchain)vkDestroySwapchainKHR(R.device,it->swapchain,nullptr);
    for(auto sem:it->presentation.acquire)if(sem)vkDestroySemaphore(R.device,sem,nullptr);
    for(auto sem:it->presentation.finished)vkDestroySemaphore(R.device,sem,nullptr);
    for(auto present:it->presents)if(present.fence)vkDestroyFence(R.device,present.fence,nullptr);
    if(it->acquired)vkDestroySemaphore(R.device,it->acquired,nullptr);
    if(it->finished)vkDestroySemaphore(R.device,it->finished,nullptr);
    if(it->ownsSurface && it->surface)vkDestroySurfaceKHR(R.instance,it->surface,nullptr);
    if(it->window)ANativeWindow_release(it->window);
    it=retiredSecondarySwapchains.erase(it);++R.secondaryRetired;
  }
  R.secondaryRetirementPending=retiredSecondarySwapchains.size();
}

static void finish_isolated_secondary(AndroidIsolatedPresenter::Result result) {
  R.secondaryPresentPending=R.secondaryAcquirePending=false;
  log_vulkan_result(result.status, "isolated secondary presentation");
  if(result.operation==AndroidIsolatedPresenter::Operation::Present &&
      (result.status==VK_SUCCESS || result.status==VK_SUBOPTIMAL_KHR))++R.drc.presented;
  if(result.status==VK_ERROR_SURFACE_LOST_KHR) { secondary_surface_lost();return; }
  // Android can continuously report SUBOPTIMAL for a deliberately oriented
  // pane. As in the existing path, real size events or OUT_OF_DATE trigger resize.
  if(result.status==VK_ERROR_OUT_OF_DATE_KHR)R.drc.resize=true;
  else if(result.status!=VK_SUCCESS && result.status!=VK_SUBOPTIMAL_KHR && result.status!=VK_NOT_READY && result.status!=VK_TIMEOUT) {
    LOG("[vulkan] isolated secondary operation unavailable (%d)",int(result.status));
    R.drc.visible=false;gfx::g_has_drc_window=false;input::set_touch(false,0,0);return;
  }
  if(result.operation==AndroidIsolatedPresenter::Operation::Surface) {
    std::lock_guard<std::mutex> guard(secondaryMutex);
    const bool current=!secondaryChanged && activeSecondaryGeneration==secondaryGeneration;
    R.drc.swapExtent=result.extent;
    R.drc.visible=current && result.status==VK_SUCCESS && result.extent.width && result.extent.height;
    gfx::g_has_drc_window=R.drc.visible.load();
  }
}
static void update_isolated_secondary_surface() {
  if(auto result=isolatedSecondary->poll())finish_isolated_secondary(*result);
  ANativeWindow* next=nullptr;int width=0,height=0;bool changed=false;
  {
    std::lock_guard<std::mutex> guard(secondaryMutex);
    if(secondaryChanged) {
      input::set_touch(false,0,0);
      if(isolatedSecondary->busy() || isolatedSecondary->acquired()) {
        if(!isolatedSecondary->acquired()) { R.drc.visible=false;gfx::g_has_drc_window=false; }
        return; // newest native-window event remains in the bounded mailbox
      }
      next=pendingSecondary;pendingSecondary=nullptr;width=secondaryWidth;height=secondaryHeight;
      secondaryChanged=false;activeSecondaryGeneration=secondaryGeneration;changed=true;
    }
  }
  if(changed) {
    R.drc.width=width;R.drc.height=height;R.drc.visible=false;gfx::g_has_drc_window=false;
    R.drc.resize=false;R.drc.presentWanted=effective_present_mode();
    isolatedSecondary->replace_surface(next,std::max(width,0),std::max(height,0),R.drc.presentWanted);
    R.secondaryPresentPending=true;return;
  }
  if(!isolatedSecondary->busy() && !isolatedSecondary->acquired() &&
      (R.drc.resize || (R.drc.visible && R.drc.presentWanted!=effective_present_mode()))) {
    R.drc.resize=false;R.drc.presentWanted=effective_present_mode();
    isolatedSecondary->resize(R.drc.presentWanted);R.secondaryPresentPending=true;
  }
}
static void update_secondary_surface() {
  if(isolatedSecondary) { update_isolated_secondary_surface();return; }
  poll_secondary_present();
  collect_secondary_swapchains();
  ANativeWindow* next;
  int width, height;
  {
    std::lock_guard<std::mutex> guard(secondaryMutex);
    if (!secondaryChanged) return;
    if(secondaryAcquired) {
      // Consume the old swapchain's acquired image before applying the latest
      // surface event. Its semaphore must receive its graphics-queue wait.
      R.drc.visible=true;
      gfx::g_has_drc_window=true;
      input::set_touch(false,0,0);
      return;
    }
    if (R.secondaryPresentPending || (R.secondaryPresentFences && !secondary_retirement_available())) {
      // Retain the newest mailbox event and skip secondary output while the
      // bounded retirement queue drains. Main presentation remains available.
      R.drc.visible = false;
      gfx::g_has_drc_window = false;
      input::set_touch(false, 0, 0);
      return;
    }
    next = pendingSecondary;
    pendingSecondary = nullptr;
    width = secondaryWidth;
    height = secondaryHeight;
    secondaryChanged = false;
    activeSecondaryGeneration = secondaryGeneration;
  }
  Screen& s = R.drc;
  if (R.secondaryPresentFences) retire_secondary_swapchain(true);
  else {
    ++R.secondaryIdleWaits;
    wait_idle();
    reset_present_screen(s);
    if (s.swapchain) vkDestroySwapchainKHR(R.device, s.swapchain, nullptr);
    if (s.surface) vkDestroySurfaceKHR(R.instance, s.surface, nullptr);
    s.swapchain = VK_NULL_HANDLE;
    s.surface = VK_NULL_HANDLE;
    if (secondaryWindow) ANativeWindow_release(secondaryWindow);
  }
  secondaryWindow = next;
  s.width = width;
  s.height = height;
  s.visible = false;
  gfx::g_has_drc_window = false;
  input::set_touch(false, 0, 0);
  if (!next || width <= 0 || height <= 0) return;
  auto create = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(
      vkGetInstanceProcAddr(R.instance, "vkCreateAndroidSurfaceKHR"));
  if (!create) return;
  VkAndroidSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
  info.window = next;
  VkResult result = create(R.instance, &info, nullptr, &s.surface);
  if(result==VK_ERROR_DEVICE_LOST)drivers::note_device_lost();
  if (result != VK_SUCCESS) {
    LOG("[vulkan] secondary surface unavailable (%d)", int(result));
    return;
  }
  VkBool32 supported = VK_FALSE;
  result = vkGetPhysicalDeviceSurfaceSupportKHR(R.physicalDevice, R.queueFamily, s.surface, &supported);
  if(result==VK_SUCCESS && supported && R.secondaryQueue && R.secondaryQueueFamily!=R.queueFamily)
    result=vkGetPhysicalDeviceSurfaceSupportKHR(R.physicalDevice,R.secondaryQueueFamily,s.surface,&supported);
  if (result != VK_SUCCESS || !supported) {
    vkDestroySurfaceKHR(R.instance, s.surface, nullptr);
    s.surface = VK_NULL_HANDLE;
    return;
  }
  s.resize = true;
  s.visible = true;
  // Android attaches this surface after init_device(), which only initializes
  // semaphores for the startup SDL windows. Captures and the explicit waiting
  // path still need this pair even when ordinary presentation uses the ring.
  if (!s.acquired) s.acquired = new_semaphore();
  if (!s.finished) s.finished = new_semaphore();
  gfx::g_has_drc_window = true;
}
#endif
static bool screen_has_window(const Screen& s) {
#ifdef __ANDROID__
  if (&s == &R.drc) return secondaryWindow && s.surface;
#endif
  return s.window != nullptr;
}
// One queue presentation of a screen's image (present(), or a held TV frame later: then `frame` is
// the frame it belongs to, no longer R.frame)
// (present observation: the request is registered while present() runs, inside the drawable's
// identity scope; a held frame registered it then)
static void register_observed_present(Screen &s, uint32_t observedPresentId, uint64_t frame) {
  pendingObservedPresents.emplace(observedPresentId, ObservedPresent{s.swapchainGeneration, frame});
  present_observer::google_present(s.swapchainGeneration, observedPresentId, frame);
}
static VkResult queue_present(Screen &s, VkSwapchainKHR swapchain, uint32_t index, VkSemaphore wait,
                              const void* pNext, uint32_t observedPresentId, uint64_t frame, bool secondary,
                              bool registered = false) {
  uint64_t handoffPresentId = 0;
  auto& timing = screenTiming[&s == &R.tv ? 0 : 1];
  VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  pi.pNext = pNext;
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &wait;
  pi.swapchainCount = 1;
  pi.pSwapchains = &swapchain;
  pi.pImageIndices = &index;
  VkResult pr = timed_call(timing.present, [&] {
    if (handoff_trace::enabled()) handoff_trace::emit(handoff_trace::Lane::Producer, handoff_trace::Kind::PresentBegin, 0, frame, &s == &R.tv ? 1 : 2);
    if (presentObservation && &s == &R.tv && observedHistory) {
      VkPresentTimeGOOGLE observedTime{};
      VkPresentTimesInfoGOOGLE observedTimes{VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE};
      if (observedPresentId) {
        observedTime.presentID = observedPresentId;
        handoffPresentId = observedTime.presentID;
        observedTime.desiredPresentTime = 0; // Observe; never change pacing.
        if (!registered) register_observed_present(s, observedPresentId, frame);
        observedTimes.swapchainCount = 1;
        observedTimes.pTimes = &observedTime;
        observedTimes.pNext = pi.pNext;
        pi.pNext = &observedTimes;
        std::lock_guard<std::mutex> queueLock(graphics_queue_mutex());
        return vkQueuePresentKHR(R.queue, &pi);
      }
    }
    std::lock_guard<std::mutex> queueLock(graphics_queue_mutex());
    VkResult result=vkQueuePresentKHR(R.queue, &pi);
#ifdef __ANDROID__
    if(secondary)result=secondary_present_result(result);
#endif
    return result;
  }, rprof::kWaitPresent);
  if (handoff_trace::enabled()) handoff_trace::emit(handoff_trace::Lane::Producer, handoff_trace::Kind::PresentEnd, 0, frame, &s == &R.tv ? 1 : 2, handoffPresentId);
  if (presentObservation && &s == &R.tv) {
    if (pr != VK_SUCCESS && pr != VK_SUBOPTIMAL_KHR)
      present_observer::fail("observed TV queue presentation failed");
    poll_present_observer();
  }
  if (pr == VK_SUCCESS || pr == VK_SUBOPTIMAL_KHR)
    ++s.presented;
#ifdef __ANDROID__
  // SUBOPTIMAL here only says the compositor rotates the picture (identity pre-transform, see
  // make_swapchain); size changes come as window events. Rebuilding would happen every frame.
  if (pr == VK_ERROR_OUT_OF_DATE_KHR)
    s.resize = true;
  else if (pr == VK_SUBOPTIMAL_KHR) {
  } else if (pr == VK_ERROR_SURFACE_LOST_KHR) {
    if (&s == &R.tv) recover_primary_surface();
    else secondary_surface_lost();
  }
  else
    vk_check(pr, "present scan buffer");
#else
  if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR)
    s.resize = true;
  else
    vk_check(pr, "present scan buffer");
#endif
  return pr;
}
static void present_held(bool forced) {
  if (!heldPresent.pending) return;
  HeldPresent h = heldPresent;
  heldPresent.pending = false;
  if (h.generation != R.tv.swapchainGeneration || h.swapchain != R.tv.swapchain) {
    // (cannot happen: every swapchain change presents a held frame first)
    LOG("[vulkan] even pacing: a held frame's swapchain is gone; frame %llu not presented", (unsigned long long)h.frame);
    return;
  }
  const int64_t at = even_now_ns();
  if (forced && at < h.due) ++evenForcedPresents;  // early: the next frame or a swapchain change forced it out
  queue_present(R.tv, h.swapchain, h.index, h.wait, nullptr, h.observedPresentId, h.frame, false, true);
  evenSchedule.presented(h.due, at);
  trace_even_presented(at);
}
static bool present(Screen &s) {
#ifdef __ANDROID__
  if (&s == &R.tv && quest_theater::active() && !quest_theater::visible()) return false;
#endif
  if (&s == &R.tv) present_held(true);  // the previous frame, if it is still held
  if (handoff_trace::enabled()) handoff_trace::set_producer_frame(R.frame);
  if (presentObservation && &s == &R.tv) poll_present_observer();
  if(active_deferred_commands) {
    if (handoff_trace::enabled()) handoff_trace::emit(handoff_trace::Lane::Producer, handoff_trace::Kind::DrainBegin, 0, R.frame, uint64_t(handoff_trace::Reason::PresentPrefix), &s == &R.tv ? 1 : 2);
    publish_or_finish_present_prefix(*active_deferred_commands);
    if (handoff_trace::enabled()) handoff_trace::emit(handoff_trace::Lane::Producer, handoff_trace::Kind::DrainEnd, 0, R.frame, uint64_t(handoff_trace::Reason::PresentPrefix), &s == &R.tv ? 1 : 2);
  }
#ifdef __ANDROID__
  if(&s==&R.drc && isolatedSecondary) {
    if(isolatedSecondary->busy()) { ++R.secondaryPresentSkipped;return false; }
    if(!isolatedSecondary->acquired() && R.drc.visible && gfx::g_has_drc_window) {
      isolatedSecondary->acquire();R.secondaryPresentPending=R.secondaryAcquirePending=true;
    }
    return false;
  }
#endif
#ifdef __ANDROID__
  const bool workerAcquired=&s==&R.drc && secondaryAcquired.has_value();
#else
  constexpr bool workerAcquired=false;
#endif
#ifdef __ANDROID__
  if (&s == &R.tv && s.window) {
    if (surfaceRecreate.exchange(false))
      recreate_surface(s);
    if (surfaceLost || !s.surface)
      return false;
  }
#endif
#ifdef __ANDROID__
  if (&s == &R.drc && R.secondaryPresentPending) { ++R.secondaryPresentSkipped;return false; }
  if (&s == &R.drc && !workerAcquired && R.secondaryPresentFences && !secondary_retirement_available() &&
      (s.resize || !s.swapchain || s.presentWanted != effective_present_mode())) return false;
#endif
  // Presentation changed (settings overlay): a new swapchain, as for a resize (also for a window that
  // is not shown right now, so the next frame it shows uses the new mode)
  if (!workerAcquired && screen_has_window(s) && s.swapchain &&
      (s.presentWanted != effective_present_mode() || (&s==&R.tv && s.hdrOutput!=hdr::enabled.load())))
    make_swapchain(s);
  Surface* scan = presentation_source_screen(s).scan.get();
  if (!screen_has_window(s) || !s.visible || s.width <= 0 || s.height <= 0 || !scan ||
      !scan->image)
    return false;
  const auto& sourceFormat = format_properties(scan->fmt.pixel);
  if (!(sourceFormat.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT))
    exception_report::raise(
        "Scan-buffer format does not support presentation blits");
  if (scan->fmt.kind != FormatInfo::FLOAT)
    exception_report::raise("Integer scan buffers cannot be blitted to "
                             "normalized presentation images");
  VkFilter filter = (sourceFormat.optimalTilingFeatures &
                     VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
                        ? VK_FILTER_LINEAR
                        : VK_FILTER_NEAREST;
#ifndef WWHD_SDL_HOST
  if (s.swapchain && (s.swapFormat == VK_FORMAT_B8G8R8A8_SRGB) != s.scanSrgb)
    s.resize = true;  // the scan buffer's encoding changed (GX2SetTVBuffer)
#endif
#ifdef __ANDROID__
  if (!workerAcquired && (s.resize || !s.swapchain)) {
    try {
      make_swapchain(s);
    } catch (const std::exception &e) {
      if (&s != &R.tv) {
        LOG("[vulkan] secondary swapchain unavailable (%s)", e.what());
        if(const auto* failure=dynamic_cast<const VulkanFailure*>(&e)) {
          if(failure->result==VK_ERROR_SURFACE_LOST_KHR) {
            secondary_surface_lost();
            return false;
          }
          if(failure->result==VK_ERROR_OUT_OF_DATE_KHR) {
            s.resize=true;
            return false;
          }
        }
        s.visible = false;
        gfx::g_has_drc_window = false;
        return false;
      }
      LOG("[vulkan] no swapchain (%s); waiting for a new surface", e.what());
      // Fast Activity replacement can coalesce pause/resume without a new
      // foreground event. Retry against SDL's current native surface ourselves.
      recover_primary_surface();
      return false;
    }
  }
#else
  if (s.resize || !s.swapchain)
    make_swapchain(s);
#endif
  // Reserve the observer identity before even eager drawable acquisition.
  // Enqueue time is still recorded immediately before QueuePresent below.
  uint32_t observedPresentId = 0;
  std::optional<present_observer::DrawableIdentityScope> drawableIdentity;
  if (presentObservation && &s == &R.tv && observedHistory) {
    if (nextObservedPresent >= MaxObservedPresents || pendingObservedPresents.size() >= MaxObservedPresents)
      present_observer::fail("bounded TV present observation capacity exceeded");
    else {
      observedPresentId = ++nextObservedPresent;
      drawableIdentity.emplace(s.swapchainGeneration, observedPresentId, R.frame);
    }
  }
  uint32_t index;
  auto& timing = screenTiming[&s == &R.tv ? 0 : 1];
  bool async = async_present() && !present_capture_requested();
#ifdef __ANDROID__
  if(&s==&R.drc && secondaryPreparedDraw)async=true;
#endif
  // The GamePad may run at a different refresh rate or stop consuming images
  // entirely. Never wait for its compositor on the main render thread.
  const bool secondary = &s == &R.drc;
  AsyncPresentState &ap = asyncPresent[&s == &R.tv ? 0 : 1];
#ifdef __ANDROID__
  SecondaryPresentFence* presentFence=nullptr;
  if (secondary && R.secondaryPresentFences) {
    auto& candidate=secondaryPresentFences[nextSecondaryPresentFence];
    if(candidate.pending) {
      VkResult status=timed_call(fenceStatusTiming,[&] { return vkGetFenceStatus(R.device,candidate.fence); });
      if(status==VK_NOT_READY)return false;
      vk_check(status,"secondary presentation completion");candidate.pending=false;
    }
    if(!candidate.fence) {
      VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
      vk_check(vkCreateFence(R.device,&info,nullptr,&candidate.fence),"secondary present fence");
    }
    presentFence=&candidate;
  }
#endif
  VkSemaphore acquireSemaphore = s.acquired;
  unsigned acquireIndex = 0;
  if (async || workerAcquired) {
#ifdef __ANDROID__
    acquireIndex = workerAcquired ? secondaryAcquireIndex : ap.next;
#else
    acquireIndex = ap.next;
#endif
    if(!workerAcquired)ap.next = (ap.next + 1) % ap.acquire.size();
    if (!ap.acquire[acquireIndex])
      ap.acquire[acquireIndex] = new_semaphore();
    // the submission that last waited on this semaphore must be done with it
    auto &previous = R.submissions[ap.slot[acquireIndex]];
    if (ap.serial[acquireIndex] && previous.pending && previous.serial == ap.serial[acquireIndex]) {
      if (secondary) {
        if (!try_record_submission(previous)) return false;
        const VkResult ready = timed_call(fenceStatusTiming,[&] { return vkGetFenceStatus(R.device, previous.fence); });
        if (ready == VK_NOT_READY)
          return false;
        vk_check(ready, "secondary acquire semaphore completion");
      }
      retire_submission(previous);
    }
    acquireSemaphore = ap.acquire[acquireIndex];
  }
  VkResult ar;
#ifdef __ANDROID__
  if(workerAcquired) {
    ar=secondaryAcquired->status;index=secondaryAcquired->image;
    secondaryAcquired.reset();
  } else if(secondary && async && secondaryPresentWorker) {
    if(!secondaryAcquireFence) {
      VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
      vk_check(vkCreateFence(R.device,&info,nullptr,&secondaryAcquireFence),"secondary acquisition fence");
    }
    secondaryAcquireIndex=acquireIndex;
    const VkDevice device=R.device;
    const VkSwapchainKHR swapchain=s.swapchain;
    const VkFence fence=secondaryAcquireFence;
    const bool fencePending=secondaryAcquireFencePending;
    secondaryPresentWorker->submit([device,swapchain,acquireSemaphore,fence,fencePending] {
      while(R.secondaryAcquireHeld.load()) {
        R.secondaryAcquireWaiting=true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      R.secondaryAcquireWaiting=false;
      if(secondaryDrawing.pending) {
        VkResult ready=vkGetFenceStatus(device,secondaryDrawing.fence);
        if(ready==VK_NOT_READY)return SecondaryPresentResult{VK_NOT_READY,0,true,0,fencePending};
        vk_check(ready,"secondary drawing completion");secondaryDrawing.pending=false;
      }
      if(fencePending) {
        VkResult ready=vkGetFenceStatus(device,fence);
        if(ready==VK_NOT_READY)return SecondaryPresentResult{VK_NOT_READY,0,true,0,true};
        vk_check(ready,"secondary acquisition fence");
      }
      if(R.secondaryInjectAcquireLoss.exchange(false))
        return SecondaryPresentResult{VK_ERROR_SURFACE_LOST_KHR,0,true,0,false};
      vk_check(vkResetFences(device,1,&fence),"reset secondary acquisition fence");
      uint32_t image=0;
      const auto start=std::chrono::steady_clock::now();
      VkResult status=vkAcquireNextImageKHR(device,swapchain,0,acquireSemaphore,fence,&image);
      return SecondaryPresentResult{status,uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now()-start).count()),true,image};
    });
    R.secondaryPresentPending=true;
    R.secondaryAcquirePending=true;
    return false;
  } else
#endif
  ar = timed_call(timing.acquire, [&] {
#ifdef __ANDROID__
    if(secondary && R.secondaryInjectAcquireLoss.exchange(false))return VK_ERROR_SURFACE_LOST_KHR;
#endif
    if (handoff_trace::enabled()) handoff_trace::emit(handoff_trace::Lane::Producer, handoff_trace::Kind::AcquireBegin, 0, R.frame, &s == &R.tv ? 1 : 2, acquireIndex);
    const auto result = vkAcquireNextImageKHR(R.device, s.swapchain, present_acquire_timeout(secondary),
                                acquireSemaphore, VK_NULL_HANDLE, &index);
    if (handoff_trace::enabled()) handoff_trace::emit(handoff_trace::Lane::Producer, handoff_trace::Kind::AcquireEnd, 0, R.frame, &s == &R.tv ? 1 : 2, uint64_t(int64_t(result)));
    return result;
  }, rprof::kWaitAcquire);
  // No image was acquired and the semaphore remains unsignalled. Leave it
  // available for another frame; submitting a wait here would deadlock.
  if (ar == VK_NOT_READY || ar == VK_TIMEOUT)
    return false;
  if (ar == VK_ERROR_OUT_OF_DATE_KHR) {
    s.resize = true;
    return false;
  }
#ifdef __ANDROID__
  if (ar == VK_ERROR_SURFACE_LOST_KHR) {
    if (&s == &R.tv) recover_primary_surface();
    else secondary_surface_lost();
    return false;
  }
#endif
  if (ar != VK_SUBOPTIMAL_KHR)
    vk_check(ar, "acquire scan image");
#ifdef __ANDROID__
  if(secondary && async && secondaryPresentWorker) {
    auto draw=std::exchange(secondaryPreparedDraw,{});
    if(draw) {
      // The worker's drawing waits on the snapshot's readiness semaphore, so its
      // signal must be submitted first (a binary semaphore wait may not precede
      // its signal). The TV presentation normally submitted it, but it returns
      // without a submission while its surface is lost or being recreated, or no
      // image was acquired; the end-of-frame flush would then come only after the
      // worker's wait and present. On the emulator's gfxstream host that present
      // blocked every queue until its 3 s release-fence timeout, after which the
      // graphics queue never went idle again.
      if(secondarySnapshotSignal)submit(VK_NULL_HANDLE,VK_NULL_HANDLE,true);
      // Acquire semaphore consumption belongs to the worker's drawing fence,
      // not to the graphics submission that merely copied the snapshot.
      ap.serial[acquireIndex]=0;
      if(ap.finished.size()!=s.images.size()) {
        for(auto semaphore:ap.finished)vkDestroySemaphore(R.device,semaphore,nullptr);
        ap.finished.clear();
        for(size_t i=0;i<s.images.size();++i)ap.finished.push_back(new_semaphore());
      }
      const VkSemaphore finished=ap.finished[index];
      const VkSwapchainKHR swapchain=s.swapchain;
      const VkFence presentCompletion=presentFence->fence;
      presentFence->pending=true;
      nextSecondaryPresentFence=(nextSecondaryPresentFence+1)%secondaryPresentFences.size();
      s.layouts[index]=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
      const VkQueue queue=R.secondaryQueue;
      secondaryPresentWorker->submit([draw=*draw,acquireSemaphore,finished,swapchain,presentCompletion,index,queue] {
        IsolatedCommandDispatch isolated;
        auto& state=secondaryDrawing;
        vk_check(vkResetCommandPool(draw.device,state.pool,0),"reset secondary command pool");
        vk_check(vkResetDescriptorPool(draw.device,state.descriptors,0),"reset secondary descriptor pool");
        vk_check(vkResetFences(draw.device,1,&state.fence),"reset secondary drawing fence");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vk_check(vkBeginCommandBuffer(state.cmd,&begin),"begin secondary drawing");
        record_present_image(draw,state.cmd,state.descriptors);
        vk_check(vkEndCommandBuffer(state.cmd),"end secondary drawing");
        const VkSemaphore waits[]={acquireSemaphore,state.ready};
        const VkPipelineStageFlags stages[]={VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT};
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount=2;submit.pWaitSemaphores=waits;submit.pWaitDstStageMask=stages;
        submit.commandBufferCount=1;submit.pCommandBuffers=&state.cmd;
        submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&finished;
        vk_check(vkQueueSubmit(queue,1,&submit,state.fence),"submit secondary drawing");
        state.pending=true;
        while(R.secondaryPresentHeld.load())std::this_thread::sleep_for(std::chrono::milliseconds(2));
        vk_check(vkResetFences(draw.device,1,&presentCompletion),"reset secondary presentation fence");
        VkSwapchainPresentFenceInfoEXT fences{VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT};
        fences.swapchainCount=1;fences.pFences=&presentCompletion;
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.pNext=&fences;
        present.waitSemaphoreCount=1;present.pWaitSemaphores=&finished;
        present.swapchainCount=1;present.pSwapchains=&swapchain;present.pImageIndices=&index;
        const auto start=std::chrono::steady_clock::now();
        VkResult status=secondary_present_result(vkQueuePresentKHR(queue,&present));
        return SecondaryPresentResult{status,uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()-start).count())};
      });
      R.secondaryPresentPending=true;
      return false;
    }
  }
#endif
  // MoltenVK may acquire a CAMetalDrawable lazily inside BeginRendering. Keep
  // this presentation-only wait on the original producer path, after completing
  // older worker commands. Keep end/reset/submit and QueuePresent on this
  // producer too; restore worker routing when present() returns.
  std::optional<ProducerPresentationRecording> producerPresentation;
  if (record_worker_active() && !nativeWorkerPresentation) producerPresentation.emplace();
  bool composed=false;
  timed_call(timing.compose, [&] {
    composed=draw_present_screen(s,index);
    return VK_SUCCESS;
  });
  if (!composed) {
    transition_image(scan, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = s.layouts[index];
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = s.images[index];
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    auto cmd = command_buffer();
    record::vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    VkClearColorValue black{};
    record::vkCmdClearColorImage(cmd, s.images[index],
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1,
                         &b.subresourceRange);
    // Clear and blit overlap the swap image. Order the two transfer writes.
    b.oldLayout = b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcAccessMask = b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    record::vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    const float ratio =
        std::min(float(s.swapExtent.width) / scan->extent.width,
                 float(s.swapExtent.height) / scan->extent.height);
    int w = int(scan->extent.width * ratio),
        h = int(scan->extent.height * ratio);
    int x = (s.swapExtent.width - w) / 2, y = (s.swapExtent.height - h) / 2;
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {int(scan->extent.width), int(scan->extent.height),
                          1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0] = {x, y, 0};
    blit.dstOffsets[1] = {x + w, y + h, 1};
    record::vkCmdBlitImage(cmd, scan->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   s.images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &blit, filter);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = 0;
    record::vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    s.layouts[index] = b.newLayout;
    s.imageUses[index] = {};
    derive_dependency(s.imageUses[index], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
  }
  record_present_capture(s,index);
  VkSemaphore finishedSemaphore = s.finished;
  if (async || workerAcquired) {
    if (ap.finished.size() != s.images.size()) {  // new swapchain
#ifdef __ANDROID__
      if (!secondary) {
        drain_record_commands();
        vk_check(vkQueueWaitIdle(R.queue), "primary presentation semaphores idle");
      }
      else if (!R.secondaryPresentFences) vk_check(device_wait_idle(), "presentation semaphores idle");
#else
      vk_check(device_wait_idle(), "presentation semaphores idle");
#endif
      for (VkSemaphore f : ap.finished)
        vkDestroySemaphore(R.device, f, nullptr);
      ap.finished.clear();
      for (size_t i = 0; i < s.images.size(); i++)
        ap.finished.push_back(new_semaphore());
    }
    finishedSemaphore = ap.finished[index];
    const size_t slot = R.activeSubmission;
    submit(acquireSemaphore, finishedSemaphore, async);
    ap.slot[acquireIndex] = slot;
    ap.serial[acquireIndex] = R.submissions[slot].serial;
  } else {
    submit(s.acquired, s.finished);
  }
  finish_present_capture(s);
  VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &finishedSemaphore;
  pi.swapchainCount = 1;
  pi.pSwapchains = &s.swapchain;
  pi.pImageIndices = &index;
#ifdef __ANDROID__
  VkSwapchainPresentFenceInfoEXT fenceInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT};
  if(presentFence) {
    vk_check(timed_call(fenceResetTiming,[&] { return vkResetFences(R.device,1,&presentFence->fence); }),"reset secondary present fence");
    fenceInfo.swapchainCount=1;fenceInfo.pFences=&presentFence->fence;
    pi.pNext=&fenceInfo;presentFence->pending=true;
    nextSecondaryPresentFence=(nextSecondaryPresentFence+1)%secondaryPresentFences.size();
  }
#endif
#ifdef __ANDROID__
  if(secondary && async && secondaryPresentWorker) {
    const VkSwapchainKHR swapchain=s.swapchain;
    const VkFence fence=presentFence ? presentFence->fence : VK_NULL_HANDLE;
    const VkQueue queue=R.secondaryQueue;
    secondaryPresentWorker->submit([swapchain,fence,finishedSemaphore,index,queue] {
      // The diagnostic holds this operation exactly where a blocking WSI call
      // would occupy the worker. No renderer/mailbox lock is held here.
      while(R.secondaryPresentHeld.load())std::this_thread::sleep_for(std::chrono::milliseconds(2));
      VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
      info.waitSemaphoreCount=1;info.pWaitSemaphores=&finishedSemaphore;
      info.swapchainCount=1;info.pSwapchains=&swapchain;info.pImageIndices=&index;
      VkSwapchainPresentFenceInfoEXT fences{VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT};
      if(fence){fences.swapchainCount=1;fences.pFences=&fence;info.pNext=&fences;}
      const auto started=std::chrono::steady_clock::now();
      VkResult result=secondary_present_result(vkQueuePresentKHR(queue,&info));
      return SecondaryPresentResult{result,uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now()-started).count())};
    });
    R.secondaryPresentPending=true;
    return false;
  }
#endif
  // Even frame delivery (interp_pacing.h EvenSchedule; interp.cpp: on by default on Android): a TV frame of a
  // swap that is ready before its slot is held (its work is submitted; only the present call waits)
  if (&s == &R.tv && async && evenPresentSwap && !secondary && !present_capture_requested() &&
#ifdef __ANDROID__
      !quest_theater::active() &&
#endif
      (evenPresentInterval || even_trace_file())) {
    const int64_t ready = even_now_ns();
    const int64_t due = evenSchedule.plan(ready, evenPresentInterval);
    trace_even_present(R.frame, evenPresentHold, evenPresentInterval, ready, due);
    if (evenPresentInterval && ++evenFrames % 1200 == 0) {
      LOG("[vulkan] even pacing: %.0f%% of the last 1200 frames held (%.2f ms on average), %llu forced out early",
          evenHeldPresents / 12.0, evenHeldPresents ? evenHeldNs / 1e6 / evenHeldPresents : 0.0,
          (unsigned long long)evenForcedPresents);
      evenHeldPresents = evenForcedPresents = evenHeldNs = 0;
    }
    if (due > ready + 100'000) {  // more than 0.1 ms early
      if (presentObservation && observedHistory && observedPresentId)
        register_observed_present(s, observedPresentId, R.frame);
      heldPresent = {true, due, s.swapchain, index, finishedSemaphore, observedPresentId, R.frame, s.swapchainGeneration};
      ++evenHeldPresents;
      evenHeldNs += uint64_t(due - ready);
      return true;
    }
    const VkResult pr = queue_present(s, s.swapchain, index, finishedSemaphore, pi.pNext, observedPresentId, R.frame, false);
    const int64_t at = even_now_ns();
    evenSchedule.presented(due, at);
    trace_even_presented(at);
    return pr == VK_SUCCESS || pr == VK_SUBOPTIMAL_KHR;
  }
  const VkResult pr = queue_present(s, s.swapchain, index, finishedSemaphore, pi.pNext, observedPresentId, R.frame, secondary);
#ifdef __ANDROID__
  if (&s == &R.tv && (pr == VK_SUCCESS || pr == VK_SUBOPTIMAL_KHR)) quest_theater::presented(stereo::frame.mode);
#endif
  if (!async)
    vk_check(timed_call(timing.idle, [&] { return vkQueueWaitIdle(R.queue); }, rprof::kWaitGpu),
             "present completion");
  return pr == VK_SUCCESS || pr == VK_SUBOPTIMAL_KHR;
}
void copy_to_scan(uint32_t cb, uint32_t target) {
  Surface *src = surface_from_color_buffer(cb);
  if (!src)
    return;
  if (stereoEye && target == 4) return; // GamePad is a shared 2D device.
  Screen &s = stereoEye ? stereoRight : (target == 4 ? R.drc : R.tv);
  auto fits = [src](const Surface *d) {
    return d->extent.width == src->extent.width && d->extent.height == src->extent.height && d->fmt.pixel == src->fmt.pixel;
  };
  // the size the picture stands for (the layout): the source at the full internal resolution (dynamic
  // resolution, dynres.h, renders it smaller; exactly its size while off); an eye-sized target counts twice
  s.buildingScanDisplayExtent = {uint32_t(std::lround(src->extent.width / src->dyn)), uint32_t(std::lround(src->extent.height / src->dyn))};
  if (target != 4) {
    bool halfWidth, halfHeight;  // an eye-sized target (surfaces.cpp), not the full-size fallback
    stereo_target_halves(src, halfWidth, halfHeight);
    if (halfWidth) s.buildingScanDisplayExtent.width *= 2;
    if (halfHeight) s.buildingScanDisplayExtent.height *= 2;
  }
  if (!s.buildingScan || !fits(s.buildingScan.get())) {
    // dynamic resolution (dynres.h): the scan images of the previous sizes are kept for the next change
    std::unique_ptr<Surface> kept;
    if (dynres::on()) {
      for (auto it = s.scanSpares.begin(); it != s.scanSpares.end(); ++it)
        if (fits(it->get())) { kept = std::move(*it); s.scanSpares.erase(it); break; }
      if (s.buildingScan) s.scanSpares.insert(s.scanSpares.begin(), std::move(s.buildingScan));
    }
    if (s.buildingScan)
      destroy_surface_image(s.buildingScan.get());
    if (kept) {
      s.buildingScan = std::move(kept);
    } else {
      s.buildingScan = std::make_unique<Surface>();
      auto *d = s.buildingScan.get();
      d->width = src->extent.width;
      d->height = src->extent.height;
      d->format = src->format;
      d->fmt = src->fmt;
      d->scale = 1;
      create_surface_image(d, false);
    }
  }
  if (!dynres::on() && !s.scanSpares.empty()) {
    for (auto &k : s.scanSpares) destroy_surface_image(k.get());
    s.scanSpares.clear();
  }
  if (dynres::on()) {
    // Scan images for the sizes of the source's kept images (the neighbouring levels: two each, the scan
    // and the one being published), made ahead one a frame; images of other sizes go.
    auto neighbour = [src](const Surface *d) {
      for (auto &k : src->spares)
        if (k->image && d->extent.width == k->extent.width && d->extent.height == k->extent.height && d->fmt.pixel == src->fmt.pixel)
          return true;
      return false;
    };
    for (auto it = s.scanSpares.begin(); it != s.scanSpares.end();) {
      // keep: the neighbours' sizes, and one of the current size (the published scan's partner after a change)
      if (neighbour(it->get()) || fits(it->get())) ++it;
      else { destroy_surface_image(it->get()); it = s.scanSpares.erase(it); }
    }
    static uint64_t madeFrame = ~0ull;
    if (madeFrame != R.frame)
      for (auto &k : src->spares) {
        if (!k->image) continue;
        int have = 0;
        for (auto &d : s.scanSpares)
          if (d->extent.width == k->extent.width && d->extent.height == k->extent.height && d->fmt.pixel == src->fmt.pixel) have++;
        if (have >= 2) continue;
        auto d = std::make_unique<Surface>();
        d->width = k->extent.width; d->height = k->extent.height; d->format = src->format; d->fmt = src->fmt; d->scale = 1;
        create_surface_image(d.get(), false);
        touch_image(d.get());
        s.scanSpares.push_back(std::move(d));
        madeFrame = R.frame;
        break;
      }
  }
  // the size the picture stands for (layout, dynres.h): the scan at the full internal resolution
  s.buildingScan->dyn = src->dyn;
  resample(src, s.buildingScan.get(), 1);
  if (target & 1)
    draw_mod_overlay(*s.buildingScan,s.buildingScanDisplayExtent);
  mark_gpu_written(s.buildingScan.get());
  s.buildingScanSrgb = s.srgb.load();
  s.buildingScanDirty = true;
}
// debug: WWHD_DUMP_FRAMES=100,300 writes the TV image of those frames to frame_<n>.png (and
// frame_<n>_drc.png; WWHD_DUMP_PRESENT=1 adds the composed window, frame_<n>_present.png), as Metal
static void dump_scan(Screen &s, const std::string &path) {
  if (!s.scan || !s.scan->image)
    return;
  try {
    write_rgba_png(path, s.scan->extent.width, s.scan->extent.height,
                   read_surface_rgba(*s.scan, s.scanSrgb));
    LOG("[gfx] wrote %s (%ux%u)", path.c_str(), s.scan->extent.width, s.scan->extent.height);
  } catch (const std::exception &e) {
    LOG("[gfx] cannot write %s: %s", path.c_str(), e.what());
  }
}
// the composed windows (what present() puts on the screen), written after the frame (swap)
static void request_present_dump(const std::string& path) { gfx::request_present_dump(path); }
static std::atomic<bool> captureRequested{false};
void request_capture() { captureRequested = true; }
static void frame_dumps(uint64_t frame) {
  static const auto frames = gfx::capture_schedule::parse(getenv("WWHD_DUMP_FRAMES"));
  const uint64_t dumpFrame = fixed_seed::script_frame(frame);  // (TV frame unless WWHD_TEST_FIXED_SEED)
  // test aid: WWHD_BG_SHADERS_DUMP=n writes the TV image of the first n frames that left a draw out
  // (Shader compile Fast) and of the two frames after each, to check what went missing
  static const uint64_t leftOutDumps = getenv("WWHD_BG_SHADERS_DUMP") ? strtoull(getenv("WWHD_BG_SHADERS_DUMP"), nullptr, 10) : 0;
  if (leftOutDumps) {
    static uint64_t until = 0, count = 0;
    const bool leftOut = bgshaders::skipped_in_frame(frame - 1);  // the frame just drawn
    if (leftOut && count < leftOutDumps) { until = dumpFrame + 2; ++count; }
    if (dumpFrame <= until) {
      LOG("[shaders] dump of %s frame %llu", leftOut ? "a left-out" : "the next", (unsigned long long)dumpFrame);
      dump_scan(R.tv, "frame_" + std::to_string(dumpFrame) + ".png");
    }
  }
  if (gfx::capture_schedule::contains(frames, dumpFrame)) {
    dump_scan(R.tv, "frame_" + std::to_string(dumpFrame) + ".png");
    dump_scan(R.drc, "frame_" + std::to_string(dumpFrame) + "_drc.png");
    if (getenv("WWHD_DUMP_PRESENT"))
      request_present_dump("frame_" + std::to_string(dumpFrame) + "_present.png");
  }
  // Test captures relative to the completed full-state load, whose actual
  // renderer frame can vary even when WWHD_STATE_LOAD_AT is fixed.
  static const auto loadFrames = gfx::capture_schedule::parse(getenv("WWHD_DUMP_LOAD_FRAMES"));
  const uint64_t loaded = loadFrames.empty() ? 0 : ss::last_load_frame();
  if (gfx::capture_schedule::relative_due(loadFrames, frame, loaded)) {
    const auto stem = gfx::capture_schedule::stem(frame - loaded, true);
    dump_scan(R.tv, stem + ".png");
    dump_scan(R.drc, stem + "_drc.png");
    if (getenv("WWHD_DUMP_PRESENT")) request_present_dump(stem + "_present.png");
    LOG("[gfx] load-relative capture: load frame %llu, frame %llu, offset %llu, load step %llu, step %llu",
        (unsigned long long)loaded, (unsigned long long)frame,
        (unsigned long long)(frame - loaded), (unsigned long long)ss::last_load_step(),
        (unsigned long long)interp::logic_steps());
  }
  // P / F12 (Graphics menu): the pictures of this frame in captures/<time>/ (WWHD_CAPTURE=<frame>
  // scripts it when WWHD_CAPTURE_PATH is not used); the Metal renderer also writes a draw log
  static const uint64_t scripted = getenv("WWHD_CAPTURE") && !getenv("WWHD_CAPTURE_PATH")
                                       ? strtoull(getenv("WWHD_CAPTURE"), nullptr, 10) : ~0ull;
  if (captureRequested.exchange(false) || frame == scripted) {
    char dir[64];
    time_t t = time(nullptr);
    strftime(dir, sizeof dir, "captures/%Y%m%d-%H%M%S", localtime(&t));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    dump_scan(R.tv, std::string(dir) + "/tv.png");
    dump_scan(R.drc, std::string(dir) + "/gamepad.png");
    request_present_dump(std::string(dir) + "/present.png");
    LOG("[gfx] capture of frame %llu written to %s", (unsigned long long)frame, dir);
  }
}
// Screenshots (screenshot.h): recorded into the frame's command buffer at the swap, read back once
// that submission's fence has signalled (checked at later swaps: no wait), encoded on the screenshot
// thread; the readback buffers come back from it to be freed on this thread.
namespace {
struct PendingShot {
  std::string path;
  bool tv = false;
  uint64_t frame = 0;
  Buffer buffer{};
  uint32_t width = 0, height = 0;
  bool bgra = false;
  size_t slot = 0;
  uint64_t serial = 0;  // 0: not submitted yet
};
std::vector<PendingShot> pendingShots;
std::mutex shotBuffersMu;
std::vector<Buffer> shotBuffersDone;
}  // namespace
static void take_screenshots(const gfx::PresentPlan &plan) {
  std::string tvPath, drcPath;
  if (!R.tv.scan || !R.tv.scan->image || !screenshot::take(R.frame + 1, tvPath, drcPath))
    return;
  auto shoot = [&](Screen &s, const std::string &path, bool tv) {
    PendingShot p;
    p.path = path;
    p.tv = tv;
    p.frame = R.frame + 1;
    try {
      if (!record_screenshot(s, p.buffer, p.width, p.height, p.bgra)) {
        screenshot::write_async(path, 0, 0, 0, nullptr, nullptr, p.frame, tv);
        return;
      }
    } catch (const std::exception &e) {
      LOG("[screenshot] %s: %s", path.c_str(), e.what());
      screenshot::write_async(path, 0, 0, 0, nullptr, nullptr, p.frame, tv);
      return;
    }
    p.slot = R.activeSubmission;
    pendingShots.push_back(std::move(p));
  };
  const auto t0 = std::chrono::steady_clock::now();
  shoot(R.tv, tvPath, true);
  // the GamePad picture while it is shown (GamePad window, picture-in-picture, GamePad only)
  if (!drcPath.empty() && R.drc.scan && R.drc.scan->image && (plan.drc_window || plan.pip_on || plan.drc_only))
    shoot(R.drc, drcPath, false);
  else if (!drcPath.empty()) {
    LOG("[screenshot] GamePad picture not shown: only the TV picture saved");
    screenshot::write_async(drcPath, 0, 0, 0, nullptr, nullptr, R.frame + 1, false);
  }
  LOG("[screenshot] frame %llu: recorded in %.2f ms (render thread)", (unsigned long long)(R.frame + 1),
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
}
static void service_screenshots() {
  {
    std::lock_guard<std::mutex> lk(shotBuffersMu);
    for (auto &b : shotBuffersDone)
      defer_buffer(b);
    shotBuffersDone.clear();
  }
  for (auto it = pendingShots.begin(); it != pendingShots.end();) {
    auto &slot = R.submissions[it->slot];
    if (!it->serial) {  // recorded in this swap: the submission of that slot holds it now
      it->serial = slot.serial;
      ++it;
      continue;
    }
    // a slot reused for a newer submission retired ours first
    if(slot.retainedUnsubmitted && slot.serial==it->serial) { ++it;continue; }
    if (slot.pending && slot.serial==it->serial && !try_record_submission(slot)) { ++it; continue; }
    const bool done = !slot.pending || slot.serial != it->serial || vkGetFenceStatus(R.device, slot.fence) == VK_SUCCESS;
    if (!done) {
      ++it;
      continue;
    }
    Buffer b = it->buffer;
    std::shared_ptr<const void> owner(nullptr, [b](const void *) {
      std::lock_guard<std::mutex> lk(shotBuffersMu);
      shotBuffersDone.push_back(b);
    });
    screenshot::write_async(it->path, it->width, it->height, size_t(it->width) * 4, static_cast<const uint8_t *>(b.mapped),
                            std::move(owner), it->frame, it->tv, it->bgra);
    it = pendingShots.erase(it);
  }
}
namespace {
gfx::PresentPlan completedPresentPlan;
bool completedPresentPlanValid = false;
uint64_t nextPipelineRepeatNs = 0, lastNewPresentNs = 0;
uint64_t pendingPresentationFrame = ~uint64_t{0};
// Deliberately one-shot and separate from the synthetic smoke proof. Readbacks
// perturb timing; this opt-in diagnostic must never be enabled for benchmarks.
struct PendingPictureProof {
  std::string directory;
  uint64_t frame = 0, loadFrame = 0, loadStep = 0, logicStep = 0;
  Surface* scans[2]{};
  VkExtent3D extents[2]{};
  std::vector<uint8_t> before[2];
};
PendingPictureProof begin_pending_picture_proof() {
  static bool attempted = false;
  const char* directory = std::getenv("WWHD_VK_PENDING_CAPTURE_DIR");
  if (attempted || !directory || !*directory) return {};
  const char* afterLoad = std::getenv("WWHD_VK_PENDING_CAPTURE_AFTER_LOAD");
  if (afterLoad && std::strcmp(afterLoad, "0") && !ss::last_load_frame()) return {};
  // Both separately published scans are required. Never inspect buildingScan.
  if (!R.tv.scan || !R.drc.scan || !R.tv.scan->image || !R.drc.scan->image) return {};
  attempted = true;
  PendingPictureProof proof;
  try {
    proof.directory = directory;
    proof.frame = R.frame; proof.loadFrame = ss::last_load_frame();
    proof.loadStep = ss::last_load_step(); proof.logicStep = interp::logic_steps();
    if (!std::filesystem::create_directories(proof.directory))
      throw std::runtime_error("pending proof directory must be fresh");
    Screen* screens[] = {&R.tv, &R.drc};
    for (unsigned i = 0; i != 2; ++i) {
      proof.scans[i] = screens[i]->scan.get();
      proof.extents[i] = proof.scans[i]->extent;
      const auto extent = proof.extents[i];
      if (!extent.width || !extent.height || uint64_t(extent.width) * extent.height > 16777216)
        throw std::runtime_error("pending capture exceeds 16 megapixels per scan");
      proof.before[i] = read_surface_rgba(*proof.scans[i], screens[i]->scanSrgb);
    }
  } catch (const std::exception& error) {
    LOG("[vulkan pending proof] capture failed: %s", error.what());
    return {};
  }
  return proof;
}
void finish_pending_picture_proof(const PendingPictureProof& proof, bool presented) {
  if (proof.directory.empty()) return;
  try {
    const bool stateUnchanged = R.frame == proof.frame && R.tv.scan.get() == proof.scans[0] &&
                               R.drc.scan.get() == proof.scans[1];
    if (!stateUnchanged) throw std::runtime_error("published scan or guest frame changed during repeat");
    const char* names[] = {"tv", "gamepad"};
    Screen* screens[] = {&R.tv, &R.drc};
    bool exact[2]{};
    for (unsigned i = 0; i != 2; ++i) {
      const auto after = read_surface_rgba(*proof.scans[i], screens[i]->scanSrgb);
      exact[i] = after == proof.before[i];
      const auto stem = proof.directory + "/" + names[i];
      write_rgba_png(stem + "-before.png", proof.extents[i].width, proof.extents[i].height, proof.before[i]);
      write_rgba_png(stem + "-after.png", proof.extents[i].width, proof.extents[i].height, after);
    }
    const auto path = proof.directory + "/proof.json";
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) throw std::runtime_error("cannot open pending proof metadata");
    std::fprintf(file, "{\"frame\":%llu,\"load_frame\":%llu,\"load_step\":%llu,\"logic_step_before\":%llu,\"logic_step_after\":%llu,\"presented\":%s,\"guest_frame_and_scans_unchanged\":true,\"tv_exact\":%s,\"gamepad_exact\":%s,\"scope\":\"published scans; excludes composed window and overlays; readbacks perturb timing\"}\n",
        (unsigned long long)proof.frame, (unsigned long long)proof.loadFrame,
        (unsigned long long)proof.loadStep, (unsigned long long)proof.logicStep,
        (unsigned long long)interp::logic_steps(), presented ? "true" : "false",
        exact[0] ? "true" : "false", exact[1] ? "true" : "false");
    const bool written = std::fclose(file) == 0;
    if (!written) throw std::runtime_error("cannot write pending proof metadata");
    LOG("[vulkan pending proof] presented=%u frame=%llu TV exact=%u GamePad exact=%u -> %s",
        unsigned(presented), (unsigned long long)proof.frame, unsigned(exact[0]), unsigned(exact[1]), proof.directory.c_str());
  } catch (const std::exception& error) {
    LOG("[vulkan pending proof] capture failed: %s", error.what());
  }
}
}
void begin_pipeline_pending_frame() {
  if (pendingPresentationFrame != R.frame) {
    pendingPresentationFrame = R.frame;
    // Allow the first demanded pipeline a frame budget before issuing a
    // repeat. Normal guest pacing alone must not force an extra presentation
    // for every small compilation. Later misses in this same frame share it.
    nextPipelineRepeatNs = std::max(nextPipelineRepeatNs, stutter::now() + uint64_t(16666667));
  }
}
void present_completed_frame_while_pipeline_pending() {
  if(active_deferred_commands)active_deferred_commands->finish_exact();
  const auto started = stutter::now();
  if (!completedPresentPlanValid || started < nextPipelineRepeatNs) return;
  // The current command stream stays paused at its exact draw. Submission of
  // its prefix is legal, but only the separately published completed scans are
  // sampled here; buildingScan and all guest attachments remain untouched.
  end_encoder();
  const auto proof = begin_pending_picture_proof();
  set_present_plan(&completedPresentPlan);
  bool presented = false;
  try {
    presented = present(R.tv);
    if (completedPresentPlan.drc_window) presented = present(R.drc) || presented;
    flush_async();
  } catch (...) { set_present_plan(nullptr); throw; }
  set_present_plan(nullptr);
  finish_pending_picture_proof(proof, presented);
  const auto ended = stutter::now();
  nextPipelineRepeatNs = ended + 16666667; // At most 60 repeats/s, independent of guest progress.
  if (presented && stutter::trace().enabled()) {
    stutter::trace().add("repeated_frame", R.frame, 0, started, ended, 0);
    stutter::trace().add("repeated_frame_age", R.frame, 0, lastNewPresentNs, ended, 0);
  }
}
#if defined(WWHD_SDL_HOST) && !defined(__ANDROID__)
static std::atomic<SDL_Window*> pendingGamepadWindow{nullptr};
static VkSurfaceKHR pendingGamepadSurface = VK_NULL_HANDLE;
static void attach_gamepad_window() {
  SDL_Window* window = pendingGamepadWindow.exchange(nullptr);
  if (!window) return;
  R.drc.surface = pendingGamepadSurface;
  VkBool32 supported = false;
  vk_check(vkGetPhysicalDeviceSurfaceSupportKHR(R.physicalDevice, R.queueFamily, R.drc.surface, &supported), "GamePad presentation support");
  if (!supported) exception_report::raise("GamePad window cannot present on the graphics queue");
  VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  vk_check(vkCreateSemaphore(R.device, &si, nullptr, &R.drc.acquired), "GamePad acquire semaphore");
  vk_check(vkCreateSemaphore(R.device, &si, nullptr, &R.drc.finished), "GamePad present semaphore");
  R.drc.window = window;
  make_swapchain(R.drc);
  gfx::g_has_drc_window = true;
}
#endif
void swap() {
  texture_budget_step();
  fallback_stats::begin_frame(R.frame);
  if (handoff_trace::enabled()) handoff_trace::set_producer_frame(R.frame);
  if(active_deferred_commands) {
    if (handoff_trace::enabled()) handoff_trace::emit(handoff_trace::Lane::Producer, handoff_trace::Kind::DrainBegin, 0, R.frame, uint64_t(handoff_trace::Reason::SwapPrefix));
    publish_or_finish_present_prefix(*active_deferred_commands);
    if (handoff_trace::enabled()) handoff_trace::emit(handoff_trace::Lane::Producer, handoff_trace::Kind::DrainEnd, 0, R.frame, uint64_t(handoff_trace::Reason::SwapPrefix));
  }
#if defined(WWHD_SDL_HOST) && !defined(__ANDROID__)
  attach_gamepad_window();
#endif
  for (Screen* s : {&R.tv, &R.drc, &stereoRight}) if (s->buildingScanDirty) {
    s->scan.swap(s->buildingScan);
    std::swap(s->scanDisplayExtent,s->buildingScanDisplayExtent);
    s->scanSrgb = s->buildingScanSrgb;
    s->buildingScanDirty = false;
  }
  service_screenshots();
  const auto swapPhaseStarted=std::chrono::steady_clock::now();
#ifdef __ANDROID__
  // Notify Java when it decided with another value: a setting transition, or the
  // native settings correcting the start-up choice Java read early (seedSavedOff).
  // Lifecycle/topology refreshes read the native value through publishAvailability().
  static int notifiedOff = -1;
  const int off = gfx::drc_screen_off();
  const int seen = javaSeenOff;
  if (seen == off) notifiedOff = off;
  else if (seen != -1 && notifiedOff != off) {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    jobject activity = env ? static_cast<jobject>(SDL_GetAndroidActivity()) : nullptr;
    if (activity) {
      jclass cls = env->GetObjectClass(activity);
      jmethodID method = cls ? env->GetMethodID(cls, "refreshGamePadDisplay", "()V") : nullptr;
      if (method) env->CallVoidMethod(activity, method);
      if (!env->ExceptionCheck() && method) notifiedOff = off;
      if (env->ExceptionCheck()) env->ExceptionClear();
      if (cls) env->DeleteLocalRef(cls);
      env->DeleteLocalRef(activity);
    }
  }
  update_secondary_surface();
  {
    std::lock_guard<std::mutex> guard(secondaryMutex);
    // GamePad screen off: the TV stays on the main screen and the second one is released (issue #222)
    bool wanted = gfx::g_has_drc_window && dual_display::requested_swap && !gfx::drc_screen_off();
    if (dual_display::active_swap.exchange(wanted) != wanted) {
      secondaryTouchBox = primaryTouchBox = {};
      ++touchLayoutGeneration;
      input::set_touch(false, 0, 0);
    }
  }
#endif
  service_captures();
  frame_dumps(R.frame + 1);
  // the layout of both pictures (GamePad window, picture-in-picture, automatic overlay, GamePad only)
  // comes from gfx/display_modes.cpp, which both window hosts and the Metal renderer share
  Surface *tvScan = R.tv.scan && R.tv.scan->image ? R.tv.scan.get() : nullptr;
  Surface *drcScan = R.drc.scan && R.drc.scan->image ? R.drc.scan.get() : nullptr;
  const auto tvDisplay = scan_display_extent(R.tv);
#ifdef WWHD_SDL_HOST
  gfx::g_filter = scale_filter();  // the SDL host keeps the scaling filter with the Vulkan settings
  const float layerW = float(R.tv.width.load()), layerH = float(R.tv.height.load());
#else
  const float layerW = float(R.tv.swapExtent.width), layerH = float(R.tv.swapExtent.height);
#endif
  // (sizes at the full internal resolution: dynamic resolution keeps the layout)
  gfx::PresentPlan plan = gfx::display_plan(
      tvScan, float(tvDisplay.width), float(tvDisplay.height),
      drcScan, nominal_width(drcScan), nominal_height(drcScan),
      layerW, layerH, R.frame + 1, stereo::frame.mode!=stereo::FullSBS);
#ifdef __ANDROID__
  // A physical second display owns the GamePad picture. Keep the requested
  // single-screen mode intact so disconnecting restores its normal fallback.
  if (gfx::g_has_drc_window) {
    plan.drc_window = true;
    plan.pip_on = false;
    plan.pip_wanted = false;
    plan.drc_only = false;
    plan.tv = gfx::display_layout(layerW, layerH,
        tvScan ? float(tvDisplay.width) : 1280,
        tvScan ? float(tvDisplay.height) : 720);
    if (drcScan && R.drc.width > 0 && R.drc.height > 0) {
      bool swapped = dual_display::active_swap;
      float w = swapped ? layerW : float(R.drc.width.load());
      float h = swapped ? layerH : float(R.drc.height.load());
      gfx::Box box = gfx::display_layout(w, h, nominal_width(drcScan), nominal_height(drcScan));
      std::lock_guard<std::mutex> guard(secondaryMutex);
      if (!secondaryChanged) {
        auto& touchBox = swapped ? primaryTouchBox : secondaryTouchBox;
        touchBox = {box.x / w, box.y / h, box.w / w, box.h / h};
      }
    }
  }
  // the view button's dot: 60 fps chosen (long press), green while drawn, yellow while paused
  if (perf_hint::fps60_chosen())
    plan.button_dot = interp::mode() != 0 ? 1 : 2;
#endif
  set_present_plan(&plan);
  // Full SBS uses a half-width logical canvas before placing the two eyes.
  // Publish that same layout for GamePad hit testing and the software pointer.
  gfx::PresentPlan inputPlan=plan;
  if(stereo::frame.mode==stereo::FullSBS) {
    inputPlan=gfx::display_plan_for(plan,layerW*.5f,layerH,
        float(tvDisplay.width),float(tvDisplay.height),
        nominal_width(drcScan),nominal_height(drcScan));
    gfx::display_input_plan(inputPlan);
  }
  // Menu and host toasts: built once at the selected window size, including present dumps
  overlay::guesthud::set_tv_region(inputPlan.tv.x,inputPlan.tv.y,inputPlan.tv.w,inputPlan.tv.h,!inputPlan.drc_only);
  const float overlayWidth = (plan.dw > 0 ? plan.dw : layerW) /
      (stereo::frame.mode == stereo::FullSBS ? 2.0f : 1.0f);
  using overlay::window_target::Window;
  Window requested = Window::TV;
  bool requireDrawable = false;
  float drcW = 0, drcH = 0;
#ifdef WWHD_SDL_HOST
  static const bool gamescopeOverlay = overlay::window_target::gamescope_session();
  requested = overlay::window_target::selected.load();
  requireDrawable = gamescopeOverlay && plan.drc_window;
  drcW = float(R.drc.width.load()); drcH = float(R.drc.height.load());
#endif
  const auto drawable = [](const Screen& screen) {
    return screen.visible && screen.scan && screen.scan->image;
  };
  const auto overlayFrame = overlay::window_target::build_frame(
      requested, plan.drc_window, requireDrawable,
      {overlayWidth, plan.dh > 0 ? plan.dh : layerH, drawable(R.tv)},
      {drcW, drcH, drawable(R.drc)},
      [](float width, float height) { return overlay::frame(width, height, overlay_renderer_init); },
      [] { overlay::set_open(false); });
#ifdef WWHD_SDL_HOST
  static const bool traceOverlay = getenv("WWHD_OVERLAY_TRACE") && std::string(getenv("WWHD_OVERLAY_TRACE")) == "1";
  static Window previousTarget = Window::None;
  static bool previousOpen = false;
  const bool menuOpen = overlay::is_open();
  if (traceOverlay && (overlayFrame.target != previousTarget || (menuOpen && !previousOpen)))
    LOG("[overlay] draw window %s, menu %s, frame %llu", overlayFrame.target == Window::TV ? "TV" : overlayFrame.target == Window::GamePad ? "GamePad" : "none",
        menuOpen ? "open" : "closed", (unsigned long long)(R.frame + 1));
  previousTarget = overlayFrame.target; previousOpen = menuOpen;
#endif
  set_overlay_draw(overlayFrame);
  compose_headless_diagnostic(R.tv);
  take_screenshots(plan);
#ifdef __ANDROID__
  prepare_secondary_snapshot();
#endif
  if(perf_enabled()) {
    ++swapPhaseTiming[0].count;
    swapPhaseTiming[0].ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now()-swapPhaseStarted).count();
  }
  evenPresentSwap = true;  // (repeated frames while a pipeline compiles are never held)
  try { timed_call(swapPhaseTiming[1],[&] { present(R.tv);return VK_SUCCESS; }); } catch (...) { evenPresentSwap = false; throw; }
  evenPresentSwap = false;
  if (plan.drc_window)
    timed_call(swapPhaseTiming[2],[&] { present(R.drc);return VK_SUCCESS; });
  // Asynchronous presentation is retired by the ring's fences. Present dumps and captures
  // read back through flush(). Both window hosts: the AppKit host presents to its CAMetalLayers
  // through the same swapchain path.
  timed_call(swapPhaseTiming[3],[&] {
    if (async_present()) flush_async();
    else flush();
    return VK_SUCCESS;
  });
#ifdef __ANDROID__
  if(isolatedSecondary && isolatedSecondary->published() && isolatedSnapshotSubmission) {
    const auto [index,serial]=*isolatedSnapshotSubmission;
    auto& producer=R.submissions[index];
    if (producer.pending && producer.serial==serial) wait_record_submission(producer);
    // Do not export a pending sync FD while the driver may serialize export
    // against later primary submissions. Poll on this thread; a retired/reused
    // slot also proves that this snapshot's original submission completed.
    const VkResult complete=!producer.pending || producer.serial!=serial ? VK_SUCCESS :
        vkGetFenceStatus(R.device,producer.fence);
    if(complete==VK_SUCCESS) {
      isolatedSecondary->present(isolatedSourceLinear,scale_filter(),fxaa_enabled());
      isolatedSnapshotSubmission.reset();
      R.secondaryPresentPending=true;R.secondaryAcquirePending=false;
    } else if(complete!=VK_NOT_READY)vk_check(complete,"isolated snapshot producer completion");
  }
#endif
  const auto swapBookkeepingStarted=std::chrono::steady_clock::now();
  if (perf_enabled()) {
    // Work the GPU has not finished at this swap: a fence query only after the record worker's
    // CPU acknowledgment (a slot still queued on the worker counts as in flight)
    unsigned submissions=0;uint64_t oldest=R.frame;
    for (auto& slot:R.submissions) {
      if (!slot.pending) continue;
      const bool queued=slot.recordWorkerSerial && !record_commands_emitted(slot.recordWorkerSerial);
      if (!queued && vkGetFenceStatus(R.device,slot.fence)!=VK_NOT_READY) continue;
      ++submissions;oldest=std::min(oldest,slot.recordedFrame);
    }
    const unsigned frames=submissions ? unsigned(R.frame-oldest+1) : 0;
    ++inFlight.samples;inFlight.submissions+=submissions;inFlight.frames+=frames;
    inFlight.maxSubmissions=std::max(inFlight.maxSubmissions,submissions);
    inFlight.maxFrames=std::max(inFlight.maxFrames,frames);
  }
  completedPresentPlan = plan;
  completedPresentPlanValid = tvScan != nullptr;
  lastNewPresentNs = stutter::now();
  nextPipelineRepeatNs = lastNewPresentNs + 16666667;
  for (auto &path : gfx::display_take_present_dumps()) {
    if (!tvScan || plan.dw < 1 || plan.dh < 1)
      continue;
    try {
      write_rgba_png(path, uint32_t(plan.dw), uint32_t(plan.dh),
                     compose_offscreen(R.tv, uint32_t(plan.dw), uint32_t(plan.dh), R.tv.scanSrgb));
      // the GamePad window's size: its swapchain, or (SDL host test runs with hidden windows, which
      // present nothing) the window's pixel size
      uint32_t drcW = R.drc.swapchain ? R.drc.swapExtent.width : 0, drcH = R.drc.swapchain ? R.drc.swapExtent.height : 0;
#ifdef WWHD_SDL_HOST
      if (!R.drc.swapchain && R.drc.window)
        drcW = uint32_t(std::max(0, R.drc.width.load())), drcH = uint32_t(std::max(0, R.drc.height.load()));
#endif
      if (plan.drc_window && drcScan && drcW && drcH) {
        std::string p = path;
        size_t dot = p.rfind(".png");
        p.insert(dot == std::string::npos ? p.size() : dot, "_drc");
        write_rgba_png(p, drcW, drcH, compose_offscreen(R.drc, drcW, drcH, R.drc.scanSrgb));
      }
      gfx::display_log_present_dump(path, plan, float(tvScan->extent.width), float(tvScan->extent.height));
    } catch (const std::exception &e) {
      LOG("[gfx] present dump %s failed: %s", path.c_str(), e.what());
    }
  }
  set_present_plan(nullptr);
  service_screenshots();  // this frame's: the submission that holds them
  fallback_stats::complete_frame(R.frame);
  stutter::frame(R.frame);
  std::atomic_ref<uint64_t>(R.frame).fetch_add(1);
  fallback_stats::begin_frame(R.frame); // Attribute early next-frame state drains exactly.
  R.completed = R.frame;
#ifdef __ANDROID__
  if (tvScan) drivers::frame_done();
#endif
  buffer_cache_end_frame();
  report_gpu_timestamps();
  perf_hint::frame_done();
  checkpoint_pipeline_cache();
  vk::checkpoint_shader_cache(R.frame);
  const uint64_t memoryInterval=memory_log_interval();
  if (memoryInterval && R.frame%memoryInterval==0) {
    const auto memory=vk::shader_memory_stats();
    uint64_t imageBytes=0,images=0,uploadBytes=0;
    std::unordered_set<VkImage> seenImages;
    auto countImage=[&](const Surface* surface) {
      for (;surface;surface=surface->mipChain.get()) {
        if (surface->image && seenImages.insert(surface->image).second) {
          ++images; imageBytes+=surface->allocationBytes;
        }
        for (const auto& kept:surface->spares)  // dynamic resolution's kept images
          if (kept->image && seenImages.insert(kept->image).second) { ++images; imageBytes+=kept->allocationBytes; }
      }
    };
    for (const auto& [address,surface]:R.surfaces) countImage(surface.get());
    std::unordered_set<VkBuffer> seenUploads;
    auto countUploads=[&](const auto& blocks) {
      for (const auto& block:blocks)
        if (seenUploads.insert(block.buffer.buffer).second) uploadBytes+=block.buffer.size;
    };
    countUploads(R.uploadBlocks);
    for (const auto& submission:R.submissions) countUploads(submission.uploadBlocks);
    constexpr double mib=1024.0*1024.0;
    LOG("[gfx] memory frame %llu RSS %.2f MiB (0=unavailable)",
        (unsigned long long)R.frame,resident_bytes()/mib);
    LOG("[gfx] memory cached surfaces %llu images %llu allocated %.2f MiB",
        (unsigned long long)R.surfaces.size(),(unsigned long long)images,imageBytes/mib);
    LOG("[gfx] memory upload blocks %llu %.2f MiB",
        (unsigned long long)seenUploads.size(),uploadBytes/mib);
    LOG("[gfx] memory shader payload: %llu shaders decompiler-text %.2f MiB GLSL-capacity %.2f MiB verify-capacity %.2f MiB SPIRV-capacity %.2f MiB",
        (unsigned long long)memory.shaders,memory.decompilerText/mib,memory.glslCapacity/mib,
        memory.verifyCapacity/mib,memory.spirvCapacity/mib);
    LOG("[gfx] memory disk %llu unshared-SPIRV-capacity %.2f MiB pending-compiles %llu retained-source-capacity %.2f MiB",
        (unsigned long long)memory.diskEntries,memory.diskSpirvCapacity/mib,
        (unsigned long long)memory.pendingCompiles,memory.retainedSourceCapacity/mib);
    LOG("[gfx] memory scope: payload/capacity excludes compiler results, disk snapshots, metadata and allocator overhead");
    LOG("[gfx] memory scope: cached image allocations exclude retired, non-cache and driver images");
    // Excludes map/metadata/allocator overhead, compiler tickets/results,
    // async disk snapshots, non-cache/retired images and driver allocations.
  }
  latch_res_scale();
  if(perf_enabled()) {
    ++swapPhaseTiming[4].count;
    swapPhaseTiming[4].ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now()-swapBookkeepingStarted).count();
  }
  if (perf_enabled() || cpu_only_stats_enabled()) {
    static auto start = std::chrono::steady_clock::now();
    static auto previousSwap = start;
    static bool havePreviousSwap = false;
    static std::array<double, 120> swapIntervals{};
    static size_t intervalCount = 0, slowIntervals = 0;
    static uint64_t frame=0,draws=0,bytes=0,allocs=0,passes=0,framebuffers=0;
    static uint64_t descriptorLookups=0,descriptorHits=0,descriptorAllocations=0,descriptorFastHits=0;
    static uint64_t vertexDeclaredBytes=0,vertexCopiedBytes=0;
    static uint64_t vertexReuseChecks=0,vertexReuseHits=0,vertexReuseBytes=0,vertexReuseCompareNs=0;
    static uint64_t batchSubmissions=0,fetchLookups=0,fetchLastHits=0;
    static uint64_t stateHashLookups=0,stateHashMemoHits=0,stateHashBytes=0;
    static uint64_t vertexBindCalls=0,vertexBindSkips=0;
    static uint64_t vertexHistoryRequests=0,vertexHistoryMatches=0,vertexHistoryBytes=0;
    static uint64_t vertexHistoryReuseChecks=0,vertexHistoryReuseHits=0,vertexHistoryReuseBytes=0;
    static std::array<uint64_t,7> vertexHistoryDistances{};
    static Renderer::CpuPreparationStats previousPreparation;
    static SparseHashStats previousSparse;
    static gx2::ShaderKeyDirtyStats previousShaderDirty;
#ifndef _WIN32
    auto threadCpuNs = []() -> uint64_t {
      timespec time{};
      if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time)) return 0;
      return uint64_t(time.tv_sec) * 1000000000ull + uint64_t(time.tv_nsec);
    };
    static uint64_t previousThreadCpu = threadCpuNs();
#endif
    auto now=std::chrono::steady_clock::now();
    if (havePreviousSwap && intervalCount < swapIntervals.size()) {
      double ms = std::chrono::duration<double, std::milli>(now-previousSwap).count();
      swapIntervals[intervalCount++] = ms;
      if (ms > 40.0) ++slowIntervals;
    }
    previousSwap=now;havePreviousSwap=true;
    if (R.frame-frame>=120) {
      double seconds=std::chrono::duration<double>(now-start).count();
#ifndef _WIN32
      const uint64_t currentThreadCpu = threadCpuNs();
      if (previousThreadCpu && currentThreadCpu >= previousThreadCpu)
        LOG("[vulkan thread CPU] %.3f ms/frame (excludes sleeping and GPU waits)",
            double(currentThreadCpu - previousThreadCpu) / 1e6 / (R.frame - frame));
      previousThreadCpu = currentThreadCpu;
#endif
      LOG("[vulkan perf] frame %llu %.2f fps, %.1f draws/frame, %.2f MiB uploads/frame, %llu new upload blocks, %.1f passes/frame",
          (unsigned long long)R.frame,(R.frame-frame)/seconds,
          double(R.drawCount-draws)/(R.frame-frame),double(R.uploadBytes-bytes)/(R.frame-frame)/(1<<20),
          (unsigned long long)(R.uploadAllocations-allocs), double(R.renderPassCount-passes)/(R.frame-frame));
      if (R.renderPasses) {
        const auto created = pass_caches().framebuffers_created();
        LOG("[vulkan render passes] %.3f framebuffers created/frame, %zu cached",
            double(created-framebuffers)/(R.frame-frame), pass_caches().framebuffer_count());
        framebuffers=created;
      }
      if (intervalCount) {
        std::sort(swapIntervals.begin(), swapIntervals.begin()+intervalCount);
        // Nearest-rank percentiles; the first report contains 119 intervals.
        size_t p50=(intervalCount+1)/2-1, p95=(intervalCount*95+99)/100-1;
        LOG("[vulkan pacing] %llu intervals p50 %.2f ms p95 %.2f ms max %.2f ms; >40 ms %llu",
            (unsigned long long)intervalCount,swapIntervals[p50],swapIntervals[p95],
            swapIntervals[intervalCount-1],(unsigned long long)slowIntervals);
      }
      intervalCount=0;slowIntervals=0;
      auto ss=vk::shader_stats();
      LOG("[vulkan shaders] lookups %llu last hits %llu variants %llu compiles %llu total compile %.1f ms; %llu programs, %llu keys, %llu shaders (%llu keys shared), %llu sharing a module",
          (unsigned long long)ss.lookups,(unsigned long long)ss.lastHits,(unsigned long long)ss.variantHits,(unsigned long long)ss.compiles,ss.compileNs/1e6,
          (unsigned long long)ss.programs,(unsigned long long)ss.variantKeys,(unsigned long long)ss.shaders,(unsigned long long)ss.variantAliases,(unsigned long long)ss.moduleAliases);
      if (ss.verifyChecks || ss.verifySplitKeys)
        LOG("[vulkan shader key verify] %llu pre-narrowing keys checked, %llu violations, %llu split; %.1f ms",
            (unsigned long long)ss.verifyChecks,(unsigned long long)ss.verifyViolations,
            (unsigned long long)ss.verifySplitKeys,ss.verifyNs/1e6);
      LOG("[vulkan shader disk] hits %llu; memory reuses %llu; SPIR-V compiles %llu %.1f ms; decompile %.1f ms; loads %llu %.1f ms; saves %llu worker %.1f ms %.2f MiB; snapshots %.1f ms",
          (unsigned long long)ss.diskHits,(unsigned long long)ss.spirvReuseHits,(unsigned long long)ss.spirvCompiles,ss.spirvCompileNs/1e6,
          ss.decompileNs/1e6,(unsigned long long)ss.diskLoads,ss.diskLoadNs/1e6,
          (unsigned long long)ss.diskSaves,ss.diskSaveNs/1e6,ss.diskSavedBytes/double(1<<20),ss.diskSnapshotNs/1e6);
      {
        uint64_t promotions=0,promotedHits=0; double promotedMs=0;
        pipeline_promotion_stats(promotions,promotedHits,promotedMs);
        LOG("[vulkan pipelines] creates %llu total %.1f ms; new keys %llu, %llu of them in the recipe database; "
            "%llu demands waited for the running warm-up compile (%llu got it, %.1f ms)",
            (unsigned long long)R.pipelineCreates,R.pipelineCreateNs/1e6,
            (unsigned long long)R.pipelineKeysNew,(unsigned long long)R.pipelineKeysKnown,
            (unsigned long long)promotions,(unsigned long long)promotedHits,promotedMs);
      }
      {
        // texture change detection (surfaces.cpp upload_surface, write_watch.h)
        extern uint64_t g_stat_full_checks, g_stat_uploads;
        extern uint64_t g_stat_staging_ring, g_stat_staging_ring_bytes, g_stat_staging_buffers, g_stat_staging_buffer_bytes;
        extern uint64_t g_stat_tex_evictions, g_stat_tex_evicted_bytes, g_stat_tex_restores, g_stat_tex_resident_bytes;
        LOG("[vulkan textures] budget: %.1f MiB of CPU textures resident; %llu released (%.1f MiB), %llu made again",
            g_stat_tex_resident_bytes/1048576.0,(unsigned long long)g_stat_tex_evictions,g_stat_tex_evicted_bytes/1048576.0,
            (unsigned long long)g_stat_tex_restores);
        g_stat_tex_evictions=g_stat_tex_evicted_bytes=g_stat_tex_restores=0;
        static uint64_t checks=0, uploads=0;
        uint64_t faults, protectedPages;
        wwatch::take_stats(faults, protectedPages);
        LOG("[vulkan textures] %llu full checks, %llu uploads, %llu page write faults, %llu pages protected; staging: "
            "%llu levels from the upload arena (%.1f MiB), %llu own buffers (%.1f MiB)",
            (unsigned long long)(g_stat_full_checks-checks),(unsigned long long)(g_stat_uploads-uploads),
            (unsigned long long)faults,(unsigned long long)protectedPages,
            (unsigned long long)g_stat_staging_ring,g_stat_staging_ring_bytes/1048576.0,
            (unsigned long long)g_stat_staging_buffers,g_stat_staging_buffer_bytes/1048576.0);
        checks=g_stat_full_checks;uploads=g_stat_uploads;
        g_stat_staging_ring=g_stat_staging_ring_bytes=g_stat_staging_buffers=g_stat_staging_buffer_bytes=0;
      }
      buffer_cache_report(double(R.frame-frame));
      // CPU-only reports leave per-draw counters/comparison clocks disabled.
      if (perf_enabled()) {
      double frames = double(R.frame-frame);
      LOG("[vulkan fetch memo] %.1f lookups/frame %.1f last hits/frame",
          (ss.fetchLookups-fetchLookups)/frames,(ss.fetchLastHits-fetchLastHits)/frames);
      fetchLookups=ss.fetchLookups;fetchLastHits=ss.fetchLastHits;
      LOG("[vulkan state hash memo] %.1f lookups/frame %.1f hits/frame %.3f MiB hashed/frame",
          (ss.stateHashLookups-stateHashLookups)/frames,
          (ss.stateHashMemoHits-stateHashMemoHits)/frames,
          (ss.stateHashBytes-stateHashBytes)/frames/(1<<20));
      stateHashLookups=ss.stateHashLookups;
      stateHashMemoHits=ss.stateHashMemoHits;stateHashBytes=ss.stateHashBytes;
      uint64_t currentBatchSubmissions=draw_batch_submissions();
      LOG("[vulkan draw batches] %.2f extra submissions/frame",
          (currentBatchSubmissions-batchSubmissions)/frames);
      batchSubmissions=currentBatchSubmissions;
      LOG("[vulkan vertex snapshots] declared %.2f MiB/frame; prepared %.2f MiB/frame",
          (R.vertexDeclaredBytes-vertexDeclaredBytes)/frames/(1<<20),
          (R.vertexCopiedBytes-vertexCopiedBytes)/frames/(1<<20));
      vertexDeclaredBytes=R.vertexDeclaredBytes;vertexCopiedBytes=R.vertexCopiedBytes;
      LOG("[vulkan vertex binds] %.1f actual/frame %.1f skipped/frame",
          (R.vertexBindCalls-vertexBindCalls)/frames,
          (R.vertexBindSkips-vertexBindSkips)/frames);
      vertexBindCalls=R.vertexBindCalls;vertexBindSkips=R.vertexBindSkips;
      LOG("[vulkan vertex history] %.1f requests/frame %.1f nonconsecutive keys/frame %.3f MiB potential/frame; distances 1..7 %.1f %.1f %.1f %.1f %.1f %.1f %.1f",
          (R.vertexHistoryRequests-vertexHistoryRequests)/frames,
          (R.vertexHistoryMatches-vertexHistoryMatches)/frames,
          (R.vertexHistoryBytes-vertexHistoryBytes)/frames/(1<<20),
          (R.vertexHistoryDistances[0]-vertexHistoryDistances[0])/frames,
          (R.vertexHistoryDistances[1]-vertexHistoryDistances[1])/frames,
          (R.vertexHistoryDistances[2]-vertexHistoryDistances[2])/frames,
          (R.vertexHistoryDistances[3]-vertexHistoryDistances[3])/frames,
          (R.vertexHistoryDistances[4]-vertexHistoryDistances[4])/frames,
          (R.vertexHistoryDistances[5]-vertexHistoryDistances[5])/frames,
          (R.vertexHistoryDistances[6]-vertexHistoryDistances[6])/frames);
      vertexHistoryRequests=R.vertexHistoryRequests;vertexHistoryMatches=R.vertexHistoryMatches;
      vertexHistoryBytes=R.vertexHistoryBytes;vertexHistoryDistances=R.vertexHistoryDistances;
      LOG("[vulkan vertex history reuse] %.1f checks/frame %.1f hits/frame %.3f MiB reused/frame",
          (R.vertexHistoryReuseChecks-vertexHistoryReuseChecks)/frames,
          (R.vertexHistoryReuseHits-vertexHistoryReuseHits)/frames,
          (R.vertexHistoryReuseBytes-vertexHistoryReuseBytes)/frames/(1<<20));
      vertexHistoryReuseChecks=R.vertexHistoryReuseChecks;
      vertexHistoryReuseHits=R.vertexHistoryReuseHits;vertexHistoryReuseBytes=R.vertexHistoryReuseBytes;
      LOG("[vulkan vertex reuse] %.1f checks/frame; %.1f hits/frame; %.2f MiB reused/frame; compare %.3f ms/frame",
          (R.vertexReuseChecks-vertexReuseChecks)/frames,
          (R.vertexReuseHits-vertexReuseHits)/frames,
          (R.vertexReuseBytes-vertexReuseBytes)/frames/(1<<20),
          (R.vertexReuseCompareNs-vertexReuseCompareNs)/frames/1e6);
      vertexReuseChecks=R.vertexReuseChecks;vertexReuseHits=R.vertexReuseHits;
      vertexReuseBytes=R.vertexReuseBytes;vertexReuseCompareNs=R.vertexReuseCompareNs;
      uint64_t descriptorChecks=R.descriptorLookups-descriptorLookups;
      uint64_t descriptorReuses=R.descriptorCacheHits-descriptorHits;
      LOG("[vulkan descriptors] %.1f lookups/frame; %.1f allocations/frame; %.1f%% reused; %.1f%% consecutive",
          descriptorChecks/frames,(R.descriptorAllocations-descriptorAllocations)/frames,
          descriptorChecks ? 100.0*descriptorReuses/descriptorChecks : 0.0,
          descriptorChecks ? 100.0*(R.descriptorFastHits-descriptorFastHits)/descriptorChecks : 0.0);
      descriptorLookups=R.descriptorLookups;descriptorHits=R.descriptorCacheHits;
      descriptorAllocations=R.descriptorAllocations;descriptorFastHits=R.descriptorFastHits;
      const auto& prep=R.cpuPreparation;
      const auto sparse=sparse_hash_stats();
      LOG("[vulkan sparse hash memo] %.1f checks/frame %.1f hits/frame %.3f MiB fresh samples/frame %.1f mixed words/frame %.1f overflow/frame",
          (sparse.checks-previousSparse.checks)/frames,
          (sparse.memoHits-previousSparse.memoHits)/frames,
          (sparse.sampleBytes-previousSparse.sampleBytes)/frames/(1<<20),
          (sparse.mixerWords-previousSparse.mixerWords)/frames,
          (sparse.overflows-previousSparse.overflows)/frames);
      previousSparse=sparse;
      const auto dirty=gx2::shader_key_dirty_stats();
      LOG("[vulkan shader key dirty] %.1f changed batches/frame %.1f baseline bumps/frame %.1f actual bumps/frame %.1f avoided/frame %.1f masked words/frame",
          (dirty.changedBatches-previousShaderDirty.changedBatches)/frames,
          (dirty.baselineWouldBumps-previousShaderDirty.baselineWouldBumps)/frames,
          (dirty.actualBumps-previousShaderDirty.actualBumps)/frames,
          (dirty.avoidedBumps-previousShaderDirty.avoidedBumps)/frames,
          (dirty.maskedWords-previousShaderDirty.maskedWords)/frames);
      previousShaderDirty=dirty;
      LOG("[vulkan sampler memo] %.1f requests/frame %.1f hits/frame %.1f map lookups/frame",
          (prep.samplerRequests-previousPreparation.samplerRequests)/frames,
          (prep.samplerMemoHits-previousPreparation.samplerMemoHits)/frames,
          (prep.samplerMapLookups-previousPreparation.samplerMapLookups)/frames);
      LOG("[vulkan uniform preparation] %.1f packs/frame %.3f MiB/frame; %llu scratch growths %.3f KiB added; %.1f snapshot requests/frame %.3f MiB/frame",
          (prep.packCalls-previousPreparation.packCalls)/frames,
          (prep.packBytes-previousPreparation.packBytes)/frames/(1<<20),
          (unsigned long long)(prep.packCapacityGrowths-previousPreparation.packCapacityGrowths),
          (prep.packCapacityGrowthBytes-previousPreparation.packCapacityGrowthBytes)/1024.0,
          (prep.uniformSnapshotCalls-previousPreparation.uniformSnapshotCalls)/frames,
          (prep.uniformSnapshotBytes-previousPreparation.uniformSnapshotBytes)/frames/(1<<20));
      LOG("[vulkan uniform reuse] %.1f checks/frame %.1f comparisons/frame %.1f hits/frame %.3f MiB avoided/frame",
          (prep.uniformReuseChecks-previousPreparation.uniformReuseChecks)/frames,
          (prep.uniformReuseComparisons-previousPreparation.uniformReuseComparisons)/frames,
          (prep.uniformReuseHits-previousPreparation.uniformReuseHits)/frames,
          (prep.uniformReuseBytes-previousPreparation.uniformReuseBytes)/frames/(1<<20));
      const uint64_t candidates=prep.descriptorBindCandidates-previousPreparation.descriptorBindCandidates;
      const double percentage=candidates?100.0/candidates:0.0;
      LOG("[vulkan bind opportunities] %.1f candidates/frame; exact whole %.2f%% VS %.2f%% PS %.2f%%; %.1f actual binds/frame %.1f skipped sets/frame",
          candidates/frames,(prep.descriptorWholeMatches-previousPreparation.descriptorWholeMatches)*percentage,
          (prep.descriptorStageMatches[0]-previousPreparation.descriptorStageMatches[0])*percentage,
          (prep.descriptorStageMatches[1]-previousPreparation.descriptorStageMatches[1])*percentage,
          (prep.descriptorBindCalls-previousPreparation.descriptorBindCalls)/frames,
          (prep.descriptorSkippedSets-previousPreparation.descriptorSkippedSets)/frames);
      LOG("[vulkan pipeline lookup] %.1f requests/frame; %.1f last hits; %.1f lookaside hits; %.1f map lookups",
          (prep.pipelineLookups-previousPreparation.pipelineLookups)/frames,
          (prep.pipelineLastHits-previousPreparation.pipelineLastHits)/frames,
          (prep.pipelineLookasideHits-previousPreparation.pipelineLookasideHits)/frames,
          (prep.pipelineMapLookups-previousPreparation.pipelineMapLookups)/frames);
      previousPreparation=prep;
      LOG("[vulkan waits] submit %.2f/frame %.2f ms/frame; present submit %.2f/frame %.2f ms/frame",
          submitWait.count/frames, submitWait.ns/1e6/frames,
          presentSubmitWait.count/frames, presentSubmitWait.ns/1e6/frames);
      LOG("[vulkan submission ring] %zu slots (%u at start); slot reuse waits %.2f/frame %.3f ms/frame; in flight at swap: submissions %.2f avg %u max, frames %.2f avg %u max",
          R.submissions.size(),submissionPoolSlots,slotReuseWait.count/frames,slotReuseWait.ns/1e6/frames,
          inFlight.samples ? double(inFlight.submissions)/inFlight.samples : 0.0,inFlight.maxSubmissions,
          inFlight.samples ? double(inFlight.frames)/inFlight.samples : 0.0,inFlight.maxFrames);
      slotReuseWait={};inFlight={};
      LOG("[vulkan queue submit] %.2f/frame %.2f ms/frame",
          queueSubmitTiming.count/frames, queueSubmitTiming.ns/1e6/frames);
      LOG("[vulkan swap phases] setup %.2f ms/frame; primary %.2f; secondary %.2f; flush %.2f; bookkeeping %.2f",
          swapPhaseTiming[0].ns/1e6/frames,swapPhaseTiming[1].ns/1e6/frames,
          swapPhaseTiming[2].ns/1e6/frames,swapPhaseTiming[3].ns/1e6/frames,swapPhaseTiming[4].ns/1e6/frames);
      swapPhaseTiming={};
      LOG("[vulkan command end] %.2f/frame %.2f ms/frame",
          commandEndTiming.count/frames,commandEndTiming.ns/1e6/frames);
      commandEndTiming={};
      LOG("[vulkan retirement CPU] fence status %.2f/frame %.2f ms/frame; reset %.2f/frame %.2f ms/frame; cleanup %.2f/frame %.2f ms/frame",
          fenceStatusTiming.count/frames,fenceStatusTiming.ns/1e6/frames,
          fenceResetTiming.count/frames,fenceResetTiming.ns/1e6/frames,
          retirementCleanupTiming.count/frames,retirementCleanupTiming.ns/1e6/frames);
      fenceStatusTiming={};fenceResetTiming={};retirementCleanupTiming={};
      for (int screen = 0; screen < 2; ++screen) {
        auto& t = screenTiming[screen];
        LOG("[vulkan compose %s] %.2f/frame %.2f ms/frame",
            screen ? "DRC" : "TV", t.compose.count/frames, t.compose.ns/1e6/frames);
        LOG("[vulkan waits %s] acquire %.2f/frame %.2f ms/frame; present %.2f/frame %.2f ms/frame; queue idle %.2f/frame %.2f ms/frame",
            screen ? "DRC" : "TV", t.acquire.count/frames, t.acquire.ns/1e6/frames,
            t.present.count/frames, t.present.ns/1e6/frames,
            t.idle.count/frames, t.idle.ns/1e6/frames);
        t = {};
      }
      submitWait = {}; presentSubmitWait = {}; queueSubmitTiming = {};
      }
      start=now;frame=R.frame;draws=R.drawCount;bytes=R.uploadBytes;allocs=R.uploadAllocations;passes=R.renderPassCount;
    }
  }
}
void set_tv_format(uint32_t format, bool tv) {
  (tv ? R.tv : R.drc).srgb = (format & 0x400) != 0;
}
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_message(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT *data, void *) {
  const std::string message = std::string("[vulkan validation ") +
      (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "error" : "warning") + "] " +
      (data->pMessageIdName ? data->pMessageIdName : "validation") + ": " +
      (data->pMessage ? data->pMessage : "");
  validationDiagnostics.record(message, [](std::string_view text) {
    LOG("%.*s", int(text.size()), text.data());
  });
  return VK_FALSE;
}
// Test aids for the paths of older drivers (docs/vulkan.md): WWHD_VK_FORCE_API=1.2 treats every GPU as
// if it reported that Vulkan version (Vulkan 1.3 GPUs then take the VK_KHR_dynamic_rendering path);
// WWHD_VK_HIDE_EXTENSIONS=VK_KHR_dynamic_rendering,... hides device extensions from the renderer.
static uint32_t device_api_version(const VkPhysicalDeviceProperties &p) {
  static const uint32_t forced = [] {
    unsigned major, minor;
    const char *e = getenv("WWHD_VK_FORCE_API");
    return e && sscanf(e, "%u.%u", &major, &minor) == 2 ? VK_MAKE_API_VERSION(0, major, minor, 0) : ~0u;
  }();
  return forced < VK_MAKE_API_VERSION(0, VK_API_VERSION_MAJOR(p.apiVersion), VK_API_VERSION_MINOR(p.apiVersion), 0)
             ? forced : p.apiVersion;
}
static std::vector<VkExtensionProperties> device_extensions(VkPhysicalDevice device) {
  uint32_t n = 0;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &n, nullptr);
  std::vector<VkExtensionProperties> des(n);
  vkEnumerateDeviceExtensionProperties(device, nullptr, &n, des.data());
  des.resize(n);
  if (const char *hide = getenv("WWHD_VK_HIDE_EXTENSIONS"))
    std::erase_if(des, [&](const VkExtensionProperties &e) {
      for (const char *p = hide; *p;) {
        size_t len = strcspn(p, ",");
        if (len == strlen(e.extensionName) && !strncmp(p, e.extensionName, len))
          return true;
        p += len + (p[len] == ',');
      }
      return false;
    });
  return des;
}
static std::string version_text(uint32_t v) {
  return std::to_string(VK_API_VERSION_MAJOR(v)) + "." + std::to_string(VK_API_VERSION_MINOR(v)) + "." +
         std::to_string(VK_API_VERSION_PATCH(v));
}
// driverVersion: vendor-specific packing, decoded as vulkaninfo does (report_header.cpp)
#ifdef _WIN32
static constexpr bool kWindows = true;
#else
static constexpr bool kWindows = false;
#endif
static std::string driver_version_text(const VkPhysicalDeviceProperties &p) {
  return reporthdr::driver_version(p.vendorID, p.driverVersion, kWindows);
}
// the GPU line of the performance report header: name, driver, Vulkan version ("" before init)
std::string device_description() {
  if (!R.properties.deviceName[0]) return "";
  return reporthdr::vulkan_gpu(R.properties.deviceName, R.properties.vendorID, R.properties.driverVersion,
                               R.properties.apiVersion, kWindows) + "; rendering path: " +
      rendering_path_name(R.renderPasses ? RenderingPath::RenderPass :
          R.dynamicRenderingKHR ? RenderingPath::KHR : RenderingPath::Core) + " (" + R.renderingReason + ")";
}
// Probe dynamic-rendering capabilities independently of the selected fallback.
static RenderingChoice rendering_path(VkPhysicalDevice device, const VkPhysicalDeviceProperties &properties,
                                     const std::vector<VkExtensionProperties> &extensions) {
  RenderingCapabilities caps{device_api_version(properties)};
  if (caps.api >= VK_API_VERSION_1_3) {
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &f13;
    vkGetPhysicalDeviceFeatures2(device, &features);
    caps.core = f13.dynamicRendering;
  }
  if (!caps.core && caps.api >= VK_API_VERSION_1_1 &&
      has_extension(extensions, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) &&
      (caps.api >= VK_API_VERSION_1_2 || (has_extension(extensions, VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME) &&
                                        has_extension(extensions, VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME)))) {
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &dynamic;
    vkGetPhysicalDeviceFeatures2(device, &features);
    caps.khr = dynamic.dynamicRendering;
  }
#ifdef _WIN32
  constexpr bool windows = true;
#else
  constexpr bool windows = false;
#endif
  return choose_rendering_path(caps, properties.vendorID, properties.driverVersion, windows,
                               getenv("WWHD_VK_FORCE_RENDERPASS"));
}
static const char kUpdateDriver[] =
#ifdef _WIN32
    "Install the newest graphics driver for this GPU (from the AMD, Intel or NVIDIA website, or through "
    "Windows Update) and start the game again. "
#elif defined(__APPLE__)
    "Vulkan on a Mac needs MoltenVK and the Vulkan loader (the release ships both next to the game; "
    "source builds: brew install vulkan-loader molten-vk). "
#else
    "Install the newest graphics driver for this GPU (on Linux: an up-to-date Mesa, or the vendor's "
    "driver) and start the game again. "
#endif
    "If no newer driver exists, this GPU cannot run the game's Vulkan renderer.";
// The Vulkan layers the loader offers, once in the log (so also in a crash log's last lines).
// Overlays install implicit layers that load into every Vulkan program (Steam, Discord, RivaTuner,
// OBS, Overwolf, ReShade...); a crash inside one then shows up in the crash log's module names, and
// this line says which were around. The list holds the explicit layers as well; implicit ones are
// active unless their disable variable is set.
static void log_instance_layers() {
  if (!vkEnumerateInstanceLayerProperties)
    return;
  uint32_t count = 0;
  if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS)
    return;
  std::vector<VkLayerProperties> layers(count);
  if (count && vkEnumerateInstanceLayerProperties(&count, layers.data()) < VK_SUCCESS)
    return;
  layers.resize(count);
  // one log line holds 240 characters: several lines of up to 200 (each layer name is <= 256)
  std::string line;
  int lines = 0;
  for (const VkLayerProperties &l : layers) {
    char one[300];
    snprintf(one, sizeof one, "%s%s (%u.%u.%u)", line.empty() ? "" : ", ", l.layerName,
             VK_API_VERSION_MAJOR(l.specVersion), VK_API_VERSION_MINOR(l.specVersion),
             VK_API_VERSION_PATCH(l.specVersion));
    if (!line.empty() && line.size() + strlen(one) > 200) {
      LOG("[vulkan] layers: %s,", line.c_str());
      line.clear();
      if (++lines == 4) {
        LOG("[vulkan] layers: ... %u in all", count);
        return;
      }
      snprintf(one, sizeof one, "%s (%u.%u.%u)", l.layerName, VK_API_VERSION_MAJOR(l.specVersion),
               VK_API_VERSION_MINOR(l.specVersion), VK_API_VERSION_PATCH(l.specVersion));
    }
    line += one;
  }
  LOG("[vulkan] layers: %s", count ? line.c_str() : "none");
}

// Instance, device, submission slots and swapchains. `extensions`: the window system's instance
// extensions; `create_surfaces` makes R.tv.surface / R.drc.surface once the instance exists.
// The host has loaded the Vulkan loader's global functions (load_global_functions).
static void init_device(std::vector<const char *> extensions,
                        const std::function<void()> &create_surfaces) {
  load_device_setting();
  reset_pipeline_lookup_cache();
#ifdef __APPLE__
  // This draw-heavy workload is faster with direct Metal resource bindings.
  // Respect an explicit MoltenVK override for devices/scenes needing larger
  // argument-buffer resource limits. Set before the loader loads the driver.
  setenv("MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS", "0", 0);
  // Immediate encoding overlaps recording with submitted GPU work. Every
  // recording entrypoint has a host autorelease pool; explicit overrides win.
  setenv("MVK_CONFIG_PREFILL_METAL_COMMAND_BUFFERS", "3", 0);
#endif
  uint32_t n;
  uint32_t loaderVersion = VK_API_VERSION_1_0;
  if (vkEnumerateInstanceVersion)
    vkEnumerateInstanceVersion(&loaderVersion);
  if (loaderVersion < VK_API_VERSION_1_1)
    exception_report::raise("The Vulkan runtime on this computer supports only Vulkan " +
                             version_text(loaderVersion) + "; the game needs Vulkan 1.1 or newer.\n\n" +
                             kUpdateDriver);
  log_instance_layers();
  uint32_t en = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &en, nullptr);
  std::vector<VkExtensionProperties> ies(en);
  vkEnumerateInstanceExtensionProperties(nullptr, &en, ies.data());
  if (!has_extension(ies, VK_KHR_SURFACE_EXTENSION_NAME))
    exception_report::raise("No Vulkan driver found (MoltenVK on a Mac: the release ships it next to the "
                             "game as libMoltenVK.dylib with MoltenVK_icd.json; source builds: brew install "
                             "molten-vk; or point VK_DRIVER_FILES at a MoltenVK_icd.json)");
  for (const char *e : extensions)
    if (!has_extension(ies, e))
      exception_report::raise(std::string("the Vulkan driver lacks instance extension ") + e);
  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  if(has_extension(ies,VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME))
    extensions.push_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
  if (has_extension(ies, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    ci.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }
#ifdef __ANDROID__
  const bool surfaceMaintenance = has_extension(ies,VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME) &&
      has_extension(ies,VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
  if(surfaceMaintenance) {
    extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
    extensions.push_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
  }
#endif
  VkDebugUtilsMessengerCreateInfoEXT debug{
      VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
  const char *layer = "VK_LAYER_KHRONOS_validation";
  VkValidationFeatureEnableEXT syncFeature = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
  VkValidationFeaturesEXT validation{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
  validation.enabledValidationFeatureCount = 1;
  validation.pEnabledValidationFeatures = &syncFeature;
  uint32_t layerCount = 0;
  vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
  std::vector<VkLayerProperties> layers(layerCount);
  vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
  bool validationAvailable = std::any_of(layers.begin(), layers.end(), [layer](const auto& p) {
    return !strcmp(p.layerName, layer);
  }) && has_extension(ies, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  bool validationWanted = getenv("WWHD_VK_VALIDATION") != nullptr;
#ifdef __ANDROID__
  validationWanted = true;
#endif
  bool validationEnabled = validationWanted && validationAvailable;
  LOG("[vulkan] validation available=%u enabled=%u; first 64 warnings/errors retained", validationAvailable, validationEnabled);
  if (validationEnabled) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    ci.enabledLayerCount = 1;
    ci.ppEnabledLayerNames = &layer;
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback = debug_message;
    ci.pNext = &debug;
    if (getenv("WWHD_VK_VALIDATION")) {
      // Validation features are provided by the enabled layer; they need not be
      // advertised by vkEnumerateInstanceExtensionProperties(nullptr, ...).
      uint32_t validationExtensionCount = 0;
      vk_check(vkEnumerateInstanceExtensionProperties(layer, &validationExtensionCount, nullptr),
               "enumerate validation layer extensions");
      std::vector<VkExtensionProperties> validationExtensions(validationExtensionCount);
      vk_check(vkEnumerateInstanceExtensionProperties(layer, &validationExtensionCount, validationExtensions.data()),
               "read validation layer extensions");
      validationExtensions.resize(validationExtensionCount);
      if (!has_extension(ies, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) &&
          !has_extension(validationExtensions, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME))
        exception_report::raise("The validation layer does not provide VK_EXT_validation_features; synchronization validation cannot be enabled");
      extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
      validation.pNext = &debug;
      ci.pNext = &validation;
      LOG("[vulkan] synchronization validation enabled");
    }
  }
  VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  ai.pApplicationName = "WWHD Vulkan";
  ai.apiVersion = VK_API_VERSION_1_3;
  ci.pApplicationInfo = &ai;
  ci.enabledExtensionCount = extensions.size();
  ci.ppEnabledExtensionNames = extensions.data();
  VkResult instanceResult = vkCreateInstance(&ci, nullptr, &R.instance);
  if (instanceResult != VK_SUCCESS && validationEnabled && !getenv("WWHD_VK_VALIDATION")) {
    // Optional Android diagnostics must not prevent a working driver from starting
    // when a layer is advertised but cannot be loaded (or its initialization fails).
    log_vulkan_result(instanceResult, "create instance with optional validation");
    LOG("[vulkan] optional validation failed to initialize; retrying without the layer");
    validationEnabled = false;
    ci.enabledLayerCount = 0;
    ci.ppEnabledLayerNames = nullptr;
    ci.pNext = nullptr;
    R.instance = VK_NULL_HANDLE;
    instanceResult = vkCreateInstance(&ci, nullptr, &R.instance);
  }
  vk_check(instanceResult, "create Vulkan instance");
  debug.pNext = nullptr;
  load_instance_functions(R.instance);
  if (validationEnabled) {
    auto create = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        R.instance, "vkCreateDebugUtilsMessengerEXT");
    VkDebugUtilsMessengerEXT messenger;
    if (!create) throw std::runtime_error("validation messenger function unavailable");
    vk_check(create(R.instance, &debug, nullptr, &messenger),
             "create validation messenger");
    LOG("[vulkan validation] VK_LAYER_KHRONOS_validation active; synchronization validation enabled");
  }
  create_surfaces();
  vkEnumeratePhysicalDevices(R.instance, &n, nullptr);
  std::vector<VkPhysicalDevice> devices(n);
  vkEnumeratePhysicalDevices(R.instance, &n, devices.data());
  devices.resize(n);
  std::string unsuitable;
  std::vector<RenderingChoice> renderingChoices(devices.size());
  std::vector<uint32_t> deviceQueues(devices.size(), UINT32_MAX);
  std::vector<VkQueueFamilyProperties> deviceQueueProperties(devices.size());
  device_inventory.clear();
  for (size_t i = 0; i < devices.size(); ++i) {
    auto device = devices[i];
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(device, &properties);
    const auto have = renderingChoices[i] = rendering_path(device, properties, device_extensions(device));
    DeviceInfo info;
    info.name = properties.deviceName;
    info.vendor = properties.vendorID;
    info.device = properties.deviceID;
    info.type = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? "discrete" :
                properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? "integrated" :
                properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? "virtual" :
                properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ? "CPU" : "other";
    info.driver = driver_version_text(properties);
    info.version = version_text(device_api_version(properties));
    info.core = have.path == RenderingPath::Core;
    info.renderPasses = have.path == RenderingPath::RenderPass;
    if (device_api_version(properties) >= VK_API_VERSION_1_1) {
      VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
      VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
      props.pNext = &id;
      vkGetPhysicalDeviceProperties2(device, &props);
      bool nonzero = false;
      for (auto byte : id.deviceUUID) nonzero |= byte != 0;
      if (nonzero) {
        info.identity = "uuid:";
        constexpr char hex[] = "0123456789abcdef";
        for (auto byte : id.deviceUUID) { info.identity += hex[byte >> 4]; info.identity += hex[byte & 15]; }
      }
    }
    if (info.identity.empty()) info.identity = "id:" + std::to_string(info.vendor) + ":" +
        std::to_string(info.device) + ":" + info.name;
    if (have.path == RenderingPath::None)
      info.unavailable = have.reason;
    else {
      uint32_t qn;
      vkGetPhysicalDeviceQueueFamilyProperties(device, &qn, nullptr);
      std::vector<VkQueueFamilyProperties> qs(qn);
      vkGetPhysicalDeviceQueueFamilyProperties(device, &qn, qs.data());
      for (uint32_t q = 0; q < qn; ++q) {
        VkBool32 tv = 0, drc = VK_TRUE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, q, R.tv.surface, &tv);
        if (R.drc.surface) vkGetPhysicalDeviceSurfaceSupportKHR(device, q, R.drc.surface, &drc);
        if ((qs[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && tv && drc) {
          deviceQueues[i] = q; deviceQueueProperties[i] = qs[q]; break;
        }
      }
      if (deviceQueues[i] == UINT32_MAX) info.unavailable = "cannot draw to the game window";
    }
    LOG("[vulkan] GPU %zu: %s (%s, vendor %04x, device %04x, Vulkan %s, driver %s, %s): %s",
        i, info.name.c_str(), info.type.c_str(), info.vendor, info.device, info.version.c_str(),
        info.driver.c_str(), info.identity.c_str(), info.usable() ? "usable" : info.unavailable.c_str());
    if (!info.usable()) unsuitable += "\n" + info.name + ": " + info.unavailable;
    device_inventory.push_back(info);
  }
  const char* deviceEnv = getenv("WWHD_VK_DEVICE");
  const auto choice = choose_device(device_inventory, device_setting(), deviceEnv ? deviceEnv : "");
  if (choice.index >= 0) {
    const auto i = size_t(choice.index);
    R.physicalDevice = devices[i];
    R.formatProperties.clear();
    R.queueFamily = deviceQueues[i];
    R.computeQueue = (deviceQueueProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
    R.gpuTimestampValidBits = deviceQueueProperties[i].timestampValidBits;
    R.dynamicRenderingKHR = renderingChoices[i].path == RenderingPath::KHR;
    R.renderPasses = renderingChoices[i].path == RenderingPath::RenderPass;
    R.renderingReason = renderingChoices[i].reason;
    LOG("[vulkan] rendering path: %s (%s)", rendering_path_name(renderingChoices[i].path), renderingChoices[i].reason);
    active_device = device_inventory[i].name;
    device_fallback = choice.fallback.empty() ? "" : choice.fallback + "; using " + active_device + ".";
    if (!device_fallback.empty()) LOG("[vulkan] %s (%s)", device_fallback.c_str(), deviceEnv ? "env" : "setting");
    LOG("[vulkan] chosen GPU: %s (%s)", active_device.c_str(), choice.reason.c_str());
  }
  if (!R.physicalDevice)
    exception_report::raise(
        devices.empty()
            ? std::string("No graphics card with a Vulkan driver was found.\n\n") + kUpdateDriver
            : "The graphics driver does not support the Vulkan features the game needs: Vulkan 1.1 or newer.\n" + unsuitable +
                  "\n\n" + kUpdateDriver);
  vkGetPhysicalDeviceProperties(R.physicalDevice, &R.properties);
#ifdef __ANDROID__
  drivers::set_vulkan_identity(std::string(R.properties.deviceName)+"; Vulkan driver "+driver_version_text(R.properties));
#endif
  const uint32_t deviceApi = device_api_version(R.properties);
  LOG("[vulkan] device: %s (Vulkan %s, driver %s%s)", R.properties.deviceName,
      version_text(R.properties.apiVersion).c_str(), driver_version_text(R.properties).c_str(),
      R.dynamicRenderingKHR ? "; dynamic rendering through VK_KHR_dynamic_rendering" : "");
  R.attachmentViewUsage =
      attachment_view_usage_wanted(R.properties.vendorID, getenv("WWHD_VK_ATTACHMENT_VIEW_USAGE"));
  if (R.attachmentViewUsage)
    LOG("[vulkan] attachment views name their usage (issue #144; WWHD_VK_ATTACHMENT_VIEW_USAGE=0 turns it off)");
  if (perf_enabled()) {
    const auto& limits=R.properties.limits;
    LOG("[vulkan uniforms] dynamic/set %u; uniform/stage %u; uniform/set %u; resources/stage %u; alignment %llu; range %u",
        limits.maxDescriptorSetUniformBuffersDynamic,limits.maxPerStageDescriptorUniformBuffers,
        limits.maxDescriptorSetUniformBuffers,limits.maxPerStageResources,
        (unsigned long long)limits.minUniformBufferOffsetAlignment,limits.maxUniformBufferRange);
  }
  const std::vector<VkExtensionProperties> des = device_extensions(R.physicalDevice);
  LOG("[vulkan diagnostics] vendor=0x%04x device=0x%04x API=%s driverRaw=0x%08x",
      R.properties.vendorID, R.properties.deviceID, version_text(R.properties.apiVersion).c_str(), R.properties.driverVersion);
  if (R.properties.apiVersion >= VK_API_VERSION_1_2 || has_extension(des, VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME)) {
    VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props.pNext = &driver;
    vkGetPhysicalDeviceProperties2(R.physicalDevice, &props);
    LOG("[vulkan diagnostics] driverName=%s driverInfo=%s driverID=%u conformance=%u.%u.%u.%u",
        driver.driverName, driver.driverInfo, unsigned(driver.driverID), driver.conformanceVersion.major,
        driver.conformanceVersion.minor, driver.conformanceVersion.subminor, driver.conformanceVersion.patch);
#ifdef __ANDROID__
    drivers::set_vulkan_identity(std::string(driver.driverName,strnlen(driver.driverName,sizeof driver.driverName))+"; "+R.properties.deviceName+"; version "+driver_version_text(R.properties));
#endif
  } else LOG("[vulkan diagnostics] driver properties unavailable");
  const auto& vertexLimits = R.properties.limits;
  LOG("[vulkan diagnostics] maxVertexInputAttributeOffset=%u maxVertexInputBindingStride=%u maxVertexInputAttributes=%u maxVertexInputBindings=%u maxDrawIndexedIndexValue=%u",
      vertexLimits.maxVertexInputAttributeOffset, vertexLimits.maxVertexInputBindingStride,
      vertexLimits.maxVertexInputAttributes, vertexLimits.maxVertexInputBindings, vertexLimits.maxDrawIndexedIndexValue);
  // Graphics > Advanced "Record graphics on a second thread", read once per start (the worker
  // starts with the device); WWHD_VK_RECORD_WORKER takes precedence (record_worker.cpp)
  {
    std::string value;
    bool defaultWorker=record_worker_default();
#ifdef __ANDROID__
    defaultWorker=defaultWorker||quest_theater::device();
#endif
    set_record_worker_saved_choice(::hostui::get("vkRecordWorker", value) ? value == "1" : defaultWorker);
  }
  nativeWorkerPresentation = false;
  if (record_worker_requested()) {
    const char* nativeOption = std::getenv("WWHD_R5_NATIVE_PRESENT");
    const bool nativeRequested = nativeOption && !std::strcmp(nativeOption, "1");
    if (nativeRequested) {
#ifdef __APPLE__
      constexpr bool appleHost = true;
#else
      constexpr bool appleHost = false;
#endif
      nativeWorkerPresentation = worker_present_policy::resolve(
          true, R.properties.apiVersion,
          has_extension(des, VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME),
          vkGetPhysicalDeviceProperties2 != nullptr, appleHost, [&] {
            VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
            VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
            properties.pNext = &driver;
            vkGetPhysicalDeviceProperties2(R.physicalDevice, &properties);
            return driver.driverID;
          }) == worker_present_policy::Mode::NativeWorker;
    }
    LOG("[vulkan worker] native_present_requested=%u presentation=%s",
        unsigned(nativeRequested), nativeWorkerPresentation ? "native worker" : "conservative producer");
  }
  std::vector<const char *> de{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  presentObservation = present_observer::enabled();
  observedHistory = nullptr;
  nextObservedPresent = 0;
  pendingObservedPresents.clear();
  if (presentObservation) {
    if (has_extension(des, VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME))
      de.push_back(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
    else {
      present_observer::fail("VK_GOOGLE_display_timing unavailable");
      presentObservation = false;
    }
  }
  if (R.dynamicRenderingKHR) {
    de.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    if (deviceApi < VK_API_VERSION_1_2) {
      de.push_back(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME);
      de.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
    }
  }
  if (has_extension(des, "VK_KHR_portability_subset"))
    de.push_back("VK_KHR_portability_subset");
  R.portabilitySubset =
      has_extension(des, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
  VkPhysicalDevicePortabilitySubsetFeaturesKHR portability{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR};
  VkPhysicalDeviceVulkan12Features available12{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  VkPhysicalDeviceFeatures2 features{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  // VkPhysicalDeviceVulkan12Features: Vulkan 1.2 devices and newer
  const bool vulkan12 = deviceApi >= VK_API_VERSION_1_2;
  void **tail = &features.pNext;
  if (vulkan12) {
    *tail = &available12;
    tail = &available12.pNext;
  }
  if (R.portabilitySubset) {
    *tail = &portability;tail=&portability.pNext;
  }
#ifdef __ANDROID__
  VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT};
  const bool hasMaintenance=surfaceMaintenance && has_extension(des,VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
  if(hasMaintenance)*tail=&maintenance;
#endif
  vkGetPhysicalDeviceFeatures2(R.physicalDevice, &features);
  if (R.portabilitySubset) {
    R.imageViewSwizzle = portability.imageViewFormatSwizzle;
    R.imageViewReinterpretation = portability.imageViewFormatReinterpretation;
    R.imageView2DOn3DImage = portability.imageView2DOn3DImage;
    R.samplerMipLodBias = portability.samplerMipLodBias;
    R.separateStencilMaskRef = portability.separateStencilMaskRef;
    R.constantAlphaColorBlendFactors =
        portability.constantAlphaColorBlendFactors;
    R.vertexAttributeAccessBeyondStride =
        portability.vertexAttributeAccessBeyondStride;
  }
  LOG("[vulkan diagnostics] vertexAttributeAccessBeyondStride=%u portabilitySubset=%u", R.vertexAttributeAccessBeyondStride, R.portabilitySubset);
  if (R.portabilitySubset) {
    VkPhysicalDevicePortabilitySubsetPropertiesKHR subset{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props.pNext = &subset;
    vkGetPhysicalDeviceProperties2(R.physicalDevice, &props);
    LOG("[vulkan diagnostics] minVertexInputBindingStrideAlignment=%u", subset.minVertexInputBindingStrideAlignment);
  } else LOG("[vulkan diagnostics] minVertexInputBindingStrideAlignment=1 (core Vulkan; attribute alignment follows component width)");
  VkPhysicalDeviceVulkan12Features enabled12{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  enabled12.samplerMirrorClampToEdge = available12.samplerMirrorClampToEdge;
  if (!vulkan12 && has_extension(des, VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME)) {
    de.push_back(VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME);  // Vulkan 1.1: the extension
    enabled12.samplerMirrorClampToEdge = VK_TRUE;
  }
  R.samplerMirrorClampToEdge = enabled12.samplerMirrorClampToEdge;
  VkPhysicalDeviceFeatures available;
  vkGetPhysicalDeviceFeatures(R.physicalDevice, &available);
  VkPhysicalDeviceFeatures enabled{};
  // The game's BC1-BC5 textures need this core feature enabled, not just supported (Vulkan spec);
  // devices without it (many Mali/PowerVR GPUs) decode BC uploads with a compute shader (bc_decode).
  enabled.textureCompressionBC = available.textureCompressionBC;
  enabled.samplerAnisotropy = available.samplerAnisotropy;
  enabled.independentBlend = available.independentBlend;
  enabled.depthClamp = available.depthClamp;
  // test switch: the path of devices without depth clamp (vertex shaders clamp at the near plane)
  if (const char* e = getenv("WWHD_VK_FORCE_DEPTH_CLAMP_EMULATION"); e && strcmp(e, "0")) {
    enabled.depthClamp = VK_FALSE;
    LOG("[vulkan] WWHD_VK_FORCE_DEPTH_CLAMP_EMULATION: depth clamp emulated in vertex shaders");
  } else if (!available.depthClamp)
    LOG("[vulkan] device has no depth clamp: emulated in vertex shaders");
  enabled.depthBiasClamp = available.depthBiasClamp;
  enabled.shaderClipDistance = available.shaderClipDistance;
  enabled.shaderCullDistance = available.shaderCullDistance;
  enabled.fillModeNonSolid = available.fillModeNonSolid;
  enabled.largePoints = available.largePoints;
  enabled.dualSrcBlend = available.dualSrcBlend;
  enabled.logicOp = available.logicOp;
  R.enabledFeatures = enabled;
  LOG("[vulkan diagnostics] features supported/enabled: robustBufferAccess=%u/%u shaderInt16=%u/%u shaderInt64=%u/%u shaderFloat64=%u/%u geometryShader=%u/%u",
      available.robustBufferAccess, enabled.robustBufferAccess, available.shaderInt16, enabled.shaderInt16,
      available.shaderInt64, enabled.shaderInt64, available.shaderFloat64, enabled.shaderFloat64,
      available.geometryShader, enabled.geometryShader);
  LOG("[vulkan diagnostics] features supported/enabled: fullDrawIndexUint32=%u/%u shaderClipDistance=%u/%u shaderCullDistance=%u/%u independentBlend=%u/%u depthClamp=%u/%u",
      available.fullDrawIndexUint32, enabled.fullDrawIndexUint32, available.shaderClipDistance, enabled.shaderClipDistance,
      available.shaderCullDistance, enabled.shaderCullDistance, available.independentBlend, enabled.independentBlend,
      available.depthClamp, enabled.depthClamp);
  if (has_extension(des, VK_EXT_PRIMITIVE_TOPOLOGY_LIST_RESTART_EXTENSION_NAME)) {
    VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT restart{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVE_TOPOLOGY_LIST_RESTART_FEATURES_EXT};
    VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    query.pNext = &restart;
    vkGetPhysicalDeviceFeatures2(R.physicalDevice, &query);
    LOG("[vulkan diagnostics] primitiveTopologyListRestart supported/enabled=%u/0 patchListRestart=%u/0",
        restart.primitiveTopologyListRestart, restart.primitiveTopologyPatchListRestart);
  }
  LOG("[vulkan diagnostics] listRestartExtension=%u (unused); indices=uint16,uint32; restart=strip-only; uint8 indices unused",
      has_extension(des, VK_EXT_PRIMITIVE_TOPOLOGY_LIST_RESTART_EXTENSION_NAME));
  LOG("[vulkan diagnostics] vertex metadata legend: endian 0=none 1=swap16 2=swap32; numeric 0=normalized 1=integer 2=scaled; observations capped at 64 inputs/32 strides/32 index paths");
  LOG("[vulkan diagnostics] vertex fetch=raw UINT (8/16/32 bit); SNORM/SSCALED/half/endian conversion=32-bit shader arithmetic; shaderInt16 unused");
  LOG("[vulkan diagnostics] fans/quads/loops=CPU index conversion; line/triangle strips=native; gl_VertexIndex=32-bit signed builtin including baseVertex, translated to guest vertex ID");
  portability.pNext=nullptr;
  VkPhysicalDeviceVulkan13Features f13{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  VkPhysicalDeviceDynamicRenderingFeaturesKHR fdr{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
  f13.dynamicRendering = fdr.dynamicRendering = VK_TRUE;
  void *chain = nullptr;
  if (R.renderPasses) tail = &chain;
  else {
    chain = R.dynamicRenderingKHR ? static_cast<void *>(&fdr) : &f13;
    tail = R.dynamicRenderingKHR ? &fdr.pNext : &f13.pNext;
  }
  if (vulkan12) {
    *tail = &enabled12;
    tail = &enabled12.pNext;
  }
  if (R.portabilitySubset)
    *tail = &portability;
#ifdef __ANDROID__
  R.secondaryPresentFences=hasMaintenance && maintenance.swapchainMaintenance1;
  if(R.secondaryPresentFences) {
    if(R.portabilitySubset)tail=&portability.pNext;
    *tail=&maintenance;
    de.push_back(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
  }
  LOG("[vulkan] secondary fence retirement %s",R.secondaryPresentFences ? "enabled" : "unavailable; idle compatibility path");
#endif
  float priorities[2]={1.0f,0.5f};
  VkDeviceQueueCreateInfo queues[2]={{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO},
                                   {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}};
  queues[0].queueFamilyIndex=R.queueFamily;
  queues[0].queueCount=1;
  queues[0].pQueuePriorities=priorities;
  uint32_t requestedFamilies=1;
#ifdef __ANDROID__
  R.secondaryQueueFamily=R.queueFamily;
  uint32_t queueFamilies=0;
  vkGetPhysicalDeviceQueueFamilyProperties(R.physicalDevice,&queueFamilies,nullptr);
  std::vector<VkQueueFamilyProperties> queueProperties(queueFamilies);
  vkGetPhysicalDeviceQueueFamilyProperties(R.physicalDevice,&queueFamilies,queueProperties.data());
  std::vector<PresentationQueueFamily> candidates(queueFamilies);
  for(uint32_t family=0;family<queueFamilies;++family) {
    VkBool32 present=VK_FALSE;
    const VkResult support=vkGetPhysicalDeviceSurfaceSupportKHR(R.physicalDevice,family,R.tv.surface,&present);
    candidates[family]={queueProperties[family].queueCount,
        bool(queueProperties[family].queueFlags&VK_QUEUE_GRAPHICS_BIT),support==VK_SUCCESS && bool(present)};
  }
  const bool forceIsolated=getenv("WWHD_VK_ISOLATED_SECONDARY") && !strcmp(getenv("WWHD_VK_ISOLATED_SECONDARY"),"1");
  const auto secondary=R.secondaryPresentFences && !forceIsolated ? secondary_presentation_queue(candidates,R.queueFamily) : std::nullopt;
  const bool isolateSecondary=!secondary &&
      has_extension(des,"VK_ANDROID_external_memory_android_hardware_buffer") &&
      has_extension(des,"VK_KHR_external_semaphore_fd") && has_extension(des,"VK_EXT_queue_family_foreign");
  if(isolateSecondary) {
    de.push_back("VK_ANDROID_external_memory_android_hardware_buffer");
    de.push_back("VK_KHR_external_semaphore_fd");de.push_back("VK_EXT_queue_family_foreign");
  }
  if(secondary) {
    R.secondaryQueueFamily=secondary->family;
    if(secondary->family==R.queueFamily)queues[0].queueCount=2;
    else {
      requestedFamilies=2;
      queues[1].queueFamilyIndex=secondary->family;
      queues[1].queueCount=1;
      queues[1].pQueuePriorities=&priorities[1];
    }
  }
#endif
  R.primaryRequestedQueues=queues[0].queueCount;
  VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  di.pNext = chain;
  di.queueCreateInfoCount = requestedFamilies;
  di.pQueueCreateInfos = queues;
  di.pEnabledFeatures = &enabled;
  di.enabledExtensionCount = de.size();
  di.ppEnabledExtensionNames = de.data();
  // Prime the support cache once, before any shader translation jobs start.
  std::string missingVertexFormats;
  for (auto format : vk::vertex_formats) {
    LOG("[vulkan diagnostics] vertex format %s VERTEX_BUFFER_BIT=%u", vk::vertex_format_name(format),
        bool(format_properties(format).bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT));
    if (!(format_properties(format).bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)) {
      if (!missingVertexFormats.empty()) missingVertexFormats += ", ";
      missingVertexFormats += vk::vertex_format_name(format);
    }
  }
  if (!missingVertexFormats.empty())
    LOG("[vulkan] vertex formats without vertex-buffer support: %s", missingVertexFormats.c_str());
  const char* forceVertexFallback = std::getenv("WWHD_VK_VERTEX_FALLBACK");
  std::string fallbackSetting;
  const bool forceFallback = forceVertexFallback ? !std::strcmp(forceVertexFallback, "1") :
      (hostui::get("vkVertexFallback", fallbackSetting) && fallbackSetting == "1");
  // issue #207 driver troubleshooting: environment first, else the saved Graphics > Advanced choice
  auto diagnostic = [](const char* env, const char* key) {
    if (const char* e = std::getenv(env)) return !std::strcmp(e, "1");
    std::string value;
    return hostui::get(key, value) && value == "1";
  };
  std::string cpuOption, cpuClasses;
  const bool hasCpuOption = hostui::get("vkCpuVertices", cpuOption);
  hostui::get("vkCpuVertexClasses", cpuClasses);
  const char* classesEnv = std::getenv("WWHD_VK_CPU_VERTEX_CLASSES");
  const auto cpuChoice = vk::choose_cpu_vertices(R.properties.vendorID,
      hasCpuOption ? cpuOption.c_str() : nullptr, std::getenv("WWHD_VK_CPU_VERTICES"),
      classesEnv ? classesEnv : (cpuClasses.empty() ? nullptr : cpuClasses.c_str()));
  R.vertexDiagnostics.cpuClasses = cpuChoice.mask;
  R.vertexDiagnostics.cpuVertices = cpuChoice.mask != 0;
  LOG("[vulkan] %s; CPU vertex classes=%s", cpuChoice.reason, vk::cpu_vertex_class_names(cpuChoice.mask).c_str());
  R.vertexDiagnostics.fullPrecision = diagnostic("WWHD_VK_FULL_PRECISION", "vkFullPrecision");
  R.vertexDiagnostics.noVertexReuse = diagnostic("WWHD_VK_NO_VERTEX_REUSE", "vkNoVertexReuse");
  g_latteCpuVertexDecode = R.vertexDiagnostics.cpuVertices;
  g_latteCpuVertexClasses = R.vertexDiagnostics.cpuClasses;
  vk::set_full_precision(R.vertexDiagnostics.fullPrecision);
  LOG("[vulkan diagnostics] convert vertices on the CPU=%u, full precision in shaders=%u, no vertex reuse=%u",
      R.vertexDiagnostics.cpuVertices, R.vertexDiagnostics.fullPrecision, R.vertexDiagnostics.noVertexReuse);
  // CPU-converted vertices are fetched as 4x32-bit float/uint only: the RGB split does not apply
  R.vertexFormats = R.vertexDiagnostics.cpuClasses == vk::CpuAll ? vk::VertexFormatPolicy{} : vk::vertex_format_policy([](VkFormat format) {
    return bool(format_properties(format).bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT);
  }, forceFallback);
  LOG("[vulkan diagnostics] RGB fallback forced=%u active=%u mask=%u (8-bit=1 16-bit=2 32-bit=4)",
      forceFallback, R.vertexFormats.fallbackMask != 0, R.vertexFormats.fallbackMask);
  if (R.vertexFormats.fallbackMask)
    LOG("[vulkan] vertex format fallback mask: %u", R.vertexFormats.fallbackMask);
  vk_check(vkCreateDevice(R.physicalDevice, &di, nullptr, &R.device),
           "create Vulkan device");
  load_device_functions(R.device, R.dynamicRenderingKHR, R.renderPasses);
  VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
  if (deviceApi >= VK_API_VERSION_1_2 || has_extension(des, VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME)) {
    VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props.pNext = &driver;
    vkGetPhysicalDeviceProperties2(R.physicalDevice, &props);
  }
  const bool serialPipelines = serial_pipelines_wanted(driver.driverID, R.properties.driverVersion,
                                                     getenv("WWHD_SERIAL_PIPELINES"));
  serialize_pipeline_creation(serialPipelines);
  if (driver.driverID == VK_DRIVER_ID_QUALCOMM_PROPRIETARY || serialPipelines)
    LOG("[vulkan] serial pipeline creation %s (driver id %d, Qualcomm release v%u; WWHD_SERIAL_PIPELINES=0/1 overrides)",
        serialPipelines ? "on" : "off", int(driver.driverID), qualcomm_driver_release(R.properties.driverVersion));
  {
    const BcDecoderChoice bc = choose_bc_decoder(driver.driverID, R.properties.vendorID, R.properties.driverVersion,
                                                 getenv("WWHD_VK_BC_CPU"));
    R.bcCpuDecode = bc.decoder == BcDecoder::Cpu;
    const char* forced = getenv("WWHD_VK_FORCE_BC_DECODE");
    if (!R.enabledFeatures.textureCompressionBC || (forced && strcmp(forced, "0")))
      LOG("[vulkan] %s; %s upload decoder enabled (%s; WWHD_VK_BC_CPU=0/1 overrides)",
          R.enabledFeatures.textureCompressionBC ? "WWHD_VK_FORCE_BC_DECODE" : "device has no BC texture support",
          R.bcCpuDecode ? "CPU" : "compute", bc.reason);
  }
  {
    // panvk has no geometric clipper; see clip_emulation_wanted in shaders.cpp.
    const char* forced = getenv("WWHD_VK_CLIP_EMULATION");
    const bool wanted = forced ? strcmp(forced, "0") != 0 : driver.driverID == VK_DRIVER_ID_MESA_PANVK;
    R.clipEmulation = wanted && R.enabledFeatures.depthClamp;
    if (R.clipEmulation || forced)
      LOG("[vulkan] depth clipping in shaders %s (%s)", R.clipEmulation ? "on" : "off",
          forced && !R.enabledFeatures.depthClamp ? "WWHD_VK_CLIP_EMULATION, but the device has no depthClamp" :
          forced ? "set by WWHD_VK_CLIP_EMULATION" :
          "panvk culls fragments for depth clipping; WWHD_VK_CLIP_EMULATION=0/1 overrides");
  }
  if (presentObservation) {
    observedHistory = reinterpret_cast<PFN_vkGetPastPresentationTimingGOOGLE>(
        vkGetDeviceProcAddr(R.device, "vkGetPastPresentationTimingGOOGLE"));
    if (!observedHistory) {
      present_observer::fail("enabled GOOGLE history entry point unavailable");
      presentObservation = false;
    }
  }
  init_pass_caches();
  vkGetDeviceQueue(R.device, R.queueFamily, 0, &R.queue);
#ifdef __ANDROID__
  if(secondary) {
    vkGetDeviceQueue(R.device,secondary->family,secondary->index,&R.secondaryQueue);
    secondaryPresentWorker=std::make_unique<PresentWorker<SecondaryPresentResult>>();
  }
  if(isolateSecondary) {
    try {
      isolatedSecondary=std::make_unique<AndroidIsolatedPresenter>(R.instance,R.physicalDevice,R.device,R.queueFamily,R.dynamicRenderingKHR,R.renderPasses);
      R.secondaryQueue=isolatedSecondary->queue();R.secondaryIsolated=true;R.secondaryPresentFences=false;
      LOG("[vulkan] isolated secondary logical device enabled; queue index 0 on each device");
    } catch(const std::exception& error) {
      LOG("[vulkan] isolated secondary logical device unavailable: %s",error.what());
    }
  }
  LOG("[vulkan] secondary presentation worker %s (primary family %u, secondary family %u)",
      R.secondaryQueue ? "dedicated queue" : "unavailable; shared queue compatibility path",
      R.queueFamily,R.secondaryQueueFamily);
#endif
  init_pipeline_cache();
  {
    // WWHD_VK_SUBMISSION_POOL (submission_pool.h): slots created now; overflow adds more up to 16 only
    // while a captured tail holds the next slot
    const auto pool=subpool::choose(std::getenv("WWHD_VK_SUBMISSION_POOL"),std::getenv("WWHD_VK_SUBMISSION_POOL8"));
    R.submissions.resize(pool.slots);
    submissionPoolSlots=pool.slots;
    LOG("[vulkan] submission pool %u slots (%s%s); overflow up to %u",pool.slots,pool.source,
        pool.adjusted ? "; invalid or out-of-range value adjusted" : "",subpool::kMaxSlots);
  }
  for (auto& slot:R.submissions) {
  VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool.queueFamilyIndex = R.queueFamily;
  pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  vk_check(vkCreateCommandPool(R.device, &pool, nullptr, &slot.commandPool),
           "create command pool");
  VkCommandBufferAllocateInfo ca{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ca.commandPool = slot.commandPool;
  ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ca.commandBufferCount = 1;
  vk_check(vkAllocateCommandBuffers(R.device, &ca, &slot.cmd),
           "allocate command buffer");
  VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  vk_check(vkCreateFence(R.device, &fi, nullptr, &slot.fence),
           "create frame fence");
  VkDescriptorPoolSize sizes[] = {
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 65536},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 32768},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 32768},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 65536}};
  VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dp.maxSets = 32768;
  dp.poolSizeCount = std::size(sizes);
  dp.pPoolSizes = sizes;
  vk_check(vkCreateDescriptorPool(R.device, &dp, nullptr, &slot.descriptorPool),
           "create descriptor pool");
  }
  init_gpu_timestamp_queries();
  activate_submission(0);
  for (Screen *s : {&R.tv, &R.drc})
    if (s->window) {
      VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
      vk_check(vkCreateSemaphore(R.device, &si, nullptr, &s->acquired),
               "create acquire semaphore");
      vk_check(vkCreateSemaphore(R.device, &si, nullptr, &s->finished),
               "create present semaphore");
      make_swapchain(*s);
    }
  vk::select_renderer();
  set_graphics_feature_available(GraphicsFeature::AO);
  set_graphics_feature_available(GraphicsFeature::AOHires);
  set_graphics_feature_available(GraphicsFeature::Anisotropy,
                                 R.enabledFeatures.samplerAnisotropy);
  if (const char* path = getenv("WWHD_CAPTURE_PATH")) {
    const char* frame = getenv("WWHD_CAPTURE");
    request_tv_dump(path, frame ? std::max(0, atoi(frame)) : 120);
  }
  bc_decode_startup_check();
  // Instantiate both static worker states before the saver can be constructed, even when
  // async compilation/warmup is disabled. They must outlive the saver's static destructor.
  merge_pipeline_demand_cache(VK_NULL_HANDLE);
  merge_pipeline_worker_cache(VK_NULL_HANDLE);
  start_record_worker();
  LOG("[vulkan record worker] %s", record_worker_active()
      ? "enabled: POD recording/submission; producer-owned pipeline state"
      : "disabled: original immediate/deferred path");
  backgroundCacheReadiness.publish();
}

#ifdef WWHD_SDL_HOST
static bool hidden_windows() {
  static const bool hidden = [] { const char* e = getenv("WWHD_HIDDEN_WINDOWS"); return e && *e && strcmp(e, "0"); }();
  return hidden;
}
// The game's own icon on the windows (title bar, taskbar): meta/iconTex.tga of the game folder, an
// uncompressed 32-bit TGA (128x128 in Wind Waker HD). Nothing happens without it.
static void set_window_icons() {
  FILE *f = std::fopen((config::game_dir + "/meta/iconTex.tga").c_str(), "rb");
  if (!f)
    return;
  std::vector<uint8_t> d;
  uint8_t buf[65536];
  for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;)
    d.insert(d.end(), buf, buf + n);
  std::fclose(f);
  if (d.size() < 18 || d[1] != 0 || d[2] != 2 || d[16] != 32)  // no colour map, true colour, 32 bpp
    return;
  const uint32_t w = d[12] | d[13] << 8, h = d[14] | d[15] << 8, start = 18 + d[0];
  if (!w || !h || w > 1024 || h > 1024 || d.size() < start + size_t(w) * h * 4)
    return;
  SDL_Surface *icon = SDL_CreateSurface(int(w), int(h), SDL_PIXELFORMAT_BGRA32);
  if (!icon)
    return;
  const bool topDown = d[17] & 0x20;  // otherwise the first row is the bottom one
  for (uint32_t y = 0; y < h; y++)
    std::memcpy(static_cast<uint8_t *>(icon->pixels) + size_t(y) * icon->pitch,
                d.data() + start + size_t(topDown ? y : h - 1 - y) * w * 4, size_t(w) * 4);
  for (Screen *s : {&R.tv, &R.drc})
    if (s->window)
      SDL_SetWindowIcon(s->window, icon);
  SDL_DestroySurface(icon);
}
bool gamepad_window_available() {
#ifdef __ANDROID__
  return false;
#else
  return !getenv("WWHD_NO_GAMEPAD");
#endif
}
SDL_Window* ensure_gamepad_window(bool create) {
#if !defined(__ANDROID__)
  // Main thread creates the native window and surface; the render thread attaches Vulkan
  // resources at its next frame boundary. No second window exists until requested.
  static SDL_Window* requested = nullptr;
  if (R.drc.window) return R.drc.window;
  if (requested) return requested;
  if (!create || !gamepad_window_available()) return nullptr;
  requested = SDL_CreateWindow("GamePad — Vulkan", 854, 480,
      SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN
#ifdef __linux__
      | SDL_WINDOW_HIGH_PIXEL_DENSITY
#endif
  );
  if (!requested) exception_report::raise(SDL_GetError());
  if (!WWHD_CREATE_WINDOW_SURFACE(requested, R.instance, nullptr, &pendingGamepadSurface))
    exception_report::raise(SDL_GetError());
  int width = 854, height = 480;
  SDL_GetWindowSizeInPixels(requested, &width, &height);
  R.drc.width = width; R.drc.height = height;
  input::set_gamepad_window(requested);
  // Release/acquire publishes the surface and dimensions to the render thread.
  pendingGamepadWindow = requested;
  LOG("[display] GamePad window created on request");
  return requested;
#else
  return nullptr;
#endif
}
// SDL host (Vulkan-only builds): SDL windows, input and audio
void init() {
#ifdef __ANDROID__
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  // Back always reaches the game (input_sdl.cpp: it opens and closes Settings); without this, Android's
  // default Back handling can still close the app after the key event on some devices
  SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#endif
  // hidden test runs: no Dock icon, no activation (the app never takes the focus from the user)
  if (hidden_windows())
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO))
    exception_report::raise(SDL_GetError());
  // the Vulkan loader (vulkan-1.dll, libvulkan.so.1), loaded here rather than imported (loader.h); the
  // windows below use the same one
  if (!SDL_Vulkan_LoadLibrary(nullptr))
    exception_report::raise(std::string("Vulkan is not installed on this computer: the Vulkan runtime "
#ifdef _WIN32
                                         "(vulkan-1.dll) "
#endif
                                         "could not be loaded (") + SDL_GetError() + ").\n\n" + kUpdateDriver);
  auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
#ifdef __ANDROID__
  if (auto custom = drivers::open_custom()) gipa = custom;
#endif
  load_global_functions(gipa);
#ifdef __ANDROID__
  SDL_AddEventWatch(lifecycle_watch, nullptr);
  const SDL_WindowFlags windowFlags = SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN;
#else
  // test runs: WWHD_HIDDEN_WINDOWS=1 never puts the windows on screen (nothing pops up or takes the
  // focus); the swapchains still exist, so frame dumps and present dumps work as with visible windows
  // Linux (issue #201): high pixel density, so Wayland with desktop scaling gives a swapchain at the
  // screen's real pixel size instead of the logical size the compositor then stretches (blurry). The
  // swapchain and the overlay already use the window's size in pixels and its pixel density.
  const SDL_WindowFlags windowFlags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
#if defined(__linux__)
                                      SDL_WINDOW_HIGH_PIXEL_DENSITY |
#endif
                                      (hidden_windows() ? SDL_WINDOW_HIDDEN : 0);
#endif
  R.tv.window = SDL_CreateWindow("Wind Waker HD — Vulkan", 1280, 720, windowFlags);
  if (!R.tv.window)
    exception_report::raise(SDL_GetError());
#ifdef __ANDROID__
  quest_theater::initialize();
  // one surface: the GamePad picture is drawn into it (display_modes.h: picture-in-picture, GamePad
  // only), never a window of its own
#else
  if (!getenv("WWHD_NO_GAMEPAD") && (!overlay::window_target::gamescope_session() ||
      ::hostui::startup_drc_mode() == gfx::kDrcWindow)) {
    // made hidden: the GamePad screen mode (load_saved_options below) shows it in window mode only;
    // the other modes draw the GamePad picture into the TV window (gfx/display_modes.h)
    R.drc.window = SDL_CreateWindow("GamePad — Vulkan", 854, 480, windowFlags | SDL_WINDOW_HIDDEN);
    if (!R.drc.window)
      exception_report::raise(SDL_GetError());
    R.drc.visible = false;
    gfx::g_has_drc_window = true;
  }
#endif
  if (!gfx::hidden::mode().presents())
    R.tv.visible = R.drc.visible = false;  // no drawables: pictures only reach frame / present dumps
  set_window_icons();
  uint32_t n;
  const char *const *se = SDL_Vulkan_GetInstanceExtensions(&n);
  if (!se)
    exception_report::raise(SDL_GetError());
  for (Screen *s : {&R.tv, &R.drc})
    if (s->window) {
      int width = 0, height = 0;
      if (!SDL_GetWindowSizeInPixels(s->window, &width, &height))
        exception_report::raise(SDL_GetError());
      s->width = width;
      s->height = height;
    }
  init_device(std::vector<const char *>(se, se + n), [] {
    if (!WWHD_CREATE_WINDOW_SURFACE(R.tv.window, R.instance, nullptr,
                                  &R.tv.surface))
      exception_report::raise(SDL_GetError());
    if (R.drc.window && !WWHD_CREATE_WINDOW_SURFACE(R.drc.window, R.instance,
                                                  nullptr, &R.drc.surface))
      exception_report::raise(SDL_GetError());
  });
  mods::mouse_init(R.tv.window);
  input::set_prompt_window(R.tv.window);
  input::set_gamepad_window(R.drc.window);
  input::init();
  install_graphics_menu(R.tv.window);
  ::hostui::load_saved_options();  // graphics and GamePad screen options saved by the settings overlay (settings.ini)
#ifdef __ANDROID__
  if (quest_theater::active()) stereo::set_mode(stereo::Off); // opt in using the theater's 3D button
#endif
}
#endif
void save_renderer_caches() {
  // Save-only checkpoint: the guest continues after test-end checkpoints.
  wait_idle();
  vk::save_shader_cache();
  save_pipeline_cache(false);
}
bool background_renderer_caches_ready() { return backgroundCacheReadiness.available(); }
void disable_background_renderer_caches() { backgroundCacheReadiness.withdraw(); }
std::function<void()> background_renderer_caches() {
  // Capture host cache bytes under the GX2 lock. File work and the returned
  // completion wait own immutable state and run after releasing that lock.
  // No surface, command-buffer or GPU-idle wait during Android suspension.
  // Recheck under execution lock: a request may have raced shutdown withdrawal.
  if (!backgroundCacheReadiness.available()) return {};
  merge_live_pipeline_caches(true);
  poll_pipeline_cache_write(false);
  start_pipeline_cache_write();
  return [write = pipelineCacheWrite] { if (write.valid()) write.wait(); };
}
void shutdown_renderer_caches() {
  backgroundCacheReadiness.withdraw();
  // Destructive teardown is reserved for orderly shutdown under execution lock.
  try {
    if (presentObservation || handoff_trace::enabled() || fallback_stats::storage) flush_present_observer();
    reset_feedback_images();
    wait_idle();
  } catch (...) {
    // A replay error can surface during flush/idle. Always join and withdraw
    // the worker before propagating it; no queued driver calls survive teardown.
    try { stop_record_worker(); } catch (...) {}
    throw;
  }
  stop_record_worker();
  bc_decode_shutdown();
#ifdef __ANDROID__
  collect_secondary_swapchains();
  shutdown_secondary_worker();
#endif
  destroy_gpu_timestamp_queries();
  vk::save_shader_cache();
  save_pipeline_cache();
  reset_pass_caches();
  // The SDL quit path uses _Exit, so static destructors never flush this.
  // Save after compiler/record workers have joined, including their last events.
  stutter::trace().flush();
}
#ifdef WWHD_SDL_HOST
// GamePad touch screen: the left mouse button on the GamePad picture, in the GamePad window (window
// mode; the picture fitted as display_layout fits it) or inside the TV window (picture-in-picture,
// GamePad only: display_modes.cpp maps the point as on macOS), mapped to 0..1 touch coordinates. A
// press that starts on the picture keeps touching while it is dragged, clamped to the picture's edge.
// The settings overlay keeps its clicks in the TV window.
enum class TouchIn { None, DrcWindow, TvWindow };
static TouchIn touchHeld = TouchIn::None;
static bool drc_window_point(float x, float y, float &tx, float &ty) {
  int pw = 0, ph = 0;
  SDL_GetWindowSizeInPixels(R.drc.window, &pw, &ph);
  const float density = SDL_GetWindowPixelDensity(R.drc.window);
  // the picture's size as the layout sees it (full internal resolution: dynamic resolution, dynres.h)
  const float sw = R.drc.scan ? nominal_width(R.drc.scan.get()) : 854.0f;
  const float sh = R.drc.scan ? nominal_height(R.drc.scan.get()) : 480.0f;
  const gfx::Box b = gfx::display_layout(float(pw), float(ph), sw, sh);
  if (b.w <= 0 || b.h <= 0) return false;
  tx = (x * density - b.x) / b.w;
  ty = (y * density - b.y) / b.h;
  return tx >= 0 && tx <= 1 && ty >= 0 && ty <= 1;
}
// a point of the TV window (window coordinates) as 0..1 from its top left
static void tv_window_point(float x, float y, float &nx, float &ny) {
  int w = 0, h = 0;
  SDL_GetWindowSize(R.tv.window, &w, &h);
  nx = w > 0 ? x / float(w) : 0;
  ny = h > 0 ? y / float(h) : 0;
}
//
// Touch screens (display_modes.h's view button, on by default on Android): a tap on the button
// switches to the next view (picture-in-picture, GamePad only, TV only), a long press (0.6 s)
// switches 60 fps on Android. On Android fingers are read as fingers (several at once, one of them
// touching the GamePad); the mouse events SDL makes from them only reach the settings overlay.
static uint64_t buttonDown = 0;  // SDL_GetTicks() of the press on the view button, 0: none
static void view_button_released() {
  if (SDL_GetTicks() - buttonDown >= 600) {
#ifdef __ANDROID__
    // the choice only: platform/perf_hint.cpp switches interpolation where the phone keeps up
    perf_hint::set_fps60_chosen(!perf_hint::fps60_chosen());
    ::hostui::graphics_changed();  // saved with the graphics options (fps60)
#endif
  } else {
    ::hostui::set_drc_mode(gfx::next_view());
  }
  buttonDown = 0;
}
#ifdef __ANDROID__
static bool synthHeld = false;                 // a mouse press SDL made from a finger, kept from the game
static SDL_FingerID touchFinger = 0, buttonFinger = 0;  // 0: none
bool android_display_touch_event(const SDL_Event& event) {
  if (event.type != SDL_EVENT_FINGER_DOWN && event.type != SDL_EVENT_FINGER_MOTION &&
      event.type != SDL_EVENT_FINGER_UP && event.type != SDL_EVENT_FINGER_CANCELED)
    return false;
  const SDL_TouchFingerEvent& f = event.tfinger;
  if (f.windowID != SDL_GetWindowID(R.tv.window)) return false;
  static uint64_t touchGeneration = 0;
  uint64_t currentGeneration = touchLayoutGeneration.load();
  if (touchGeneration != currentGeneration) {
    touchGeneration = currentGeneration;
    touchFinger = buttonFinger = 0;
    buttonDown = 0;
  }
  const bool dual = gfx::g_has_drc_window;
  if (event.type == SDL_EVENT_FINGER_DOWN && SDL_GetHintBoolean("WWHD_DISPLAY_SMOKE_INPUT_TRACE", false)) {
    std::lock_guard<std::mutex> guard(secondaryMutex);
    LOG("[display smoke input] down layout=%llu fingerHeld=%lld capture=%d changed=%d viewport=%f,%f,%f,%f",
        (unsigned long long)currentGeneration, (long long)touchFinger, int(overlay::captures()),
        int(secondaryChanged), primaryTouchBox.x, primaryTouchBox.y, primaryTouchBox.w, primaryTouchBox.h);
  }
  if (dual && !dual_display::active_swap) {
    touchFinger = buttonFinger = 0;
    buttonDown = 0;
    return false;
  }
  float tx = 0, ty = 0;
  auto hit = [&](bool dragging = false) {
    if (!dual) return gfx::overlay_hit(f.x, f.y, &tx, &ty, dragging);
    std::lock_guard<std::mutex> guard(secondaryMutex);
    return gamepad_touch::map(primaryTouchBox, f.x, f.y, dragging, tx, ty);
  };
  switch (event.type) {
  case SDL_EVENT_FINGER_DOWN:
    if (input::touch_from_controller(f.touchID)) return true;  // a controller's own touch pad / buttons
    if (overlay::captures()) return false;
    if (!dual && !buttonFinger && gfx::view_button_hit(f.x, f.y)) {
      buttonFinger = f.fingerID;
      buttonDown = std::max<uint64_t>(SDL_GetTicks(), 1);
      return true;
    }
    if (touchFinger || !hit()) return false;
    touchFinger = f.fingerID;
    input::set_touch(true, tx, ty);
    return true;
  case SDL_EVENT_FINGER_MOTION:
    if (!touchFinger || f.fingerID != touchFinger) return f.fingerID == buttonFinger && buttonFinger;
    if (!hit(true)) tx = ty = 0;
    input::set_touch(true, tx, ty);
    return true;
  case SDL_EVENT_FINGER_UP:
  case SDL_EVENT_FINGER_CANCELED:
    if (buttonFinger && f.fingerID == buttonFinger) {
      buttonFinger = 0;
      if (event.type == SDL_EVENT_FINGER_UP) view_button_released();
      buttonDown = 0;
      return true;
    }
    if (!touchFinger || f.fingerID != touchFinger) return false;
    touchFinger = 0;
    if (!hit(true)) tx = ty = 0;
    input::set_touch(false, tx, ty);
    return true;
  default:
    return false;
  }
}
#endif
static bool gamepad_touch(const SDL_Event& event) {
  // In gamescope the GamePad may host the menu. Let SDL overlay input receive its clicks,
  // including touch-emulated mouse events, before the guest GamePad touch router consumes them.
  if (overlay::window_target::selected.load() == overlay::window_target::Window::GamePad && overlay::captures()) {
    if (touchHeld != TouchIn::None) { touchHeld = TouchIn::None; input::set_touch(false, 0, 0); }
    return false;
  }
  const SDL_WindowID drcId = R.drc.window ? SDL_GetWindowID(R.drc.window) : 0;
  const SDL_WindowID tvId = SDL_GetWindowID(R.tv.window);
  float tx = 0, ty = 0;
#ifdef __ANDROID__
  if (android_display_touch_event(event)) return true;
  // the mouse events SDL makes from fingers: those on the GamePad picture or the button are the
  // fingers' (above) and kept from the game's mouse camera; the settings overlay gets them all
  if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
      event.button.which == SDL_TOUCH_MOUSEID) {
    if (overlay::captures() || event.button.windowID != tvId) return false;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) return std::exchange(synthHeld, false);
    float nx, ny;
    tv_window_point(event.button.x, event.button.y, nx, ny);
    synthHeld = gfx::view_button_hit(nx, ny) || gfx::overlay_hit(nx, ny, &tx, &ty);
    return synthHeld;
  }
  if (event.type == SDL_EVENT_MOUSE_MOTION && event.motion.which == SDL_TOUCH_MOUSEID) return synthHeld;
#endif
  switch (event.type) {
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    if (event.button.button != SDL_BUTTON_LEFT) return false;
    if (event.button.windowID == tvId && !overlay::captures() && gfx::view_button_enabled()) {
      float nx, ny;
      tv_window_point(event.button.x, event.button.y, nx, ny);
      if (gfx::view_button_hit(nx, ny)) {
        buttonDown = std::max<uint64_t>(SDL_GetTicks(), 1);
        return true;
      }
    }
    if (drcId && event.button.windowID == drcId) {
      if (!drc_window_point(event.button.x, event.button.y, tx, ty)) return true;  // outside the picture
      touchHeld = TouchIn::DrcWindow;
    } else if (event.button.windowID == tvId && !overlay::captures()) {
      float nx, ny;
      tv_window_point(event.button.x, event.button.y, nx, ny);
      if (!gfx::overlay_hit(nx, ny, &tx, &ty)) return false;  // the game's window: mouse camera etc.
      touchHeld = TouchIn::TvWindow;
    } else {
      return false;
    }
    input::set_touch(true, tx, ty);
    return true;
  case SDL_EVENT_MOUSE_MOTION:
  case SDL_EVENT_MOUSE_BUTTON_UP: {
    const bool up = event.type == SDL_EVENT_MOUSE_BUTTON_UP;
    if (buttonDown && up && event.button.button == SDL_BUTTON_LEFT) {
      view_button_released();
      return true;
    }
    if (touchHeld == TouchIn::None || (up && event.button.button != SDL_BUTTON_LEFT)) return false;
    const float x = up ? event.button.x : event.motion.x, y = up ? event.button.y : event.motion.y;
    if (touchHeld == TouchIn::DrcWindow) {
      drc_window_point(x, y, tx, ty);
      tx = std::clamp(tx, 0.0f, 1.0f);
      ty = std::clamp(ty, 0.0f, 1.0f);
    } else {
      float nx, ny;
      tv_window_point(x, y, nx, ny);
      if (!gfx::overlay_hit(nx, ny, &tx, &ty, true)) tx = ty = 0;
    }
    if (up) touchHeld = TouchIn::None;
    input::set_touch(!up, tx, ty);
    return true;
  }
  case SDL_EVENT_WINDOW_FOCUS_LOST:
    if (touchHeld != TouchIn::None) { touchHeld = TouchIn::None; input::set_touch(false, 0, 0); }
    return false;
  default:
    return false;
  }
}

// Ctrl+G (Cmd+G on macOS, as the AppKit host's Display menu): show / hide the GamePad screen in the
// current mode (the GamePad window, or the picture-in-picture overlay; auto mode: keep it up)
static bool drc_key(const SDL_Event& event) {
  if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat || event.key.scancode != SDL_SCANCODE_G) return false;
#ifdef __APPLE__
  const SDL_Keymod want = SDL_KMOD_GUI;
#else
  const SDL_Keymod want = SDL_KMOD_CTRL;
#endif
  if (!(event.key.mod & want) || (event.key.mod & (SDL_KMOD_ALT | SDL_KMOD_SHIFT))) return false;
  ::hostui::toggle_drc();
  return true;
}
// debug: a test variable's list of TV frames ("3500,3700"); due once per listed frame
static bool test_frame_due(const std::vector<uint64_t> &frames, size_t &i) {
  if (i >= frames.size() || frame_count() < frames[i]) return false;
  i++;
  return true;
}
static std::vector<uint64_t> test_frames(const char *var) {
  std::vector<uint64_t> f;
  if (const char *e = getenv(var))
    for (const char *p = e; *p;) {
      f.push_back(strtoull(p, (char **)&p, 10));
      while (*p == ',') p++;
    }
  return f;
}
// debug: WWHD_TEST_DRC_KEY=3500,3700 simulates Ctrl+G at those frames (as display.mm's Cmd+G)
static void test_drc_key() {
  static const std::vector<uint64_t> frames = test_frames("WWHD_TEST_DRC_KEY");
  static size_t i = 0;
  if (test_frame_due(frames, i)) {
    LOG("[display] test: Ctrl+G at frame %llu", (unsigned long long)frame_count());
    ::hostui::toggle_drc();
  }
}

// full screen (the TV window's is remembered for the next start: hostui::tv_fullscreen_changed)
static void toggle_fullscreen(SDL_Window* window) {
  const bool full = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
  const char* which = window == R.drc.window ? "GamePad window" : "TV window";
  if (!SDL_SetWindowFullscreen(window, !full))
    LOG("[display] %s: switching to %s failed: %s", which, full ? "windowed" : "full screen", SDL_GetError());
  else
    LOG("[display] %s %s", which, full ? "windowed" : "full screen");
  if (window == R.tv.window)
    ::hostui::tv_fullscreen_changed();
}
// F11 or Alt+Enter toggles the focused window (TV or GamePad)
static bool fullscreen_key(const SDL_Event& event) {
  if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) return false;
  if (overlay::captures() || getenv("WWHD_NO_HOST_INPUT")) return false;
#ifdef __ANDROID__
  if (!getenv("WWHD_ANDROID_KEYBOARD")) return false;
#endif
  const int code = input::physical_keycode(event.key.scancode);
  int mods = (event.key.mod & SDL_KMOD_SHIFT ? 1 : 0) | (event.key.mod & SDL_KMOD_CTRL ? 2 : 0) |
             (event.key.mod & SDL_KMOD_ALT ? 4 : 0) | (event.key.mod & SDL_KMOD_GUI ? 8 : 0);
  if (input_map::shortcut_for_key(input_map::current(), code, mods) != input_map::kFullscreen) return false;
  SDL_Window* window = SDL_GetWindowFromID(event.key.windowID);
  toggle_fullscreen(window ? window : R.tv.window.load());
  return true;
}
// debug: WWHD_TEST_FULLSCREEN_KEY=3500,3700 simulates F11 on the TV window at those frames (with
// WWHD_HIDDEN_WINDOWS the window stays hidden: SDL only notes the state for when it is shown)
static void test_fullscreen_key() {
  static const std::vector<uint64_t> frames = test_frames("WWHD_TEST_FULLSCREEN_KEY");
  static size_t i = 0;
  if (test_frame_due(frames, i)) {
    LOG("[display] test: F11 at frame %llu", (unsigned long long)frame_count());
    toggle_fullscreen(R.tv.window);
  }
}

#ifdef __ANDROID__
// Settings > Quit Game on Android: the activity closes the app and removes it from the recent apps
// (WwhdActivity.quitApp: finishAndRemoveTask, then the process ends); a bare _Exit would leave the
// game's task in the recent apps. False if the activity could not be asked.
static bool android_end_task() {
  auto* env=static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  if(!env)return false;
  jobject activity=static_cast<jobject>(SDL_GetAndroidActivity());
  if(!activity)return false;
  bool asked=false;
  if(jclass cls=env->GetObjectClass(activity)) {
    if(jmethodID method=env->GetStaticMethodID(cls,"quitApp","()Z"))
      asked=env->CallStaticBooleanMethod(cls,method);
    env->DeleteLocalRef(cls);
  }
  if(env->ExceptionCheck()) {
    env->ExceptionClear();
    asked=false;
  }
  env->DeleteLocalRef(activity);
  return asked;
}
#endif
// Closing the TV window ends the game. SDL only sends SDL_EVENT_QUIT once every window is closed, so
// with the GamePad window open the close button of the TV window did nothing. Closing the GamePad
// window only hides it (WWHD_NO_GAMEPAD=1 starts without it).
// Settings > Quit Game ends the game the same way (hostui::quit: SDL_EVENT_QUIT).
static void quit_game() {
  ss::save_last_session(1500);  // the "Last session" state (a few KB; skipped outside gameplay)
  audio::finish_dump();  // _Exit bypasses atexit handlers
  screenshot::finish();  // the screenshots taken are written first
  gx2::shutdown_vulkan_caches();
#ifdef __ANDROID__
  if (::hostui::quit_from_menu() && android_end_task()) {
    LOG("[quit] closing the app");
    for (int i = 0; i < 50; i++) SDL_Delay(100);  // the activity ends the process; this is a fallback
  }
#endif
  std::_Exit(0);
}
static bool close_request(const SDL_Event& event) {
  if (event.type != SDL_EVENT_WINDOW_CLOSE_REQUESTED) return false;
  if (event.window.windowID == SDL_GetWindowID(R.tv.window)) quit_game();
  if (R.drc.window && event.window.windowID == SDL_GetWindowID(R.drc.window)) {
    ::hostui::drc_window_closed();  // hidden until Ctrl+G or the settings overlay shows it again
    LOG("[display] GamePad window closed (hidden); the game keeps running");
  }
  return true;
}

void run_main_loop() {
  auto titleTime = std::chrono::steady_clock::now();
  uint64_t titleFrames = gx2::flips_presented();
  // Deterministic shutdown hook for cache durability tests.
  const char* exitAt=getenv("WWHD_EXIT_AT_FRAME");
  const uint64_t exitFrame=exitAt ? std::strtoull(exitAt,nullptr,10) : 0;
  // Issue #109: the mouse pointer hides after 2 s without mouse use while the game window has focus
  // (not while the settings overlay is open, which needs it; the mouse camera captures it itself).
  auto mouseUsed = std::chrono::steady_clock::now();
  bool pointerHidden = false, pointerKept = false;
  for (;;) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
#ifdef _WIN32
      if(event.type==SDL_EVENT_WINDOW_HDR_STATE_CHANGED && event.window.windowID==SDL_GetWindowID(R.tv.window)) {
        const bool enabled=SDL_GetBooleanProperty(SDL_GetWindowProperties(R.tv.window),SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN,false);
        // SDL also reports white-level/headroom changes after presentation.
        // Our explicit nits controls do not require a new swapchain for those.
        if(hdr::displayEnabled.exchange(enabled)!=enabled)R.tv.resize=true;
      }
#endif
      if ((event.type == SDL_EVENT_MOUSE_MOTION && event.motion.which != SDL_TOUCH_MOUSEID) ||
          ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
           event.button.which != SDL_TOUCH_MOUSEID) ||
          event.type == SDL_EVENT_MOUSE_WHEEL) {
        mouseUsed = std::chrono::steady_clock::now();
      }
      if (event.type == SDL_EVENT_QUIT) quit_game();
      if (close_request(event)) continue;
      if (fullscreen_key(event)) continue;
      if (drc_key(event)) continue;
      if (gamepad_touch(event)) {
        // Consumed GamePad drags still move the visible stereo pointer.
        const bool motion=event.type==SDL_EVENT_MOUSE_MOTION;
        const bool button=event.type==SDL_EVENT_MOUSE_BUTTON_DOWN || event.type==SDL_EVENT_MOUSE_BUTTON_UP;
        if((motion||button) && overlay::window_target::selected.load()==overlay::window_target::Window::TV &&
           (motion?event.motion.windowID:event.button.windowID)==SDL_GetWindowID(R.tv.window)) {
          float nx,ny;
          tv_window_point(motion?event.motion.x:event.button.x,motion?event.motion.y:event.button.y,nx,ny);
          overlay::mouse_move(nx,ny);
        }
        continue;
      }
      input::handle_event(event);
      for (Screen *s : {&R.tv, &R.drc})
        if (s->window && event.window.windowID == SDL_GetWindowID(s->window)) {
          if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            s->width = event.window.data1;
            s->height = event.window.data2;
            s->resize = true;
          }
          if (s == &R.tv && (event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN ||
                             event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN))
            ::hostui::tv_fullscreen_changed();  // also when the window manager switched it
          if (event.type == SDL_EVENT_WINDOW_MINIMIZED)
            s->visible = false;
          if (event.type == SDL_EVENT_WINDOW_RESTORED ||
              event.type == SDL_EVENT_WINDOW_SHOWN)
            s->visible = gfx::hidden::mode().presents();  // Android shows its surface also in a hidden run
        }
    }
    input::update();
#ifndef __ANDROID__
    {
      const bool focused = R.tv.window && (SDL_GetWindowFlags(R.tv.window) & SDL_WINDOW_INPUT_FOCUS);
      const bool keep = !focused || overlay::is_open() || mods::mouse_captured() || overlay::pointer_layer_active();
      // A hidden pointer shown again for the overlay (or focus loss) gets a fresh 2 s once that ends.
      if (keep && !pointerKept && pointerHidden) mouseUsed = std::chrono::steady_clock::now();
      pointerKept = keep;
      const bool idle=std::chrono::steady_clock::now()-mouseUsed>std::chrono::seconds(2);
      const bool stereoPointer=overlay::window_target::selected.load()==overlay::window_target::Window::TV &&
          stereo::requestedMode!=stereo::Off && SDL_GetMouseFocus()==R.tv.window &&
          !mods::mouse_captured() && (overlay::captures() || !idle);
      overlay::set_stereo_pointer_visible(stereoPointer);
      const bool hide=stereoPointer || (!keep && idle);
      if(hide!=pointerHidden) {
        if(hide)SDL_HideCursor();else SDL_ShowCursor();
        pointerHidden=hide;
      }
    }
#endif
    ::hostui::run_posted();  // option changes from the settings overlay (render thread)
    test_drc_key();
    test_fullscreen_key();
    if (const int m = gfx::display_test_mode(frame_count()); m >= 0)
      ::hostui::set_drc_mode(m);
    overlay::set_density(SDL_GetWindowPixelDensity(R.tv.window));
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - titleTime).count();
    static auto polled = now;  // twice a second: frame interpolation's cap (and Android's display mode)
    if (now - polled >= std::chrono::milliseconds(500) || polled == now) {
      polled = now;
      display_rate::poll(R.tv.window);
    }
    if (elapsed >= 0.5 && !input::text_prompt_active()) {
      uint64_t frames = gx2::flips_presented();
      if ((exitFrame && frame_count() >= exitFrame) || dsweep::exit_requested()) {
        SDL_Event quit{};quit.type=SDL_EVENT_QUIT;SDL_PushEvent(&quit);
      }
      char title[160];
      int mode = interp::mode();
      std::snprintf(title, sizeof title,
          "The Legend of Zelda: The Wind Waker HD (Vulkan) — %.0f fps%s%s · %gx%s",
          double(frames - titleFrames) / elapsed,
          mode ? " · " : "", mode ? interp::mode_name() : "",
          double(requested_res_scale()), fxaa_enabled() ? " · FXAA" : "");
      if (gx2::uncapped()) std::strncat(title, " · UNCAPPED (debug)", sizeof title - std::strlen(title) - 1);
      SDL_SetWindowTitle(R.tv.window, title);
      titleFrames = frames;
      titleTime = now;
    }
    SDL_Delay(1);
  }
}
#endif  // WWHD_SDL_HOST

#if defined(__APPLE__) && !defined(WWHD_SDL_HOST)
// AppKit host: the TV / GamePad windows of gfx/display.mm, shared with the Metal renderer. Each view
// has a CAMetalLayer; Vulkan presents to it through VK_EXT_metal_surface (MoltenVK).
static const char *image_loaded(const char *part) {
  for (uint32_t i = 0, n = _dyld_image_count(); i < n; i++)
    if (const char *name = _dyld_get_image_name(i))
      if (strstr(name, part))
        return name;
  return nullptr;
}
// The macOS release ships the Vulkan loader and MoltenVK next to the game (tools/release/macos_vulkan.py;
// the game's rpath is its own folder, so that loader is the one dyld loads). Point the loader at the
// MoltenVK beside it unless the player chose drivers (VK_DRIVER_FILES / VK_ICD_FILENAMES); source
// builds without that file keep the loader's own search (Homebrew's MoltenVK).
static void use_bundled_driver() {
  for (const char *var : {"VK_DRIVER_FILES", "VK_ICD_FILENAMES"})
    if (const char *v = getenv(var); v && *v) {
      LOG("[vulkan] drivers from %s: %s", var, v);
      return;
    }
  std::string icd = host::exe_dir() + "/MoltenVK_icd.json";
  if (host::exe_dir().empty() || access(icd.c_str(), R_OK) != 0)
    return;
  setenv("VK_DRIVER_FILES", icd.c_str(), 1);
  LOG("[vulkan] driver: bundled MoltenVK (%s)", icd.c_str());
}
void init_appkit(void *tvLayer, void *drcLayer) {
  if (const char *e = getenv("WWHD_VK_FORCE_INIT_FAIL"); e && *e && strcmp(e, "0"))
    exception_report::raise("Vulkan start-up failure forced for testing (WWHD_VK_FORCE_INIT_FAIL)");
  // weak imports (CMakeLists.txt): a Mac without them still starts the game with Metal
  const char *loader = image_loaded("/libvulkan");
  if (!loader)
    exception_report::raise("The Vulkan loader (libvulkan) is missing. The release ships it next to the game "
                             "(libvulkan.1.dylib): run the setup again to repair it. Source builds: "
                             "brew install vulkan-loader molten-vk");
  LOG("[vulkan] loader: %s", loader);
#ifdef WWHD_GLSLANG_WEAK
  if (!image_loaded("/libglslang"))
    exception_report::raise("glslang (shader compiler for Vulkan) is not installed. "
                             "Install it with: brew install glslang");
#endif
  use_bundled_driver();
  load_global_functions(reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(RTLD_DEFAULT, "vkGetInstanceProcAddr")));
  if (!tvLayer)
    exception_report::raise("no TV window layer");
  R.tv.window = tvLayer;
  R.drc.window = drcLayer;
  init_device({VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_METAL_SURFACE_EXTENSION_NAME}, [&] {
    auto create = (PFN_vkCreateMetalSurfaceEXT)vkGetInstanceProcAddr(R.instance, "vkCreateMetalSurfaceEXT");
    if (!create)
      exception_report::raise("vkCreateMetalSurfaceEXT unavailable");
    for (Screen *s : {&R.tv, &R.drc}) {
      if (!s->window)
        continue;
      VkMetalSurfaceCreateInfoEXT ci{VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT};
      ci.pLayer = static_cast<const CAMetalLayer *>(s->window);
      vk_check(create(R.instance, &ci, nullptr, &s->surface), "create Metal surface");
    }
  });
  LOG("[vulkan] presenting to the AppKit windows through VK_EXT_metal_surface");
  load_saved_stereo_hdr();  // as the SDL host's hostui::load_saved_options
}
void screen_changed(int screen, bool visible) {
  Screen &s = screen ? R.drc : R.tv;
  s.visible = visible;
  s.resize = true;  // drawable size or backing scale may have changed
}
#endif
} // namespace gfxvk
