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
void start_pipeline_worker(VkDevice device, PipelineRecipeFingerprint fingerprint,
    std::string path, std::vector<uint8_t> cacheSeed, std::string headstartPath);
bool take_warm_pipeline(const std::vector<uint8_t>& key,
    const std::array<std::vector<uint32_t>, 2>& spirv,
    const std::array<std::vector<VkDescriptorSetLayoutBinding>, 2>& descriptors,
    bool dynamicUniforms, const GraphicsPipelineState& currentState, WarmPipeline& out);
void remember_pipeline_recipe(std::shared_ptr<PipelineRecipe> recipe);
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
// Caller serializes target access; skip active compilation unless wait is true.
bool merge_pipeline_worker_cache(VkPipelineCache target, bool wait = false);
}
