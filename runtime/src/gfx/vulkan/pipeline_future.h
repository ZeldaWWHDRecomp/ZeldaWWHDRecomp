// Pipeline futures (MT recording stage 1, WWHD_VK_RECORD_MT bit 32 / WWHD_VK_RECORD_PIPELINE_FUTURES=1; record_mt.h).
// Decomp build, fast mode (cmake WWHD_NATIVE_DRAW).
//
// Under the record worker a draw whose pipeline is still compiling made the render thread wait for the compile
// (draw.cpp: begin_exact_preparation() is false with the worker), the ~150 waits per run and the p99 cost of R5. With
// futures the draw is recorded as without the worker's exact preparation: the bind and the draws go into the POD stream
// as BindFuture commands that carry the compile (its ticket moved into a shared Future); the emitter waits for it,
// binds the pipeline and records the draws, or skips them when the compile failed - the rule of the single-thread
// path's deferred lambdas (draw.cpp deferredPipelineDraw: bind and draws only `if (pipeline->pipeline)`).
// The render thread's later resolve_pending_pipeline takes the same result from the Future and does the bookkeeping
// as before. Whoever waits first takes the ticket out of the Future and waits without its mutex; the other side waits
// for the stored result. The render thread never waits for the emitter while it holds the ticket with a present
// callback (that callback drains the emitter): then it publishes and lets the emitter wait.
// Only while the worker's main stream records (not the stereo eye stream, not the presentation scope).
#pragma once
#ifdef WWHD_NATIVE_DRAW
#include "loader.h"
#include "pipeline_demand.h"
#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace gfxvk::pipeline_future {
struct Future {
  std::mutex mutex;
  std::optional<PipelineDemandTicket> ticket;
  std::condition_variable changed;
  std::optional<PipelineBuildResult> result;  // set by the first wait, either thread
  std::exception_ptr error;
  ~Future();
};
// render thread: futures apply to this draw (bit 32, the worker's main stream records)
bool active();
// the ticket moves into the future (TICKET is left empty)
std::shared_ptr<Future> make(std::optional<PipelineDemandTicket>& ticket);
// PendingPipeline's destructor: the compile done (or dropped) before its modules go
void discard(Future& f);
// render thread: the result when ready within BUDGET_NS (0: poll; a ticket the emitter waits on counts as not ready),
// or wait for it. Every call returns the same result once it exists.
std::optional<PipelineBuildResult> poll(Future& f, uint64_t budgetNs);
PipelineBuildResult wait(Future& f, const std::function<void()>& presentWhilePending);
// the render thread waits for the ticket itself: prioritize first (resolve_pending_pipeline)
void prioritize(Future& f);
// render thread: queue the bind / a draw of the future's pipeline (a rejected command drains, waits and records here)
void bind(VkCommandBuffer cmd, const std::shared_ptr<Future>& f);
void draw(VkCommandBuffer cmd, const std::shared_ptr<Future>& f, uint32_t vertices, uint32_t instances,
          uint32_t firstVertex);
void draw_indexed(VkCommandBuffer cmd, const std::shared_ptr<Future>& f, uint32_t indices, uint32_t instances,
                  int32_t vertexOffset);
// opcode 31 BindFuture replay (payload after the sub-opcode)
void replay(const std::byte* bytes, size_t size, bool execute);
void report(double frames, char* out, size_t outSize);
}  // namespace gfxvk::pipeline_future
#endif
