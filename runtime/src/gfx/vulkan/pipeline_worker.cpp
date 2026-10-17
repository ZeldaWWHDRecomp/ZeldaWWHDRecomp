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
#include "worker_metrics.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
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
struct Ready { WarmPipeline handles; std::shared_ptr<PipelineRecipe> recipe; };
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
  std::unordered_set<std::string> consumed;
  size_t recipeBytes = 0;
  RecipeShaderPool shaderPool;
  ~Worker() {
    { std::lock_guard lock(mutex); stopped = true; saveStopped = true; }
    saveWake.notify_all();
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
constexpr size_t maxUnusedWarmPipelines = 256;
#else
constexpr size_t maxUnusedWarmPipelines = 1024;
#endif
void retain_ready(Worker& w, const std::string& key, WarmPipeline handles,
                  const std::shared_ptr<PipelineRecipe>& recipe) {
  if(w.ready.size() >= maxUnusedWarmPipelines) {
    auto victim=w.ready.begin(); destroy(w.device,victim->second.handles); w.ready.erase(victim);
  }
  w.ready.emplace(key,Ready{handles,recipe});
}

size_t footprint(const PipelineRecipe& p) {
  size_t size = 8192 + p.key.size();
  for (int i = 0; i < 2; ++i)
    size += p.words(i).size() * 4 + p.descriptors[i].size() * sizeof(VkDescriptorSetLayoutBinding);
  return size;
}
bool insert_recipe(Worker& w, const std::shared_ptr<PipelineRecipe>& recipe) {
  auto key = identity(recipe->key);
  auto found = w.recipes.find(key);
  const size_t old = found == w.recipes.end() ? 0 : footprint(*found->second);
  const size_t size = footprint(*recipe);
  if ((found == w.recipes.end() && w.recipes.size() >= kPipelineRecipeMaxCount) ||
      size > kPipelineRecipeMaxBytes - (w.recipeBytes - old)) return false;
  w.shaderPool.freeze(*recipe);
  w.recipeBytes += size - old;
  w.recipes.insert_or_assign(std::move(key), recipe);
  return true;
}
void changed(Worker& w) {
  w.revision.changed(); w.saveWake.notify_one();
}
void persist_snapshot(Worker& w) {
  std::lock_guard writer(w.persistenceMutex);
  std::vector<std::shared_ptr<PipelineRecipe>> snapshot;
  uint64_t revision;
  {
    std::lock_guard lock(w.mutex);
    if (!w.revision.dirty()) return;
    revision = w.revision.current;
    snapshot.reserve(w.recipes.size());
    for (auto& [key, recipe] : w.recipes) snapshot.push_back(recipe);
  }
  try {
    auto bytes = encode_pipeline_recipes(w.fingerprint, snapshot);
    atomic_recipe_write(w.path, bytes, host::replace_file);
    {
      std::lock_guard lock(w.mutex);
      w.revision.complete(revision);
    }
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
    auto due = w.lastSave + std::chrono::seconds(120);
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
VkResult compile(VkDevice device, VkPipelineCache cache, PipelineRecipe& recipe, WarmPipeline& out) {
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
    VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    module.codeSize = recipe.words(stage).size() * 4; module.pCode = recipe.words(stage).data();
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
    if (started) stutter::trace().add("startup_pipeline", 0, std::hash<std::string>{}(identity(recipe.key)),
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
    if (loaded.empty()) { w.result = {0, failed}; return; }
    {
      std::lock_guard lock(w.mutex);
      std::erase_if(loaded, [&](const auto& recipe) {
        return w.consumed.contains(identity(recipe->key)) || !insert_recipe(w, recipe);
      });
    }
    VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    ci.initialDataSize = seed.size(); ci.pInitialData = seed.data();
    if (vkCreatePipelineCache(w.device, &ci, nullptr, &cache) != VK_SUCCESS) {
      ci.initialDataSize = 0; ci.pInitialData = nullptr;
      if (vkCreatePipelineCache(w.device, &ci, nullptr, &cache) != VK_SUCCESS)
        throw std::runtime_error("worker pipeline cache creation failed");
    }
    { std::unique_lock lock(w.cacheMutex); w.cache = cache; }
    std::vector<uint8_t>().swap(seed); // Vulkan copied initial cache data during creation.
    std::fprintf(stderr, "[vulkan warmup] %zu exact pipeline recipes queued\n", loaded.size());
    for (auto& recipe : loaded) {
      // Demand has its own single compiler; stop issuing speculative work
      // while it is busy. A driver call already in flight cannot be cancelled.
      while (pipeline_demand_active() || vk::shader_compiler_active()) {
        { std::lock_guard lock(w.mutex); if (w.stopped) break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      auto key = identity(recipe->key);
      {
        std::lock_guard lock(w.mutex);
        if (w.stopped) break;
        if (w.consumed.contains(key)) { ++cancelled; continue; }
      }
      WarmPipeline result;
      {
        std::shared_lock cacheLock(w.cacheMutex);
        if (compile(w.device, cache, *recipe, result) != VK_SUCCESS) { ++failed; continue; }
        w.cacheChanged = true;
      }
      bool publish;
      {
        std::lock_guard lock(w.mutex);
        publish = !w.stopped && !w.consumed.contains(key);
        if (publish) {
          try { retain_ready(w,key,result,recipe); }
          catch (...) { destroy(w.device,result); throw; }
        }
      }
      if (publish) ++completed;
      else { destroy(w.device, result); ++cancelled; }
    }
    std::fprintf(stderr, "[vulkan warmup] %zu ready, %zu failed, %zu cancelled\n", completed, failed, cancelled);
  } catch (const std::exception& error) {
    ++failed;
    std::fprintf(stderr, "[vulkan warmup] skipped: %s\n", error.what());
  }
  w.result = {completed, failed};
}

}
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
    std::lock_guard lock(w.mutex);
    auto id = identity(key);
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
void remember_pipeline_recipe(std::shared_ptr<PipelineRecipe> recipe) {
  auto& w = worker();
  if (!w.device) return;
  record_headstart_pipeline(w.fingerprint, recipe);
  std::lock_guard lock(w.mutex);
  if (insert_recipe(w, recipe)) changed(w);
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
PipelineWarmupResult warm_resolved_pipeline_recipes(std::vector<std::shared_ptr<PipelineRecipe>> recipes,
    const std::function<void(size_t)>& progress) {
  auto& w = worker();
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
PipelineWarmupResult finish_pipeline_warmup() {
  auto& w = worker();
  if (w.thread.joinable()) w.thread.join();
  return w.result;
}
}
