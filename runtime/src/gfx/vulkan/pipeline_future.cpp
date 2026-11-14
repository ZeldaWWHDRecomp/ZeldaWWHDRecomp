// Pipeline futures: pipeline_future.h
#ifdef WWHD_VK_SMOOTH
#include "pipeline_future.h"
#include "record_mt.h"
#include "record_worker.h"
#include "deferred_commands.h"
#include "pod_commands.h"
#include "runtime.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace gfxvk::pipeline_future {
namespace {
enum Kind : uint32_t { kBind = 0, kDraw = 1, kDrawIndexed = 2 };
struct Payload {
  uint32_t kind, a;  // a: vertices / indices
  VkCommandBuffer cmd;
  std::shared_ptr<Future>* ref;  // owned by the stream; the execute pass deletes it
  uint32_t instances, first;     // first: firstVertex (kDraw) or the vertex offset (kDrawIndexed)
};
struct Stats { std::atomic<uint64_t> binds{0}, draws{0}, rejected{0}, emitterWaits{0}, emitterWaitNs{0}, producerTakes{0},
                                     producerEmitterWaits{0}; } stats;
uint64_t now_ns() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
// the emitter's view: the pipeline to bind, or none (the compile failed: skip, as the deferred lambdas do)
VkPipeline emitter_pipeline(Future& f) {
  std::unique_lock lock(f.mutex);
  for (;;) {
    if (f.result) return f.result->result == VK_SUCCESS ? f.result->pipeline : VK_NULL_HANDLE;
    if (f.error) return VK_NULL_HANDLE;
    if (f.ticket) {
      auto ticket = std::move(*f.ticket);
      f.ticket.reset();
      lock.unlock();
      const uint64_t t0 = now_ns();
      std::optional<PipelineBuildResult> r;
      std::exception_ptr e;
      try { r = ticket.wait({}); } catch (...) { e = std::current_exception(); }
      ++stats.emitterWaits;
      stats.emitterWaitNs += now_ns() - t0;
      lock.lock();
      if (r) f.result = r; else f.error = e;
      f.changed.notify_all();
      continue;
    }
    f.changed.wait(lock);  // the render thread has the ticket out for a bounded poll
  }
}
void encode(Payload p, const std::shared_ptr<Future>& f) {
  auto* stream = record_worker_commands();
  if (stream) {
    p.ref = new std::shared_ptr<Future>(f);
    if (record_mt::append(*stream, record_mt::Ext::BindFuture, {reinterpret_cast<const std::byte*>(&p), sizeof p})) {
      ++(p.kind == kBind ? stats.binds : stats.draws);
      return;
    }
    delete p.ref;
    ++stats.rejected;
    drain_record_commands(fallback_stats::Boundary::EncoderFallback, 31, stream->failure_reason());
  }
  // here, after the drain: the emitter is idle and every earlier command of this draw was emitted
  const VkPipeline pipeline = emitter_pipeline(*f);
  if (!pipeline) return;
  if (p.kind == kBind) gfxvk::vkCmdBindPipeline(p.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  else if (p.kind == kDraw) gfxvk::vkCmdDraw(p.cmd, p.a, p.instances, p.first, 0);
  else gfxvk::vkCmdDrawIndexed(p.cmd, p.a, p.instances, 0, int32_t(p.first), 0);
}
}  // namespace
Future::~Future() = default;
bool active() {
  return record_mt::on(record_mt::kFutures) && record_worker_commands() && !eye_deferred_commands;
}
std::shared_ptr<Future> make(std::optional<PipelineDemandTicket>& ticket) {
  auto f = std::make_shared<Future>();
  f->ticket = std::move(ticket);
  ticket.reset();
  return f;
}
void discard(Future& f) {
  std::unique_lock lock(f.mutex);
  if (f.ticket) {
    f.ticket->discard_and_wait();
    f.ticket.reset();
    return;
  }
  // the emitter (or a poll) has it out: wait until it is back or done
  f.changed.wait(lock, [&] { return f.result || f.error || f.ticket; });
  if (f.ticket) { f.ticket->discard_and_wait(); f.ticket.reset(); }
  // (an unconsumed result's pipeline stays alive: commands of a submission may use it)
}
std::optional<PipelineBuildResult> poll(Future& f, uint64_t budgetNs) {
  std::unique_lock lock(f.mutex, std::try_to_lock);
  if (!lock.owns_lock()) return std::nullopt;
  if (f.result) return f.result;
  if (f.error) std::rethrow_exception(f.error);
  if (!f.ticket) return std::nullopt;  // the emitter waits for it
  auto ticket = std::move(*f.ticket);
  f.ticket.reset();
  lock.unlock();
  std::optional<PipelineBuildResult> r;
  try { r = ticket.wait_for(budgetNs); } catch (...) {
    lock.lock();
    f.error = std::current_exception();
    f.changed.notify_all();
    throw;
  }
  lock.lock();
  if (r) f.result = r; else f.ticket = std::move(ticket);
  f.changed.notify_all();
  return r;
}
PipelineBuildResult wait(Future& f, const std::function<void()>& presentWhilePending) {
  std::unique_lock lock(f.mutex);
  if (f.result) return *f.result;
  if (f.error) std::rethrow_exception(f.error);
  if (f.ticket && !presentWhilePending) {
    auto ticket = std::move(*f.ticket);
    f.ticket.reset();
    lock.unlock();
    ++stats.producerTakes;
    std::optional<PipelineBuildResult> r;
    try { r = ticket.wait({}); } catch (...) {
      lock.lock();
      f.error = std::current_exception();
      f.changed.notify_all();
      throw;
    }
    lock.lock();
    f.result = r;
    f.changed.notify_all();
    return *r;
  }
  // the emitter takes the ticket at the future's first command: make sure it gets there, then wait for its result
  lock.unlock();
  publish_record_commands();
  ++stats.producerEmitterWaits;
  lock.lock();
  f.changed.wait(lock, [&] { return f.result || f.error; });
  if (f.error) std::rethrow_exception(f.error);
  return *f.result;
}
void prioritize(Future& f) {
  std::lock_guard lock(f.mutex);
  if (f.ticket) f.ticket->prioritize();
}
void bind(VkCommandBuffer cmd, const std::shared_ptr<Future>& f) { encode({kBind, 0, cmd, nullptr, 0, 0}, f); }
void draw(VkCommandBuffer cmd, const std::shared_ptr<Future>& f, uint32_t vertices, uint32_t instances,
          uint32_t firstVertex) {
  encode({kDraw, vertices, cmd, nullptr, instances, firstVertex}, f);
}
void draw_indexed(VkCommandBuffer cmd, const std::shared_ptr<Future>& f, uint32_t indices, uint32_t instances,
                  int32_t vertexOffset) {
  encode({kDrawIndexed, indices, cmd, nullptr, instances, uint32_t(vertexOffset)}, f);
}
void replay(const std::byte* bytes, size_t size, bool execute) {
  Payload p;
  if (size != sizeof p) throw std::runtime_error("bad POD BindFuture");
  std::memcpy(&p, bytes, sizeof p);
  if (!p.ref || p.kind > kDrawIndexed) throw std::runtime_error("bad POD BindFuture");
  if (!execute) return;
  std::unique_ptr<std::shared_ptr<Future>> ref(p.ref);
  const VkPipeline pipeline = emitter_pipeline(**ref);
  if (!pipeline) return;
  if (p.kind == kBind) gfxvk::vkCmdBindPipeline(p.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  else if (p.kind == kDraw) gfxvk::vkCmdDraw(p.cmd, p.a, p.instances, p.first, 0);
  else gfxvk::vkCmdDrawIndexed(p.cmd, p.a, p.instances, 0, int32_t(p.first), 0);
}
void report(double frames, char* out, size_t outSize) {
  if (!(record_mt::bits() & record_mt::kFutures)) return;
  auto take = [](std::atomic<uint64_t>& v) { return double(v.exchange(0, std::memory_order_relaxed)); };
  const double b = take(stats.binds), d = take(stats.draws), r = take(stats.rejected), w = take(stats.emitterWaits),
               ns = take(stats.emitterWaitNs), pt = take(stats.producerTakes), pe = take(stats.producerEmitterWaits);
  std::snprintf(out, outSize,
                "; pipeline futures: binds %.2f/frame, draws %.2f/frame, rejected %.2f; emitter waits %.2f/frame "
                "(%.3f ms/frame); render thread took the compile %.2f/frame, waited for the emitter %.2f/frame",
                b / frames, d / frames, r / frames, w / frames, ns / 1e6 / frames, pt / frames, pe / frames);
}
}  // namespace gfxvk::pipeline_future
#endif
