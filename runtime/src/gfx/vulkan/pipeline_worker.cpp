#include "pass.h"
#include "pipeline_worker.h"
#include "../headstart_limits.h"
#include "pipeline_identity.h"
#include "recipe_persistence.h"
#include "headstart.h"
#include "pipeline_demand.h"
#include "shader_compiler.h"
#include "platform/host.h"
#include "stutter_trace.h"
#include "platform/process_peak_rss.h"
#ifdef __APPLE__
#include <mach/mach.h>
#endif
#include "worker_metrics.h"
#include "../../pipeline_groups.h"
#include <deque>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <zlib.h>

namespace gfxvk {
namespace {
std::string identity(const std::vector<uint8_t>& key) {
  return {reinterpret_cast<const char*>(key.data()), key.size()};
}
void destroy(VkDevice device, WarmPipeline& p) {
  if (p.pipeline) vkDestroyPipeline(device, p.pipeline, nullptr);
  if (p.layout) vkDestroyPipelineLayout(device, p.layout, nullptr);
  for (auto set : p.sets) if (set) vkDestroyDescriptorSetLayout(device, set, nullptr);
  p = {};
}
struct Ready { WarmPipeline handles; std::shared_ptr<PipelineRecipe> recipe; uint64_t sequence = 0; };
struct Worker {
  VkDevice device = VK_NULL_HANDLE;
  PipelineRecipeFingerprint fingerprint{};
  std::string path;
  std::string headstartPath;
  PipelineWarmupResult result;
  std::mutex mutex;
  std::mutex persistenceMutex; // Explicit checkpoint and periodic saver share one writer.
  std::thread thread, saver;
  std::condition_variable saveWake;
  bool saveStopped = false;
  RecipeSaveRevision revision;
  std::chrono::steady_clock::time_point lastSave = std::chrono::steady_clock::now();
  bool stopped = false;
  std::shared_mutex cacheMutex;
  VkPipelineCache cache = VK_NULL_HANDLE; // Published/mutated under cacheMutex.
  std::atomic<bool> cacheChanged{false};
  std::unordered_map<std::string, Ready> ready;
  std::unordered_map<std::string, std::shared_ptr<PipelineRecipe>> recipes;
  // first-seen order of the recipes (loaded ones in file order): snapshots keep it, so the next start warms them in
  // the order the game first needed them (boot and title first)
  std::unordered_map<std::string, uint64_t> order;
  uint64_t nextOrder = 0;
  std::unordered_set<std::string> consumed;
  size_t recipeBytes = 0;
  RecipeShaderPool shaderPool;
  // Grouped recipes (the setup's model sweep): compiled when one of their object archives is loaded in play.
  std::unordered_map<uint32_t, std::vector<std::string>> byGroup;
  std::unordered_set<uint32_t> resident;      // groups resident in the game (last published set)
  std::unordered_set<uint32_t> prefetched;    // groups queued since they became resident
  std::deque<std::string> prefetch;           // keys of newly resident groups, compiled before startup work
  std::unordered_set<std::string> compiling;  // keys a prefetch compiler is building now
  // keys whose driver compile is running now (a subset of compiling), and the signal when one finishes: a demand for
  // one of them waits for it instead of compiling the same pipeline again (take_warm_pipeline, WWHD_VK_DEMAND_PROMOTE)
  std::unordered_set<std::string> inDriver;
  std::condition_variable driverDone;
  uint64_t promotions = 0, promotedHits = 0, promotedWaitNs = 0;
  uint64_t residentGeneration = 0, readySequence = 0;
  size_t prefetchedReady = 0;
  bool drainExit = false;                     // finish/offline warming: leave once the queues are empty
  std::condition_variable workWake;
  ~Worker() {
    { std::lock_guard lock(mutex); stopped = true; saveStopped = true; }
    saveWake.notify_all(); workWake.notify_all();
    if (thread.joinable()) thread.join();
    if (saver.joinable()) saver.join();
    for (auto& [key, entry] : ready) destroy(device, entry.handles);
    if (cache) vkDestroyPipelineCache(device, cache, nullptr);
  }
};
Worker& worker() { static Worker value; return value; }
// Native driver allocations have no portable byte-size query. Bound retained
// unused handles independently of the CPU recipe database; evicted recipes and
// the driver cache remain available for a correct demand compile.
#ifdef __ANDROID__
constexpr size_t kMaxUnusedWarmPipelines = 256;
#else
constexpr size_t kMaxUnusedWarmPipelines = 1024;
#endif
// The limit exists for memory only. From the device's memory budget (VK_EXT_memory_budget's heap budget, else the
// device-local heap size; MoltenVK reports Metal's recommendedMaxWorkingSetSize there): a small share of it at a
// measured ~25 KB per ready pipeline on MoltenVK (64 KB assumed for headroom), within [kMin, kMax]; without a budget
// the fixed value above. WWHD_VK_WARM_POOL=n overrides it (measurements).
std::atomic<uint64_t> g_memoryBudget{0};
size_t max_unused_warm() {
  static const size_t value = [] {
    if (const char* e = std::getenv("WWHD_VK_WARM_POOL"); e && *e)
      return std::clamp<size_t>(std::strtoul(e, nullptr, 10), 64, 65536);
#ifdef __ANDROID__
    constexpr double kShare = 0.01; constexpr size_t kMin = 256, kMax = 1024;
#else
    constexpr double kShare = 0.03; constexpr size_t kMin = 1024, kMax = 16384;
#endif
    const uint64_t budget = g_memoryBudget.load();
    if (!budget) return kMaxUnusedWarmPipelines;
    return std::clamp<size_t>(size_t(double(budget) * kShare / (64.0 * 1024)), kMin, kMax);
  }();
  return value;
}
#define maxUnusedWarmPipelines max_unused_warm()
std::atomic<size_t> g_evictions{0};
// the process's memory now (Apple: its physical footprint, which includes the GPU's allocations on unified memory;
// elsewhere the peak resident size), for the warm's log lines
uint64_t process_memory() {
#ifdef __APPLE__
  task_vm_info_data_t info{};
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
    return info.phys_footprint;
  return 0;
#else
  process_peak_rss::Sample sample;
  uint64_t bytes = 0;
  return process_peak_rss::query(sample) && process_peak_rss::bytes(sample, bytes) ? bytes : 0;
#endif
}
// Archive prefetch compilers: a few on desktops (an archive brings up to dozens of pipelines a few frames before its
// first draw), one on phones (memory, few big cores).
size_t prefetch_threads() {
  if (const char* e = std::getenv("WWHD_VK_PREFETCH_THREADS"); e && *e) return std::clamp<size_t>(std::strtoul(e, nullptr, 10), 1, 8);
#ifdef __ANDROID__
  return 1;
#else
  const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
  return std::clamp<size_t>(cores > 4 ? cores / 4 : 1, 1, 3);
#endif
}
// Prefetch waits while the render thread's own (demand) compiler is busy: a pipeline needed now comes first.
// WWHD_VK_PREFETCH_YIELD=0 (measurements) lets it run on.
bool prefetch_yields() {
  static const bool value = [] { const char* e = std::getenv("WWHD_VK_PREFETCH_YIELD"); return !e || std::strcmp(e, "0"); }();
  return value;
}
// Recipes derived from the head start's Metal recipes (only the speculative mark, pipeline_recipe.h) in play:
// Background (default): a throttled queue after all other work; Start: with the start queue, as before (they made
// the first visits smoother on the phone, but compiling ~1,000 of them at start took the 1% lows down);
// Off: never compiled ahead (a demand still finds them in the driver cache the preparation filled).
// WWHD_VK_METAL_QUEUE=background|start|off; play (measurements): the background queue without its free run before
// play, as for a save state loaded straight after start. The rest after each compile: WWHD_VK_METAL_QUEUE_REST (a multiple of the
// compile's time, default 4: at most a fifth of one core) and at least WWHD_VK_METAL_QUEUE_REST_MS (default 8).
enum class SpeculativeMode { Background, Start, Off, PlayOnly };
SpeculativeMode speculative_mode() {
  static const SpeculativeMode value = [] {
    const char* e = std::getenv("WWHD_VK_METAL_QUEUE");
    if (e && !std::strcmp(e, "start")) return SpeculativeMode::Start;
    if (e && !std::strcmp(e, "off")) return SpeculativeMode::Off;
    if (e && !std::strcmp(e, "play")) return SpeculativeMode::PlayOnly;
    return SpeculativeMode::Background;
  }();
  return value;
}
#define speculativeMode speculative_mode()
const char* speculative_mode_name() {
  switch (speculative_mode()) {
  case SpeculativeMode::Start: return "with the start queue (WWHD_VK_METAL_QUEUE=start)";
  case SpeculativeMode::Off: return "not compiled ahead (WWHD_VK_METAL_QUEUE=off)";
  case SpeculativeMode::PlayOnly: return "throttled background queue from play on (WWHD_VK_METAL_QUEUE=play)";
  default: return "throttled background queue";
  }
}
unsigned speculative_rest_factor() {
  static const unsigned value = [] { const char* e = std::getenv("WWHD_VK_METAL_QUEUE_REST"); return e && *e ? std::clamp<unsigned>(std::strtoul(e, nullptr, 10), 0, 100) : 4u; }();
  return value;
}
unsigned speculative_rest_min_ms() {
  static const unsigned value = [] { const char* e = std::getenv("WWHD_VK_METAL_QUEUE_REST_MS"); return e && *e ? std::clamp<unsigned>(std::strtoul(e, nullptr, 10), 0, 1000) : 8u; }();
  return value;
}
bool group_resident(const Worker& w, const PipelineRecipe& recipe) {
  for (auto group : recipe.groups) if (w.resident.contains(group)) return true;
  return false;
}
// Compiling ahead stops while the ready pipelines fill the limit with ones compiled at start (ungrouped: the boot's,
// the title's and the other generic ones, needed anywhere): it never pushes those out.
bool room_for_prefetch(const Worker& w) {
  if (w.ready.size() + 64 < maxUnusedWarmPipelines) return true;
  for (const auto& [key, entry] : w.ready)
    if (!entry.recipe->groups.empty()) return true;
  return false;
}
// Full: the entry least likely to be drawn soon goes first: one whose archives are all unloaded, else the oldest
// compiled ahead for an archive (the newest load is drawn next), else the oldest.
void retain_ready(Worker& w, const std::string& key, WarmPipeline handles,
                  const std::shared_ptr<PipelineRecipe>& recipe) {
  if(w.ready.size() >= maxUnusedWarmPipelines) {
    auto victim=w.ready.end();
    // rank: 2 = only unloaded archives, 1 = grouped, 0 = ungrouped (compiled at start)
    auto rank=[&](const Ready& r){ return r.recipe->groups.empty() ? 0 : group_resident(w,*r.recipe) ? 1 : 2; };
    int victimRank=-1;
    for (auto it=w.ready.begin();it!=w.ready.end();++it) {
      const int itRank=rank(it->second);
      if (victim==w.ready.end() || itRank>victimRank || (itRank==victimRank && it->second.sequence<victim->second.sequence))
        victim=it, victimRank=itRank;
    }
    destroy(w.device,victim->second.handles); w.ready.erase(victim);
    ++g_evictions;
    stutter::trace().add("warm_evicted", 0, 0, 0, 0, 0);
  }
  w.ready.emplace(key,Ready{handles,recipe,++w.readySequence});
}

// Bytes a recipe holds besides its shared, packed shaders (counted once each in the pool).
size_t footprint(const PipelineRecipe& p) {
  size_t size = 2048 + p.key.size() + p.groups.size() * 4;
  for (int i = 0; i < 2; ++i) size += p.descriptors[i].size() * sizeof(VkDescriptorSetLayoutBinding);
  return size;
}
void index_groups(Worker& w, const std::string& key, const PipelineRecipe& recipe) {
  bool archives = false;
  for (auto group : recipe.groups) {
    if (group == kSpeculativeGroup) continue;  // a mark, not an archive (pipeline_recipe.h)
    archives = true;
    auto& keys = w.byGroup[group];
    if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
  }
  if (archives) pipeline_groups::want();
}
bool insert_recipe(Worker& w, const std::shared_ptr<PipelineRecipe>& recipe) {
  auto key = identity(recipe->key);
  auto found = w.recipes.find(key);
  // A pipeline recorded in play keeps the archive groups the sweep found for it (the new recipe is not shared yet).
  // The speculative mark (a recipe derived from the head start's Metal recipes) stays only while both have it: a
  // pipeline the game asked for here is an ordinary recipe from then on.
  if (found != w.recipes.end()) {
    auto marked = [](const PipelineRecipe& r) { return std::find(r.groups.begin(), r.groups.end(), kSpeculativeGroup) != r.groups.end(); };
    const bool keepMark = marked(*recipe) && marked(*found->second);
    for (auto group : found->second->groups) if (group != kSpeculativeGroup) recipe->add_group(group);
    if (!keepMark) std::erase(recipe->groups, kSpeculativeGroup);
  }
  const size_t old = found == w.recipes.end() ? 0 : footprint(*found->second);
  const size_t size = footprint(*recipe);
  if ((found == w.recipes.end() && w.recipes.size() >= kPipelineRecipeMaxCount) ||
      size > kPipelineRecipeMaxBytes - std::min(kPipelineRecipeMaxBytes, w.recipeBytes - old)) return false;
  const size_t packed = w.shaderPool.packedBytes;
  w.shaderPool.freeze(*recipe);
  w.recipeBytes += size - old + (w.shaderPool.packedBytes - packed);
  index_groups(w, key, *recipe);
  w.order.try_emplace(key, w.nextOrder++);
  w.recipes.insert_or_assign(std::move(key), recipe);
  return true;
}
void changed(Worker& w) {
  w.revision.changed(); w.saveWake.notify_one();
}
std::atomic<int64_t> g_durableNs{0};  // steady-clock start of the last snapshot written (pipeline_recipes_durable_ns)
int64_t steady_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void persist_snapshot(Worker& w) {
  std::lock_guard writer(w.persistenceMutex);
  std::vector<std::shared_ptr<PipelineRecipe>> snapshot;
  uint64_t revision;
  const int64_t started = steady_ns();
  {
    std::lock_guard lock(w.mutex);
    if (!w.revision.dirty()) return;
    revision = w.revision.current;
    std::vector<std::pair<uint64_t, std::shared_ptr<PipelineRecipe>>> ordered;
    ordered.reserve(w.recipes.size());
    for (auto& [key, recipe] : w.recipes) {
      auto at = w.order.find(key);
      ordered.emplace_back(at == w.order.end() ? UINT64_MAX : at->second, recipe);
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    snapshot.reserve(ordered.size());
    for (auto& entry : ordered) snapshot.push_back(std::move(entry.second));
  }
  try {
    auto bytes = encode_local_pipeline_recipes(w.fingerprint, snapshot);
    atomic_recipe_write(w.path, bytes, host::replace_file);
    {
      std::lock_guard lock(w.mutex);
      w.revision.complete(revision);
    }
    g_durableNs.store(started, std::memory_order_release);
    std::fprintf(stderr, "[vulkan warmup] saved %zu recipes (%zu bytes)\n", snapshot.size(), bytes.size());
  } catch (const std::exception& error) {
    std::fprintf(stderr, "[vulkan warmup] save failed: %s\n", error.what());
  }
  std::lock_guard lock(w.mutex); w.lastSave = std::chrono::steady_clock::now();
}
void save_loop(Worker& w) {
  host::set_thread_name("Vulkan recipe save");
  std::unique_lock lock(w.mutex);
  while (!w.saveStopped) {
    w.saveWake.wait(lock, [&] { return w.saveStopped || w.revision.dirty(); });
    if (w.saveStopped) break;
    // Only one bounded snapshot at a time; failures also back off for two
    // minutes, and an explicit checkpoint always saves the latest revision.
    // (the setup's model sweep: every few seconds, so a sweep that stops the game loses little; dsweep.cpp marks
    // its work done only once it is saved, pipeline_recipes_durable_ns)
    static const int every = std::getenv("WWHD_DSWEEP") ? 5 : 120;
    auto due = w.lastSave + std::chrono::seconds(every);
    if (w.saveWake.wait_until(lock, due, [&] { return w.saveStopped; })) break;
    lock.unlock();
    try { persist_snapshot(w); }
    catch (const std::exception& error) {
      std::fprintf(stderr, "[vulkan warmup] snapshot failed: %s\n", error.what());
      std::lock_guard retry(w.mutex); w.lastSave = std::chrono::steady_clock::now();
    }
    lock.lock();
  }
}
VkResult compile(VkDevice device, VkPipelineCache cache, PipelineRecipe& recipe, WarmPipeline& out,
                 const char* traceKind = "startup_pipeline") {
  VkShaderModule modules[2]{};
  auto cleanup = [&] {
    for (auto module : modules) if (module) vkDestroyShaderModule(device, module, nullptr);
  };
  VkResult result = VK_SUCCESS;
  for (int stage = 0; stage < 2 && result == VK_SUCCESS; ++stage) {
    VkDescriptorSetLayoutCreateInfo descriptor{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptor.bindingCount = recipe.descriptors[stage].size();
    descriptor.pBindings = recipe.descriptors[stage].data();
    result = vkCreateDescriptorSetLayout(device, &descriptor, nullptr, &out.sets[stage]);
    if (result != VK_SUCCESS) break;
    const auto words = recipe.words(stage);
    VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    module.codeSize = words.size() * 4; module.pCode = words.data();
    result = vkCreateShaderModule(device, &module, nullptr, &modules[stage]);
  }
  if (result == VK_SUCCESS) {
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 2; layout.pSetLayouts = out.sets;
    result = vkCreatePipelineLayout(device, &layout, nullptr, &out.layout);
  }
  if (result == VK_SUCCESS) {
    // Rebind pointers on a private copy: the persistence thread may serialize
    // this immutable recipe concurrently.
    auto state = *recipe.state;
    auto info = *state.get();
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (int i = 0; i < 2; ++i) {
      stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
      stages[i].pName = "main"; stages[i].module = modules[i];
    }
    info.pStages = stages; info.layout = out.layout;
    if (R.renderPasses) render_pass_pipeline_targets(info, state.rendering_targets());
    const auto cpuStart = stutter::trace().enabled() ? stutter::thread_cpu_now() : 0;
    const auto started = stutter::trace().enabled() ? stutter::now() : 0;
    result = vkCreateGraphicsPipelines(device, cache, 1, &info, nullptr, &out.pipeline);
    if (started) stutter::trace().add(traceKind, 0, std::hash<std::string>{}(identity(recipe.key)),
        started, stutter::now(), int(result), stutter::cpu_elapsed(cpuStart));
  }
  cleanup();
  if (result != VK_SUCCESS) destroy(device, out);
  else out.dynamicUniforms = recipe.dynamicUniforms;
  return result;
}
std::vector<std::shared_ptr<PipelineRecipe>> read_recipes(Worker& w, bool headstart) {
  const auto& path = headstart ? w.headstartPath : w.path;
  if (path.empty() || path == "0") return {};
  std::error_code ec;
  const auto size = std::filesystem::file_size(std::filesystem::u8path(path), ec);
  if (ec) return {};
  if (size > (headstart ? kHeadstartMaxFileBytes : kPipelineRecipeMaxBytes)) throw std::runtime_error("recipe file size limit");
  std::unique_ptr<FILE, decltype(&std::fclose)> file(open_utf8_file(path, "rb"), &std::fclose);
  if (!file) return {};
  if (!headstart) {
    std::vector<uint8_t> bytes(size);
    if (std::fread(bytes.data(), 1, size, file.get()) != size || std::fgetc(file.get()) != EOF || std::ferror(file.get()))
      throw std::runtime_error("recipe read failed");
    return decode_pipeline_recipes(w.fingerprint, bytes);
  }
  auto word = [](const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
  };
  std::vector<std::shared_ptr<PipelineRecipe>> result;
  uint64_t consumed = 0, rawBytes = 0, records = 0;
  while (consumed < size) {
    uint8_t header[12];
    if (size - consumed < 12 || std::fread(header, 1, 12, file.get()) != 12)
      throw std::runtime_error("truncated head-start header");
    consumed += 12;
    auto type = word(header), rawSize = word(header + 4), compressed = word(header + 8);
    if (++records > kHeadstartMaxRecords || type < 1 || type > 6 || !rawSize || rawSize > kHeadstartMaxRecordBytes ||
        !compressed || compressed > kHeadstartMaxRecordBytes || compressed > size - consumed ||
        rawSize > kHeadstartMaxRawBytes - rawBytes) throw std::runtime_error("head-start record bounds");
    rawBytes += rawSize; consumed += compressed;
    if (type != 4) {
      if (std::fseek(file.get(), compressed, SEEK_CUR)) throw std::runtime_error("head-start seek failed");
      continue;
    }
    std::vector<uint8_t> encoded(compressed), raw(rawSize);
    if (std::fread(encoded.data(), 1, compressed, file.get()) != compressed)
      throw std::runtime_error("truncated pipeline head start");
    uLongf decoded = rawSize;
    if (uncompress(raw.data(), &decoded, encoded.data(), compressed) != Z_OK || decoded != rawSize)
      throw std::runtime_error("invalid pipeline head-start compression");
    // Different device/schema: ignore the local recipe without ever handing it
    // to Vulkan. Shader records remain useful across devices.
    if (raw.size() < 64 || std::memcmp(raw.data(), "WWVKPR01", 8))
      throw std::runtime_error("invalid pipeline head-start database");
    if (std::memcmp(raw.data() + 8, w.fingerprint.data(), w.fingerprint.size())) continue;
    auto recipes = decode_pipeline_recipes(w.fingerprint, raw);
    if (recipes.size() > kPipelineRecipeMaxCount - result.size()) throw std::runtime_error("head-start pipeline count limit");
    result.insert(result.end(), recipes.begin(), recipes.end());
  }
  return result;
}
void run(Worker& w, std::vector<uint8_t> seed) {
  host::set_thread_name("Vulkan warmup");
  VkPipelineCache cache = VK_NULL_HANDLE;
  size_t completed = 0, failed = 0, cancelled = 0;
  struct WarmupMetrics {
    size_t &completed, &failed;
    uint64_t started = stutter::trace().enabled() ? stutter::now() : 0;
    uint64_t cpu = started ? stutter::thread_cpu_now() : 0;
    ~WarmupMetrics() {
      if (started) stutter::trace().add("startup_warmup", 0, completed, started,
          stutter::now(), int(failed), stutter::cpu_elapsed(cpu));
    }
  } metrics{completed, failed};
  try {
    const auto began = std::chrono::steady_clock::now();
    auto msSince = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count(); };
    std::vector<std::shared_ptr<PipelineRecipe>> loaded;
    std::unordered_set<std::string> seen;
    for (bool headstart : {false, true}) {
      try {
        for (auto& recipe : read_recipes(w, headstart))
          if (loaded.size() < kPipelineRecipeMaxCount && seen.insert(identity(recipe->key)).second) loaded.push_back(recipe);
      } catch (const std::exception& error) {
        if (headstart) ++failed;
        std::fprintf(stderr, "[vulkan warmup] %s ignored: %s\n", headstart ? "head start" : "local recipes", error.what());
      }
    }
    // The worker's driver cache exists also without recipes to warm now: setup's warm (a fresh setup has no local
    // recipes yet) compiles the head start's pipelines into it afterwards (warm_resolved_pipeline_recipes), and only
    // this cache is merged into the persisted one. Without it they were compiled with no cache and lost on exit.
    VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    ci.initialDataSize = seed.size(); ci.pInitialData = seed.data();
    if (vkCreatePipelineCache(w.device, &ci, nullptr, &cache) != VK_SUCCESS) {
      ci.initialDataSize = 0; ci.pInitialData = nullptr;
      if (vkCreatePipelineCache(w.device, &ci, nullptr, &cache) != VK_SUCCESS)
        throw std::runtime_error("worker pipeline cache creation failed");
    }
    { std::unique_lock lock(w.cacheMutex); w.cache = cache; }
    std::vector<uint8_t>().swap(seed); // Vulkan copied initial cache data during creation.
    if (loaded.empty()) { w.result = {0, failed}; return; }
    const double readMs = msSince();
    std::deque<std::shared_ptr<PipelineRecipe>> startup;
    std::deque<std::string> speculative;  // (under w.mutex) the throttled background queue
    size_t grouped = 0;
    {
      std::lock_guard lock(w.mutex);
      std::erase_if(loaded, [&](const auto& recipe) {
        return w.consumed.contains(identity(recipe->key)) || !insert_recipe(w, recipe);
      });
      // Grouped recipes wait for their archives; the rest (seen in play, the head start) are warmed now. Recipes
      // derived from the head start's Metal recipes (only the speculative mark) go to the throttled background queue.
      for (auto& recipe : loaded) {
        if (recipe->groups.empty()) startup.push_back(recipe);
        else if (recipe->speculative_only()) {
          if (speculativeMode == SpeculativeMode::Start) startup.push_back(recipe);
          else if (speculativeMode == SpeculativeMode::Background || speculativeMode == SpeculativeMode::PlayOnly)
            speculative.push_back(identity(recipe->key));
        } else ++grouped;
      }
    }
    // the setup's model sweep, record only (WWHD_DSWEEP_RECORD_ONLY=1): the recipes are kept (and saved with the new
    // ones), but nothing is compiled ahead
    static const bool recordOnly = [] { const char* e = std::getenv("WWHD_DSWEEP_RECORD_ONLY"); return e && *e == '1'; }();
    if (recordOnly) {
      std::fprintf(stderr, "[vulkan warmup] record only: %zu recipes kept, none compiled\n", loaded.size());
      w.result = {0, failed};
      return;
    }
    std::fprintf(stderr, "[vulkan warmup] %zu exact pipeline recipes queued, %zu wait for their object archives "
                 "(pool %zu; process memory %.0f MB; read %.0f ms, ready to compile after %.0f ms)\n", startup.size(),
                 grouped, maxUnusedWarmPipelines, process_memory() / 1e6, readMs, msSince());
    // (the game's stage scan tells when play begins: pipeline_groups::playing)
    if (!speculative.empty()) pipeline_groups::want_playing();
    if (!speculative.empty() || speculativeMode == SpeculativeMode::Start || speculativeMode == SpeculativeMode::Off)
      std::fprintf(stderr, "[vulkan warmup] Metal-derived recipes: %s (%zu in the background queue)\n",
                   speculative_mode_name(), speculative.size());
    // Several compilers for archive prefetch (an archive's models are drawn a few frames after it loads); the
    // driver pipeline cache is internally synchronized. Startup work stays one at a time and yields to demand.
    std::atomic<size_t> prefetched{0}, done{0}, failures{0}, cancels{0};
    size_t startupTaken = 0;  // (under w.mutex)
    std::deque<std::string> background;
    bool backgroundPlanned = false;
    constexpr size_t kStartHead = 384;
    std::atomic<size_t> threadCount{0}, speculativeDone{0};
    std::chrono::steady_clock::time_point speculativeNext{};  // (under w.mutex) the throttle
    bool speculativeInPlay = false;  // (under w.mutex)
    auto loop = [&] {
      const size_t index = threadCount++;  // 0: the first compiler, the only one the background uses until play
      host::set_thread_name("Vulkan warmup");
      host::lower_thread_priority();  // the game's own threads (and its demand compiles) always win
      for (;;) {
        std::shared_ptr<PipelineRecipe> recipe;
        bool ahead = false, head = false, spec = false, specThrottled = false;
        {
          std::unique_lock lock(w.mutex);
          for (;;) {
            if (w.stopped) break;
            // Newly loaded archives: their recipes go before any startup work, and before the rest of earlier
            // archives' (the newest load is the one about to be drawn: a room the player enters must not wait
            // behind archives loaded long before).
            if (!w.byGroup.empty() && pipeline_groups::generation() != w.residentGeneration) {
              w.residentGeneration = pipeline_groups::generation();
              const auto now = pipeline_groups::resident();
              w.resident = std::unordered_set<uint32_t>(now.begin(), now.end());
              std::erase_if(w.prefetched, [&](uint32_t group) { return !w.resident.contains(group); });
              std::vector<std::string> fresh;
              for (auto group : now) {
                auto found = w.byGroup.find(group);
                if (found == w.byGroup.end() || !w.prefetched.insert(group).second) continue;
                fresh.insert(fresh.end(), found->second.begin(), found->second.end());
              }
              w.prefetch.insert(w.prefetch.begin(), fresh.begin(), fresh.end());
              w.workWake.notify_all();
            }
            while (!w.prefetch.empty() && !recipe && room_for_prefetch(w)) {
              auto key = std::move(w.prefetch.front()); w.prefetch.pop_front();
              auto found = w.recipes.find(key);
              if (found == w.recipes.end() || w.consumed.contains(key) || w.ready.contains(key) ||
                  w.compiling.contains(key)) continue;
              recipe = found->second; ahead = true;
            }
            if (recipe) break;
            if (!startup.empty()) {
              recipe = std::move(startup.front()); startup.pop_front();
              head = startupTaken++ < kStartHead;
              if (startup.empty())  // (the last one is still compiling: the size is close, not exact)
                std::fprintf(stderr, "[vulkan warmup] start queue taken: %zu ready; process memory %.0f MB\n",
                             w.ready.size(), process_memory() / 1e6);
              break;
            }
            // Then, when the pool can hold every recipe (desktops: the pool is sized by the memory budget), the
            // grouped rest in first-seen order: compiled ahead in the background, so a place whose archives load
            // with many pipelines at once does not have to wait for its prefetch. Newly loaded archives still go
            // first; this work yields to the game's own compiles like the start queue.
            if (!backgroundPlanned) {
              backgroundPlanned = true;
              // (debug: WWHD_VK_BACKGROUND_WARM=0 leaves it out, for measurements)
              static const bool off = std::getenv("WWHD_VK_BACKGROUND_WARM") && !std::atoi(std::getenv("WWHD_VK_BACKGROUND_WARM"));
              if (!off && w.recipes.size() + 64 <= maxUnusedWarmPipelines) {
                std::vector<std::pair<uint64_t, std::string>> rest;
                for (const auto& [key, r] : w.recipes)
                  if (!r->groups.empty() && !r->speculative_only()) {
                    auto at = w.order.find(key);
                    rest.emplace_back(at == w.order.end() ? UINT64_MAX : at->second, key);
                  }
                std::sort(rest.begin(), rest.end());
                for (auto& entry : rest) background.push_back(std::move(entry.second));
                std::fprintf(stderr, "[vulkan warmup] %zu grouped recipes compiled in the background (pool %zu)\n",
                             background.size(), maxUnusedWarmPipelines);
              }
            }
            // (until the game plays, one thread: the boot, the file select and the loads into the first stage keep
            // their CPU; afterwards all of them)
            while (!background.empty() && !recipe && (index == 0 || pipeline_groups::playing())) {
              auto key = std::move(background.front()); background.pop_front();
              auto found = w.recipes.find(key);
              if (found == w.recipes.end() || w.consumed.contains(key) || w.ready.contains(key) ||
                  w.compiling.contains(key)) continue;
              recipe = found->second;
              if (background.empty())
                std::fprintf(stderr, "[vulkan warmup] background taken: %zu ready; process memory %.0f MB\n",
                             w.ready.size(), process_memory() / 1e6);
            }
            if (recipe) break;
            // Last, the Metal-derived recipes: one thread, only while the pool has room (they never push out a pipeline
            // compiled at start), waiting while the game's own compiles run (below). Before play (the boot, the title,
            // the file select, the first loads) back to back; in play throttled: after each compile the queue rests a
            // multiple of its time, so it takes a small share of one core.
            if (!speculative.empty() && index == 0 && (speculativeMode != SpeculativeMode::PlayOnly || pipeline_groups::playing())) {
              const bool playing = pipeline_groups::playing();
              if (w.ready.size() + 64 >= maxUnusedWarmPipelines) {
                std::fprintf(stderr, "[vulkan warmup] Metal-derived background queue stopped: the pool is full (%zu "
                             "compiled, %zu left for demand)\n", speculativeDone.load(), speculative.size());
                speculative.clear();
              } else if (!playing || std::chrono::steady_clock::now() >= speculativeNext) {
                while (!speculative.empty() && !recipe) {
                  auto key = std::move(speculative.front()); speculative.pop_front();
                  auto found = w.recipes.find(key);
                  if (found == w.recipes.end() || w.consumed.contains(key) || w.ready.contains(key) ||
                      w.compiling.contains(key) || !found->second->speculative_only()) continue;
                  recipe = found->second; spec = true; specThrottled = playing;
                  if (playing && !speculativeInPlay) {
                    speculativeInPlay = true;
                    std::fprintf(stderr, "[vulkan warmup] play began: %zu Metal-derived recipes compiled before it, %zu "
                                 "left for the throttled queue\n", speculativeDone.load(), speculative.size() + 1);
                  }
                }
                if (speculative.empty())
                  std::fprintf(stderr, "[vulkan warmup] Metal-derived background queue taken: %zu compiled\n",
                               speculativeDone.load() + (recipe ? 1 : 0));
              }
            }
            if (recipe) break;
            if (w.drainExit || (w.byGroup.empty() && speculative.empty())) break;
            w.workWake.wait_for(lock, std::chrono::milliseconds(speculative.empty() ? 50 : 10));
          }
          if (recipe) w.compiling.insert(identity(recipe->key));
        }
        if (!recipe) break;
        auto key = identity(recipe->key);
        auto finished = [&] { std::lock_guard lock(w.mutex); w.compiling.erase(key); };
        // Demand has its own single compiler; startup work waits while it is busy. A driver call already in
        // flight cannot be cancelled.
        // (the head of the start queue - in first-seen order the boot's, title's and file select's - never waits:
        // the game is about to draw it)
        while (!head && (!ahead || prefetch_yields()) && (pipeline_demand_active() || vk::shader_compiler_active())) {
          { std::lock_guard lock(w.mutex); if (w.stopped) break; }
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        {
          std::lock_guard lock(w.mutex);
          if (w.stopped) { w.compiling.erase(key); break; }
          if (w.consumed.contains(key)) { w.compiling.erase(key); ++cancels; continue; }
        }
        WarmPipeline result;
        VkResult compiled;
        { std::lock_guard lock(w.mutex); w.inDriver.insert(key); }
        {
          std::shared_lock cacheLock(w.cacheMutex);
          const auto began = std::chrono::steady_clock::now();
          compiled = compile(w.device, cache, *recipe, result, ahead ? "prefetch_pipeline" : spec ? "speculative_pipeline" : "startup_pipeline");
          if (compiled == VK_SUCCESS) w.cacheChanged = true;
          if (spec) {
            const auto now = std::chrono::steady_clock::now();
            std::lock_guard lock(w.mutex);
            if (specThrottled) speculativeNext = now + std::max(std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::milliseconds(speculative_rest_min_ms())), (now - began) * speculative_rest_factor());
            if (compiled == VK_SUCCESS) ++speculativeDone;
          }
        }
        if (compiled != VK_SUCCESS) {
          { std::lock_guard lock(w.mutex); w.inDriver.erase(key); }
          w.driverDone.notify_all();
          finished(); ++failures; continue;
        }
        bool publish;
        {
          std::lock_guard lock(w.mutex);
          w.compiling.erase(key);
          w.inDriver.erase(key);
          publish = !w.stopped && !w.consumed.contains(key);
          if (publish) {
            try { retain_ready(w,key,result,recipe); }
            catch (...) { destroy(w.device,result); w.driverDone.notify_all(); throw; }
          }
        }
        w.driverDone.notify_all();
        if (publish) { ++done; prefetched += ahead; }
        else { destroy(w.device, result); ++cancels; }
      }
    };
    const size_t helpers = grouped ? prefetch_threads() - 1 : 0;
    std::vector<std::thread> pool;
    for (size_t i = 0; i < helpers; ++i) pool.emplace_back([&] { try { loop(); } catch (...) {} });
    loop();
    { std::lock_guard lock(w.mutex); w.drainExit = true; }  // helpers leave too once the queues are empty
    w.workWake.notify_all();
    for (auto& thread : pool) thread.join();
    completed += done; failed += failures; cancelled += cancels;
    if (grouped) std::fprintf(stderr, "[vulkan warmup] %zu compiled ahead for loaded object archives\n", prefetched.load());
    if (speculativeDone) std::fprintf(stderr, "[vulkan warmup] %zu Metal-derived recipes compiled in the background\n", speculativeDone.load());
    std::fprintf(stderr, "[vulkan warmup] %zu ready, %zu failed, %zu cancelled; %zu evicted from the pool of %zu; process "
                 "memory %.0f MB\n", completed, failed, cancelled, g_evictions.load(), maxUnusedWarmPipelines,
                 process_memory() / 1e6);
  } catch (const std::exception& error) {
    ++failed;
    std::fprintf(stderr, "[vulkan warmup] skipped: %s\n", error.what());
  }
  w.result = {completed, failed};
}

}
void set_memory_budget(uint64_t bytes) { g_memoryBudget.store(bytes); }
void start_pipeline_worker(VkDevice device, PipelineRecipeFingerprint fingerprint,
    std::string path, std::vector<uint8_t> cacheSeed, std::string headstartPath) {
  // Construct the trace first so it outlives a worker still finishing during
  // static destruction after an exceptional shutdown.
  (void)stutter::trace();
  (void)vk::shader_compiler_active(); // Demand state must outlive startup-worker shutdown.
  auto& w = worker();
  if (w.device || path.empty()) return;
  w.device = device; w.fingerprint = fingerprint; w.path = std::move(path);
  w.headstartPath = std::move(headstartPath);
  w.saver = std::thread(save_loop, std::ref(w));
  w.thread = std::thread(run, std::ref(w), std::move(cacheSeed));
}
bool take_warm_pipeline(const std::vector<uint8_t>& key,
    const std::array<std::vector<uint32_t>, 2>& spirv,
    const std::array<std::vector<VkDescriptorSetLayoutBinding>, 2>& descriptors,
    bool dynamicUniforms, const GraphicsPipelineState& currentState, WarmPipeline& out) {
  auto& w = worker();
  if (!w.device) return false;
  Ready entry;
  {
    std::unique_lock lock(w.mutex);
    auto id = identity(key);
    // Demand promotion (WWHD_VK_DEMAND_PROMOTE=1): the worker's driver compile of this very pipeline is running, so
    // waiting for it (at most WWHD_VK_DEMAND_PROMOTE_MS, default 1000) is shorter than compiling it again from the
    // start. Jobs still queued are compiled by the demand path (and dropped from the queue: consumed below).
    static const bool promote = [] { const char* e = std::getenv("WWHD_VK_DEMAND_PROMOTE"); return e && !std::strcmp(e, "1"); }();
    if (promote && w.inDriver.contains(id)) {
      static const int64_t limitMs = [] { const char* e = std::getenv("WWHD_VK_DEMAND_PROMOTE_MS"); return e && *e ? std::max(0, std::atoi(e)) : 1000; }();
      const auto started = std::chrono::steady_clock::now();
      ++w.promotions;
      w.driverDone.wait_for(lock, std::chrono::milliseconds(limitMs), [&] { return w.stopped || !w.inDriver.contains(id); });
      w.promotedWaitNs += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
      w.promotedHits += w.ready.contains(id);
    }
    // This also prevents an in-flight startup job from publishing a duplicate
    // after the render thread has already compiled this key.
    if (w.consumed.size() < kPipelineRecipeMaxCount) w.consumed.insert(id);
    auto found = w.ready.find(id);
    if (found == w.ready.end()) return false;
    entry = std::move(found->second); w.ready.erase(found);
  }
  bool matches = false;
  try {
    matches = portable_recipe_matches(*entry.recipe, spirv, descriptors, dynamicUniforms) &&
        entry.recipe->state &&
        encode_pipeline_fixed_state(*entry.recipe->state) == encode_pipeline_fixed_state(currentState);
  } catch (...) { /* Allocation/schema failures fall back to current-state creation. */ }
  if (!matches) {
    destroy(w.device, entry.handles);
    return false;
  }
  record_headstart_pipeline(w.fingerprint, entry.recipe);
  out = entry.handles;
  return true;
}
void pipeline_promotion_stats(uint64_t& promotions, uint64_t& hits, double& waitMs) {
  auto& w = worker();
  std::lock_guard lock(w.mutex);
  promotions = w.promotions; hits = w.promotedHits; waitMs = w.promotedWaitNs / 1e6;
}
bool pipeline_recipe_known(const std::vector<uint8_t>& key) {
  auto& w = worker();
  if (!w.device) return false;
  std::lock_guard lock(w.mutex);
  return w.recipes.contains(identity(key));
}
void remember_pipeline_recipe(std::shared_ptr<PipelineRecipe> recipe) {
  auto& w = worker();
  if (!w.device) return;
  record_headstart_pipeline(w.fingerprint, recipe);
  std::lock_guard lock(w.mutex);
  if (insert_recipe(w, recipe)) changed(w);
}
int64_t pipeline_recipes_durable_ns() {
  auto& w = worker();
  if (!w.device) return INT64_MAX;
  std::lock_guard lock(w.mutex);
  if (!w.revision.dirty()) return steady_ns();
  return g_durableNs.load(std::memory_order_acquire);
}
void note_pipeline_group(const std::vector<uint8_t>& key, uint32_t group) {
  auto& w = worker();
  if (!w.device || !group) return;
  std::lock_guard lock(w.mutex);
  auto id = identity(key);
  auto found = w.recipes.find(id);
  if (found == w.recipes.end()) return;
  const auto& old = *found->second;
  // A pipeline first made outside the sweep's batches (the title's own scene: sea, sky, 2D) stays ungrouped, so it
  // is compiled at start, not only once one of the archives drawn next to it loads.
  if (!old.state || old.groups.empty() || old.groups.size() >= kPipelineRecipeMaxGroups ||
      std::find(old.groups.begin(), old.groups.end(), group) != old.groups.end()) return;
  // Published recipes are immutable (the saver may be encoding this one): replace it with a copy.
  auto copy = std::make_shared<PipelineRecipe>();
  copy->key = old.key; copy->spirv = old.spirv; copy->descriptors = old.descriptors;
  copy->state = std::make_unique<GraphicsPipelineState>(*old.state);
  copy->dynamicUniforms = old.dynamicUniforms; copy->sharedSpirv = old.sharedSpirv; copy->groups = old.groups;
  copy->add_group(group);
  w.recipeBytes += 4;
  index_groups(w, id, *copy);
  found->second = std::move(copy);
  changed(w);
}
void checkpoint_pipeline_recipes() {
  auto& w=worker();
  if(!w.device) return;
  try { persist_snapshot(w); }
  catch(const std::exception& error) {
    std::fprintf(stderr,"[vulkan warmup] checkpoint snapshot failed: %s\n",error.what());
  }
}
void save_pipeline_recipes() {
  auto& w = worker();
  if (!w.device) return;
  {
    std::lock_guard lock(w.mutex); w.stopped = true;
  }
  w.workWake.notify_all();
  if (w.thread.joinable()) w.thread.join();
  {
    std::lock_guard lock(w.mutex); w.saveStopped = true;
  }
  w.saveWake.notify_all();
  if (w.saver.joinable()) w.saver.join();
  try { persist_snapshot(w); }
  catch (const std::exception& error) {
    std::fprintf(stderr, "[vulkan warmup] final snapshot failed: %s\n", error.what());
  }
}
bool merge_pipeline_worker_cache(VkPipelineCache target, bool wait) {
  auto& w = worker();
  std::unique_lock lock(w.cacheMutex, std::defer_lock);
  if (wait) lock.lock();
  else if (!lock.try_lock()) return false;
  if (!w.device || !target || !w.cache || !w.cacheChanged.load()) return false;
  const auto result = vkMergePipelineCaches(w.device, target, 1, &w.cache);
  if (result != VK_SUCCESS) {
    std::fprintf(stderr, "[vulkan warmup] cache merge failed (%d)\n", int(result));
    return false;
  }
  w.cacheChanged = false;
  std::fprintf(stderr, "[vulkan warmup] worker pipeline cache merged for persistence\n");
  return true;
}
static void leave_prefetch(Worker& w) {
  { std::lock_guard lock(w.mutex); w.drainExit = true; }
  w.workWake.notify_all();
}
PipelineWarmupResult warm_resolved_pipeline_recipes(std::vector<std::shared_ptr<PipelineRecipe>> recipes,
    const std::function<void(size_t)>& progress) {
  auto& w = worker();
  leave_prefetch(w);
  if (w.thread.joinable()) w.thread.join();
  if (!w.device || recipes.size() > kPipelineRecipeMaxCount) return {0, recipes.size()};
  std::atomic<size_t> next{0}, ready{0}, failed{0}, finished{0};
  std::mutex progressMutex;
  auto compileAll = [&] {
    host::set_thread_name("Vulkan state warmup");
    for (size_t i; (i = next.fetch_add(1)) < recipes.size();) {
      const auto& recipe = recipes[i];
      const auto key = identity(recipe->key);
      bool skip = false;
      {
        std::lock_guard lock(w.mutex);
        if (w.stopped) { ++failed; skip = true; }
        else if (w.ready.contains(key)) { ++ready; skip = true; }
      }
      if (!skip) {
        WarmPipeline handles;
        try {
          VkResult compiled;
          {
            std::shared_lock cacheLock(w.cacheMutex);
            compiled = compile(w.device, w.cache, *recipe, handles);
            if (compiled == VK_SUCCESS) w.cacheChanged = true;
          }
          if (compiled != VK_SUCCESS) ++failed;
          else {
            std::lock_guard lock(w.mutex);
            if (!insert_recipe(w, recipe)) { destroy(w.device, handles); ++failed; }
            else {
              try { retain_ready(w,key,handles,recipe); handles = {}; }
              catch (...) { destroy(w.device, handles); throw; }
              changed(w);
              ++ready;
            }
          }
        } catch (const std::exception& error) {
          destroy(w.device, handles);
          ++failed;
          std::fprintf(stderr, "[vulkan state warmup] skipped: %s\n", error.what());
        }
      }
      const size_t done = ++finished;
      if (progress) { std::lock_guard lock(progressMutex); progress(done); }
    }
  };
  // Offline warming only (nothing else runs): a few compiling threads, leaving cores free.
  const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
  const size_t threads = std::min<size_t>({4, cores > 2 ? cores - 2 : 1, std::max<size_t>(1, recipes.size())});
  std::vector<std::thread> pool;
  for (size_t t = 1; t < threads; ++t) pool.emplace_back(compileAll);
  w.thread = std::thread(compileAll);
  w.thread.join();
  for (auto& thread : pool) thread.join();
  PipelineWarmupResult batch{ready.load(), failed.load()};
  w.result.ready += batch.ready; w.result.failed += batch.failed;
  return batch;
}
PipelineWarmupResult warm_all_recipes() {
  auto& w = worker();
  leave_prefetch(w);
  if (w.thread.joinable()) w.thread.join();
  std::vector<std::shared_ptr<PipelineRecipe>> rest;
  {
    std::lock_guard lock(w.mutex);
    std::vector<std::pair<uint64_t, std::shared_ptr<PipelineRecipe>>> ordered;
    for (auto& [key, recipe] : w.recipes)
      if (!w.ready.contains(key)) {
        auto at = w.order.find(key);
        ordered.emplace_back(at == w.order.end() ? UINT64_MAX : at->second, recipe);
      }
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& entry : ordered) rest.push_back(std::move(entry.second));
  }
  return warm_resolved_pipeline_recipes(std::move(rest));
}
PipelineWarmupResult finish_pipeline_warmup() {
  auto& w = worker();
  leave_prefetch(w);
  if (w.thread.joinable()) w.thread.join();
  return w.result;
}
}
