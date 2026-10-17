#include "pipeline_demand.h"
#include "stutter_trace.h"
#include "worker_metrics.h"
#include "platform/host.h"
#include <algorithm>
#include <atomic>
#include <exception>
#include <stdexcept>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
#include <deque>
#include <string>
#include <limits>
#include <mutex>
#include <thread>

namespace gfxvk {
namespace {
// Diagnostics must never alter ownership or admission. Event labels allocate;
// an exhausted trace buffer/allocator must not leak a driver-created handle or
// strand an enqueued job whose caller never received its ticket.
void trace_event(const char* kind, uint64_t frame, uint64_t key,
                 uint64_t started, uint64_t ended, int result,
                 uint64_t cpu = ~uint64_t{0}) noexcept {
  try { stutter::trace().add(kind, frame, key, started, ended, result, cpu); }
  catch (...) {}
}
}
struct PipelineDemandTicket::State {
  GraphicsPipelineState state;
  std::function<void(GraphicsPipelineState&)> prepare;
  uint64_t frame, key, requested;
  std::string kind;
  VkDevice device;
  std::mutex mutex;
  std::condition_variable wake;
  bool ready = false, consumed = false;
  PipelineBuildResult result;
  std::exception_ptr error;
  State(const GraphicsPipelineState& value, uint64_t f, uint64_t k,
        const char* label, VkDevice d, const std::function<void(GraphicsPipelineState&)>& prerequisites)
      : state(value), prepare(prerequisites), frame(f), key(k),
        requested(stutter::trace().enabled() ? stutter::now() : 0),
        kind(label), device(d) {}
  ~State() {
    if (result.pipeline) vkDestroyPipeline(device, result.pipeline, nullptr);
  }
};
namespace {
using Job = PipelineDemandTicket::State;
struct Compiler {
  VkDevice device = VK_NULL_HANDLE;
  VkPipelineCache cache = VK_NULL_HANDLE;
  std::vector<uint8_t> seed;
  std::mutex mutex, cacheMutex;
  std::condition_variable wake;
  std::deque<std::shared_ptr<Job>> queue;
  std::thread thread;
  std::atomic<unsigned> active{0};
  bool stopped = false, changed = false;
  ~Compiler() {
    { std::lock_guard lock(mutex); stopped = true; }
    wake.notify_all();
    if (thread.joinable()) thread.join();
    if (cache) vkDestroyPipelineCache(device, cache, nullptr);
  }
};
Compiler& compiler() { static Compiler value; return value; }
void run(Compiler& c) {
  host::set_thread_name("Vulkan demand");
  // Live checkpoints take cacheMutex; queue admission never holds it.
  VkResult cacheResult = VK_SUCCESS;
  {
    std::lock_guard cacheLock(c.cacheMutex);
    if (!c.cache) {
      VkPipelineCacheCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
      info.initialDataSize = c.seed.size(); info.pInitialData = c.seed.data();
      cacheResult = vkCreatePipelineCache(c.device, &info, nullptr, &c.cache);
      if (cacheResult != VK_SUCCESS && !c.seed.empty()) {
        info.initialDataSize = 0; info.pInitialData = nullptr;
        cacheResult = vkCreatePipelineCache(c.device, &info, nullptr, &c.cache);
      }
      std::vector<uint8_t>().swap(c.seed);
    }
  }
  for (;;) {
    std::shared_ptr<Job> job;
    {
      std::unique_lock lock(c.mutex);
      c.wake.wait(lock, [&] { return c.stopped || !c.queue.empty(); });
      if (c.queue.empty()) return;
      job = std::move(c.queue.front()); c.queue.pop_front();
    }
    PipelineBuildResult result;
    try {
      if(job->prepare) {
        const bool tracing=stutter::trace().enabled();
        const auto started=tracing?stutter::now():0;
        const auto cpu=tracing?stutter::thread_cpu_now():0;
        try { job->prepare(job->state); }
        catch(...) {
          if(tracing)trace_event("pipeline_prerequisites",job->frame,job->key,started,stutter::now(),int(VK_ERROR_UNKNOWN),stutter::cpu_elapsed(cpu));
          throw;
        }
        if(tracing)trace_event("pipeline_prerequisites",job->frame,job->key,started,stutter::now(),int(VK_SUCCESS),stutter::cpu_elapsed(cpu));
      }
      const auto cpuStart = stutter::trace().enabled() ? stutter::thread_cpu_now() : 0;
      const auto started = stutter::now();
      {
        std::lock_guard cacheLock(c.cacheMutex);
        result.result = cacheResult == VK_SUCCESS
            ? vkCreateGraphicsPipelines(c.device, c.cache, 1, job->state.get(), nullptr, &result.pipeline)
            : cacheResult;
        c.changed |= result.result == VK_SUCCESS;
      }
      const auto ended = stutter::now();
      result.compileNs = ended - started;
      if (stutter::trace().enabled()) trace_event(job->kind.c_str(), job->frame, job->key, started, ended, int(result.result), stutter::cpu_elapsed(cpuStart));
      if (result.result != VK_SUCCESS && result.pipeline) {
        vkDestroyPipeline(c.device, result.pipeline, nullptr);
        result.pipeline = VK_NULL_HANDLE;
      }
      { std::lock_guard lock(job->mutex); job->result = result; job->ready = true; }
      result.pipeline = VK_NULL_HANDLE; // Ownership transferred to the job.
    } catch (...) {
      if (result.pipeline) vkDestroyPipeline(c.device, result.pipeline, nullptr);
      { std::lock_guard lock(job->mutex); job->error = std::current_exception(); job->ready = true; }
    }
    c.active.fetch_sub(1, std::memory_order_release);
    job->wake.notify_all();
    c.wake.notify_all();
  }
}
}
void start_pipeline_demand(VkDevice device, std::vector<uint8_t> seed) {
  (void)stutter::trace(); // Trace must outlive compiler static destruction.
  auto& c = compiler();
  if (c.device && c.device != device) throw std::runtime_error("pipeline compiler device changed without teardown");
  if (c.device) return;
  c.device = device; c.seed = std::move(seed);
  try { c.thread = std::thread(run, std::ref(c)); }
  catch (...) { c.device = VK_NULL_HANDLE; throw; }
}
PipelineDemandTicket::PipelineDemandTicket(std::shared_ptr<State> state)
    : state_(std::move(state)) {}
std::optional<PipelineBuildResult> PipelineDemandTicket::wait_for_impl(uint64_t budgetNs) {
  if (!state_) throw std::logic_error("empty pipeline demand ticket");
  auto& job = *state_;
  std::unique_lock lock(job.mutex);
  if (job.consumed) throw std::logic_error("pipeline demand result already consumed");
  if (!job.ready && budgetNs) {
    const auto bounded = std::min<uint64_t>(budgetNs, uint64_t(std::numeric_limits<int64_t>::max()));
    job.wake.wait_for(lock, std::chrono::nanoseconds(bounded), [&] { return job.ready; });
  }
  if (!job.ready) return std::nullopt;
  if (job.consumed) throw std::logic_error("pipeline demand result already consumed");
  job.consumed = true;
  if (job.error) std::rethrow_exception(job.error);
  auto result = job.result;
  job.result.pipeline = VK_NULL_HANDLE;
  if (job.requested) trace_event("pipeline_ticket_lifetime", job.frame, job.key,
      job.requested, stutter::now(), int(result.result));
  return result;
}
std::optional<PipelineBuildResult> PipelineDemandTicket::wait_for(uint64_t budgetNs) {
  if (!budgetNs || !state_ || !stutter::trace().enabled()) return wait_for_impl(budgetNs);
  const auto started = stutter::now();
  try {
    auto result = wait_for_impl(budgetNs);
    trace_event("pipeline_wait", state_->frame, state_->key, started,
        stutter::now(), int(result ? result->result : VK_NOT_READY));
    return result;
  } catch (...) {
    trace_event("pipeline_wait", state_->frame, state_->key, started,
        stutter::now(), int(VK_ERROR_UNKNOWN));
    throw;
  }
}
PipelineBuildResult PipelineDemandTicket::wait(const std::function<void()>& presentWhilePending) {
  if (!state_) throw std::logic_error("empty pipeline demand ticket");
  const auto started = stutter::trace().enabled() ? stutter::now() : 0;
  auto recordWait = [&](VkResult result) {
    if (!started) return;
    const auto ended = stutter::now();
    trace_event("pipeline_wait", state_->frame, state_->key, started, ended, int(result));
    // Backward-compatible Exact metric, now measuring the actual caller pause,
    // never the lifetime of an asynchronously retained ticket.
    trace_event("pipeline_pending", state_->frame, state_->key, started, ended, int(result));
  };
  std::exception_ptr presentationError;
  try {
    for (;;) {
      // Internal polling must not duplicate the enclosing exact wait metric.
      if (auto result = wait_for_impl(1000000)) {
        if (presentationError) {
          if (result->pipeline) vkDestroyPipeline(state_->device, result->pipeline, nullptr);
          std::rethrow_exception(presentationError);
        }
        recordWait(result->result);
        return *result;
      }
      if (!presentationError && presentWhilePending) try { presentWhilePending(); }
      catch (...) { presentationError = std::current_exception(); }
    }
  } catch (...) {
    recordWait(VK_ERROR_UNKNOWN);
    throw;
  }
}
void PipelineDemandTicket::discard_and_wait() {
  if (!state_) return;
  auto& job = *state_;
  std::unique_lock lock(job.mutex);
  job.wake.wait(lock, [&] { return job.ready; });
  if (job.result.pipeline) vkDestroyPipeline(job.device, job.result.pipeline, nullptr);
  job.result.pipeline = VK_NULL_HANDLE;
  job.consumed = true;
}
std::optional<PipelineDemandTicket> begin_pipeline_async(const GraphicsPipelineState& state,
    uint64_t frame, uint64_t key, const char* kind,
    const std::function<void(GraphicsPipelineState&)>& prepare) {
  auto& c = compiler();
  if (!c.device) throw std::runtime_error("pipeline demand compiler unavailable");
  std::lock_guard lock(c.mutex);
  if (c.stopped) {
    if (c.thread.joinable()) throw std::runtime_error("pipeline compiler shutdown in progress");
    c.stopped = false; c.thread = std::thread(run, std::ref(c));
  }
  if (c.queue.size() >= 8) return std::nullopt;
  auto job = std::make_shared<Job>(state, frame, key, kind, c.device, prepare);
  c.queue.push_back(job); c.active.fetch_add(1, std::memory_order_release);
  if (job->requested) trace_event("pipeline_request", frame, key,
      job->requested, job->requested, int(c.queue.size()));
  c.wake.notify_one();
  return PipelineDemandTicket(std::move(job));
}
PipelineBuildResult build_pipeline_async(const GraphicsPipelineState& state,
    uint64_t frame, uint64_t key, const char* kind,
    const std::function<void()>& presentWhilePending,
    const std::function<void(GraphicsPipelineState&)>& prepare) {
  std::exception_ptr presentationError;
  uint64_t admissionStarted = 0;
  for (;;) {
    if (auto ticket = begin_pipeline_async(state, frame, key, kind, prepare)) {
      if (admissionStarted) {
        const auto ended = stutter::now();
        trace_event("pipeline_admission_wait", frame, key, admissionStarted, ended, 0);
        trace_event("pipeline_wait", frame, key, admissionStarted, ended, 0);
        trace_event("pipeline_pending", frame, key, admissionStarted, ended, 0);
      }
      // Admission waiting has the same callback-error cleanup as compilation.
      auto result = ticket->wait([&] {
        if (presentationError) std::rethrow_exception(presentationError);
        if (presentWhilePending) presentWhilePending();
      });
      if (presentationError) {
        if (result.pipeline) vkDestroyPipeline(compiler().device, result.pipeline, nullptr);
        std::rethrow_exception(presentationError);
      }
      return result;
    }
    if (!admissionStarted && stutter::trace().enabled()) admissionStarted = stutter::now();
    if (!presentationError && presentWhilePending) try { presentWhilePending(); }
    catch (...) { presentationError = std::current_exception(); }
    auto& c = compiler();
    std::unique_lock lock(c.mutex);
    c.wake.wait_for(lock, std::chrono::milliseconds(1));
  }
}
bool pipeline_demand_active() { return compiler().active.load(std::memory_order_acquire) != 0; }
bool async_pipeline_enabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("WWHD_VK_ASYNC_PIPELINES");
    return !value || std::strcmp(value, "0");
  }();
  return enabled;
}
bool finish_pipeline_demand(VkPipelineCache target) {
  auto& c = compiler();
  if (!c.device) return false;
  { std::lock_guard lock(c.mutex); c.stopped = true; }
  c.wake.notify_all();
  if (c.thread.joinable()) c.thread.join();
  return merge_pipeline_demand_cache(target, true);
}
bool merge_pipeline_demand_cache(VkPipelineCache target, bool wait) {
  auto& c = compiler();
  std::unique_lock lock(c.cacheMutex, std::defer_lock);
  if (wait) lock.lock();
  else if (!lock.try_lock()) return false;
  if (!target || !c.cache || !c.changed) return false;
  const auto result = vkMergePipelineCaches(c.device, target, 1, &c.cache);
  if (result != VK_SUCCESS) return false;
  c.changed = false;
  return true;
}
}
