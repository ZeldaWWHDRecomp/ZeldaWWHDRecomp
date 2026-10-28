#pragma once
#include "pipeline_recipe.h"
#include <functional>
#include <string>

namespace gfxvk {
struct WarmPipeline {
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkDescriptorSetLayout sets[2]{};
  bool dynamicUniforms = false;
};
// The worker never accesses guest memory, R, a command buffer, or a render-thread
// pipeline cache. Ready objects transfer to the render thread exactly once.
// The device's memory budget in bytes (0: unknown), before start_pipeline_worker: it sizes the pool of unused warm
// pipelines (the pool exists for memory only)
void set_memory_budget(uint64_t bytes);
void start_pipeline_worker(VkDevice device, PipelineRecipeFingerprint fingerprint,
    std::string path, std::vector<uint8_t> cacheSeed, std::string headstartPath);
bool take_warm_pipeline(const std::vector<uint8_t>& key,
    const std::array<std::vector<uint32_t>, 2>& spirv,
    const std::array<std::vector<VkDescriptorSetLayoutBinding>, 2>& descriptors,
    bool dynamicUniforms, const GraphicsPipelineState& currentState, WarmPipeline& out);
void remember_pipeline_recipe(std::shared_ptr<PipelineRecipe> recipe);
// Whether the recipe database (local recipes, the head start, recipes recorded since) has this portable key: the
// coverage counter of pipelines the game asks for (draw.cpp, "[vulkan pipelines]").
bool pipeline_recipe_known(const std::vector<uint8_t>& key);
// Demands that waited for the worker's running compile of their pipeline (WWHD_VK_DEMAND_PROMOTE), how many got it,
// and the time they waited in all.
// Sizing (WWHD_VK_MEMORY_LOG_FRAMES): the recipe database (packed SPIR-V and state, bytes) and the pool of compiled
// pipelines no draw has taken yet, its limit and how many it dropped.
struct PipelineWorkerMemory { size_t recipes = 0, recipeBytes = 0, unusedWarm = 0, unusedWarmLimit = 0, evicted = 0; };
void pipeline_worker_memory(PipelineWorkerMemory& out);
void pipeline_promotion_stats(uint64_t& promotions, uint64_t& hits, double& waitMs);
// The setup's model sweep drew a model of archive group `group` with this (portable) key: file the recipe under it too.
void note_pipeline_group(const std::vector<uint8_t>& key, uint32_t group);
// Steady-clock time (ns) up to which every recipe recorded is on disk: the start of the last snapshot written, now
// when nothing is unsaved, INT64_MAX without a worker (the setup's model sweep marks work done only once saved).
int64_t pipeline_recipes_durable_ns();
// Explicit checkpoint/shutdown only: joins the worker and writes local recipes.
void save_pipeline_recipes();
// Save current dirty revision without stopping compiler or periodic persistence.
void checkpoint_pipeline_recipes();
struct PipelineWarmupResult { size_t ready = 0, failed = 0; };
// Offline only, caller resolves all shader identities/layouts on render thread.
// Joins startup, then a worker owns this immutable local-SPIR-V batch.
// Compiles on a few threads (the pipeline cache is internally synchronized); progress(finished)
// runs on a compiling thread, one call at a time.
PipelineWarmupResult warm_resolved_pipeline_recipes(std::vector<std::shared_ptr<PipelineRecipe>> recipes,
    const std::function<void(size_t)>& progress = {});
// Offline warming only: finish the whole startup queue instead of cancelling it.
PipelineWarmupResult finish_pipeline_warmup();
// Offline warming only: compile every recipe of the database not compiled yet (WWHD_VK_WARM_PRUNE: the rebuilt
// driver cache then holds all of them), in first-seen order.
PipelineWarmupResult warm_all_recipes();
// Caller serializes target access; skip active compilation unless wait is true.
bool merge_pipeline_worker_cache(VkPipelineCache target, bool wait = false);
}
