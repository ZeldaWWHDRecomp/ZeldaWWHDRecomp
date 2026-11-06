#pragma once
#include "pipeline_state.h"
#include <functional>
#include <memory>
#include <optional>

namespace gfxvk {
struct PipelineBuildResult {
  VkResult result = VK_ERROR_INITIALIZATION_FAILED;
  VkPipeline pipeline = VK_NULL_HANDLE;
  uint64_t compileNs = 0;
};
// A ticket owns CPU create-info and an unclaimed result. Immutable Vulkan
// modules/layout/render pass remain caller-owned until completion (or a compiler
// drain). Destroy tickets before device teardown. Dropping a pending ticket is
// safe: the worker destroys its eventual unclaimed pipeline.
class PipelineDemandTicket {
public:
  struct State;
  explicit PipelineDemandTicket(std::shared_ptr<State> state);
  PipelineDemandTicket(PipelineDemandTicket&&) noexcept = default;
  PipelineDemandTicket& operator=(PipelineDemandTicket&&) noexcept = default;
  PipelineDemandTicket(const PipelineDemandTicket&) = delete;
  PipelineDemandTicket& operator=(const PipelineDemandTicket&) = delete;
  // Empty means pending, including a zero-budget poll. A populated failure is
  // terminal. A result can be consumed once; later consumption throws.
  std::optional<PipelineBuildResult> wait_for(uint64_t budgetNs);
  // Move a still queued job to the front: the caller is about to wait for it (Shader compile Fast
  // queues the pipelines of draws that were left out ahead of the ones that must wait).
  void prioritize();
  PipelineBuildResult wait(const std::function<void()>& presentWhilePending);
  // Drain without transferring a handle or emitting trace events; useful before
  // releasing caller-owned immutable Vulkan prerequisites during teardown.
  void discard_and_wait();
private:
  std::optional<PipelineBuildResult> wait_for_impl(uint64_t budgetNs);
  std::shared_ptr<State> state_;
};
// Non-blocking admission; queue pressure returns empty and requires an Exact
// fallback rather than dropping a draw. Retain the same ticket across frames.
// Preparation is copied only upon admission and runs on the pipeline worker,
// before native compilation timing. Its owner retains any created modules.
std::optional<PipelineDemandTicket> begin_pipeline_async(
    const GraphicsPipelineState& state, uint64_t frame, uint64_t key, const char* kind,
    const std::function<void(GraphicsPipelineState&)>& prepare = {});
// A dedicated compiler owns its cache. The caller retains the immutable Vulkan
// modules/layout until this operation returns; all CPU create-info arrays are
// copied into the queued job. The callback services completed-frame presentation
// while the exact draw stays at the head of the command stream.
void start_pipeline_demand(VkDevice device, std::vector<uint8_t> seed);
bool async_pipeline_enabled();
PipelineBuildResult build_pipeline_async(const GraphicsPipelineState& state,
    uint64_t frame, uint64_t key, const char* kind,
    const std::function<void()>& presentWhilePending,
    const std::function<void(GraphicsPipelineState&)>& prepare = {});
bool pipeline_demand_active();
// Under the renderer execution lock, after queued rendering has drained.
bool finish_pipeline_demand(VkPipelineCache target);
// Caller serializes target access. Normal checkpoints skip a busy compiler;
// background/explicit checkpoints may wait off the UI thread, without stopping it.
// The warm-up worker compiles into the demand compiler's driver cache, so that the render thread's own build of a
// pipeline the worker compiled ahead is a cache hit (warm_handover.h). VK_INCOMPLETE: that cache does not exist yet.
VkResult compile_in_demand_cache(VkDevice device, const VkGraphicsPipelineCreateInfo& info, VkPipeline* out);
bool merge_pipeline_demand_cache(VkPipelineCache target, bool wait = false);
}
