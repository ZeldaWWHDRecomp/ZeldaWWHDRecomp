#include "gfx/vulkan/pipeline_demand.h"
#include "pipeline_state_fixture.h"
#include "gfx/vulkan/stutter_trace.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <type_traits>
#include <new>
#include <cstdlib>

using namespace gfxvk;
namespace {
template<class T> T handle(uintptr_t value) {
  if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(value);
  else return T(value);
}
const auto caller = std::this_thread::get_id();
std::mutex gateMutex;
std::condition_variable gate;
bool released = false;
VkResult desired = VK_SUCCESS, mergeDesired = VK_SUCCESS;
std::atomic<bool> failNextCallerAllocation = false, failNextWorkerAllocation = false;
std::atomic<unsigned> creates = 0, destroys = 0, pipelinesDestroyed = 0, merges = 0, compileCalls = 0;
VKAPI_ATTR VkResult VKAPI_CALL create_cache(VkDevice, const VkPipelineCacheCreateInfo*, const VkAllocationCallbacks*, VkPipelineCache* out) {
  assert(std::this_thread::get_id() != caller);
  *out = handle<VkPipelineCache>(10); ++creates; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroy_cache(VkDevice, VkPipelineCache, const VkAllocationCallbacks*) { ++destroys; }
VKAPI_ATTR void VKAPI_CALL destroy_pipeline(VkDevice, VkPipeline, const VkAllocationCallbacks*) { ++pipelinesDestroyed; }
VKAPI_ATTR VkResult VKAPI_CALL merge_cache(VkDevice, VkPipelineCache, uint32_t count, const VkPipelineCache*) {
  assert(std::this_thread::get_id() == caller && count == 1);
  ++merges; return mergeDesired;
}
VKAPI_ATTR VkResult VKAPI_CALL create_pipeline(VkDevice, VkPipelineCache, uint32_t count,
    const VkGraphicsPipelineCreateInfo* info, const VkAllocationCallbacks*, VkPipeline* out) {
  assert(std::this_thread::get_id() != caller && count == 1);
  ++compileCalls;
  std::unique_lock lock(gateMutex);
  gate.wait(lock, [] { return released; });
  // The callback mutated the source snapshot during compilation. This job
  // must still see the original CPU arrays and retained immutable handles.
  assert(info->pVertexInputState->pVertexBindingDescriptions[0].stride == 24);
  assert(info->layout == handle<VkPipelineLayout>(7));
  assert(info->pStages[0].module == handle<VkShaderModule>(8));
  assert(info->pStages[1].module == handle<VkShaderModule>(9));
  *out = handle<VkPipeline>(42); // Deliberately return a partial handle on failure.
  return desired;
}
void reset(VkResult result) {
  std::lock_guard lock(gateMutex); released = false; desired = result;
}
void release() {
  { std::lock_guard lock(gateMutex); released = true; }
  gate.notify_all();
}
GraphicsPipelineState state() {
  auto result = pipeline_test::state();
  auto* info = const_cast<VkGraphicsPipelineCreateInfo*>(result.get());
  info->layout = handle<VkPipelineLayout>(7);
  auto* stages = const_cast<VkPipelineShaderStageCreateInfo*>(info->pStages);
  stages[0].module = handle<VkShaderModule>(8); stages[1].module = handle<VkShaderModule>(9);
  return result;
}
}
// Inject diagnostic allocation failures after driver success. Ordinary state
// allocations happen before arming these flags; only the selected thread fails.
void* operator new(std::size_t bytes) {
  auto& fail = std::this_thread::get_id() == caller
      ? failNextCallerAllocation : failNextWorkerAllocation;
  if (fail.exchange(false)) throw std::bad_alloc();
  if (void* value = std::malloc(bytes ? bytes : 1)) return value;
  throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
namespace gfxvk {
PFN_vkCreatePipelineCache vkCreatePipelineCache = create_cache;
PFN_vkDestroyPipelineCache vkDestroyPipelineCache = destroy_cache;
PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines = create_pipeline;
PFN_vkDestroyPipeline vkDestroyPipeline = destroy_pipeline;
PFN_vkMergePipelineCaches vkMergePipelineCaches = merge_cache;
}
int main() {
  const auto tracePath = std::filesystem::temp_directory_path() /
      ("wwhd-demand-test-" + std::to_string(stutter::now()) + ".csv");
#ifdef _WIN32
  _putenv_s("WWHD_VK_STUTTER_TRACE", tracePath.string().c_str());
#else
  setenv("WWHD_VK_STUTTER_TRACE", tracePath.string().c_str(), 1);
#endif
  std::atexit([] { assert(creates == destroys); });

  start_pipeline_demand(handle<VkDevice>(1), {});
  for (auto result : {VK_SUCCESS, VK_ERROR_UNKNOWN}) {
    reset(result);
    auto source = state();
    unsigned pumps = 0;
    auto built = build_pipeline_async(source, 3, 4, "guest_pipeline", [&] {
      auto* info = source.get();
      const_cast<VkVertexInputBindingDescription*>(info->pVertexInputState->pVertexBindingDescriptions)[0].stride = 999;
      if (++pumps == 3) release();
    });
    assert(pumps >= 3 && built.result == result);
    if (result == VK_SUCCESS) {
      assert(built.pipeline == handle<VkPipeline>(42));
      vkDestroyPipeline(handle<VkDevice>(1), built.pipeline, nullptr);
    } else assert(!built.pipeline);
  }
  assert(pipelinesDestroyed == 2); // Caller success + worker partial-failure cleanup.
  reset(VK_SUCCESS);
  bool propagated = false;
  try {
    build_pipeline_async(state(), 3, 4, "guest_pipeline", [&] {
      release(); throw std::runtime_error("presentation failure");
    });
  } catch (const std::runtime_error&) { propagated = true; }
  assert(propagated && pipelinesDestroyed == 3);
  assert(finish_pipeline_demand(handle<VkPipelineCache>(11)));
  assert(!pipeline_demand_active() && merges == 1 && creates == 1);
  // A cache checkpoint may be followed by more rendering. Restart safely,
  // retaining the compiler cache rather than creating/leaking another one.
  reset(VK_SUCCESS);
  auto built = build_pipeline_async(state(), 3, 4, "guest_pipeline", release);
  assert(built.result == VK_SUCCESS);
  vkDestroyPipeline(handle<VkDevice>(1), built.pipeline, nullptr);
  assert(finish_pipeline_demand(handle<VkPipelineCache>(11)));
  assert(merges == 2 && creates == 1);
  // Utility bootstrap has no completed image to pump. An absent callback is
  // valid and must not turn a slow successful compilation into an exception.
  reset(VK_SUCCESS);
  std::thread unblock([] {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    release();
  });
  auto utility = build_pipeline_async(state(), 3, 0, "present_pipeline", {});
  unblock.join();
  assert(utility.result == VK_SUCCESS && utility.pipeline);
  vkDestroyPipeline(handle<VkDevice>(1), utility.pipeline, nullptr);
  assert(finish_pipeline_demand(handle<VkPipelineCache>(11)));
  assert(merges == 3 && creates == 1);
  // The same retained ticket can be polled over multiple frames, without
  // enqueuing duplicate compiles. CPU state is independently owned.
  reset(VK_SUCCESS);
  auto source = state();
  const auto beforeCalls = compileCalls.load();
  std::string temporaryKind = "guest_pipeline";
  auto ticket = begin_pipeline_async(source, 5, 6, temporaryKind.c_str());
  // Both the job and buffered trace must own labels beyond caller/job lifetime.
  temporaryKind.assign(4096, 'x');
  assert(ticket);
  while (compileCalls == beforeCalls) std::this_thread::yield();
  const_cast<VkVertexInputBindingDescription*>(source.get()->pVertexInputState->pVertexBindingDescriptions)[0].stride = 999;
  assert(!ticket->wait_for(0));
  assert(!ticket->wait_for(1000000));
  assert(!ticket->wait_for(0));
  assert(compileCalls == beforeCalls + 1);
  release();
  auto ready = ticket->wait({});
  assert(ready.result == VK_SUCCESS && ready.pipeline);
  vkDestroyPipeline(handle<VkDevice>(1), ready.pipeline, nullptr);
  bool twice = false;
  try { (void)ticket->wait_for(0); } catch (const std::logic_error&) { twice = true; }
  assert(twice);
  ticket.reset();

  // Abandoned pending jobs still own and destroy their eventual result. Eight
  // queued jobs are admitted; pressure gives an explicit Exact fallback signal.
  reset(VK_SUCCESS);
  const auto beforeAbandonCalls = compileCalls.load();
  const auto beforeAbandonDestroy = pipelinesDestroyed.load();
  auto running = begin_pipeline_async(state(), 6, 7, "guest_pipeline");
  assert(running);
  while (compileCalls == beforeAbandonCalls) std::this_thread::yield();
  std::vector<PipelineDemandTicket> queued;
  for (unsigned i = 0; i < 8; ++i) {
    auto next = begin_pipeline_async(state(), 6, 8 + i, "guest_pipeline");
    assert(next); queued.push_back(std::move(*next));
  }
  std::atomic<unsigned> capacityPrepared=0;
  const std::function<void(GraphicsPipelineState&)> prepareCapacity=[&](GraphicsPipelineState&) {
    assert(std::this_thread::get_id()!=caller); ++capacityPrepared;
  };
  assert(!begin_pipeline_async(state(), 6, 99, "guest_pipeline", prepareCapacity));
  assert(capacityPrepared==0); // Failed admission never executes preparation.
  running.reset(); queued.clear();
  unsigned capacityPumps = 0;
  auto capacityFallback = build_pipeline_async(state(), 6, 99, "guest_pipeline", [&] {
    if (++capacityPumps == 3) release();
  }, prepareCapacity);
  assert(capacityPrepared==1);
  assert(capacityPumps >= 3 && capacityFallback.pipeline);
  vkDestroyPipeline(handle<VkDevice>(1), capacityFallback.pipeline, nullptr);
  release();
  assert(finish_pipeline_demand(handle<VkPipelineCache>(11)));
  assert(compileCalls == beforeAbandonCalls + 10);
  assert(pipelinesDestroyed == beforeAbandonDestroy + 10);

  // Ready failure is distinct from pending, and its partial handle is cleaned.
  reset(VK_ERROR_UNKNOWN);
  auto failed = begin_pipeline_async(state(), 7, 1, "guest_pipeline");
  assert(failed && !failed->wait_for(0));
  release();
  const auto failure = failed->wait({});
  assert(failure.result == VK_ERROR_UNKNOWN && !failure.pipeline);
  failed.reset();
  (void)finish_pipeline_demand(handle<VkPipelineCache>(11));

  // Teardown explicitly drains before releasing modules/layout. Discarding a
  // completed ticket is idempotent and never transfers an unclaimed handle.
  reset(VK_SUCCESS);
  const auto beforeDiscard = pipelinesDestroyed.load();
  auto discarded = begin_pipeline_async(state(), 8, 1, "guest_pipeline");
  assert(discarded);
  std::thread unblockDiscard([] {
    std::this_thread::sleep_for(std::chrono::milliseconds(5)); release();
  });
  discarded->discard_and_wait();
  unblockDiscard.join();
  assert(pipelinesDestroyed == beforeDiscard + 1);
  discarded->discard_and_wait();
  discarded.reset();
  assert(pipelinesDestroyed == beforeDiscard + 1);
  (void)finish_pipeline_demand(handle<VkPipelineCache>(11));


  // Trace allocation failure after compilation cannot change the returned
  // driver result or abandon its handle on either worker or consuming thread.
  reset(VK_SUCCESS);
  const auto beforeDiagnosticWorker = compileCalls.load();
  auto diagnosticWorker = begin_pipeline_async(state(), 9, 2,
      "guest_pipeline_with_owned_diagnostic_label");
  assert(diagnosticWorker);
  // The compiler thread was restarted by this request. Arm only once it is
  // blocked inside the driver, so the injected failure deterministically hits
  // the post-compile trace label rather than thread start-up.
  while (compileCalls == beforeDiagnosticWorker) std::this_thread::yield();
  failNextWorkerAllocation = true;
  release();
  (void)finish_pipeline_demand(handle<VkPipelineCache>(11));
  assert(!failNextWorkerAllocation);
  auto workerResult = diagnosticWorker->wait_for(0);
  assert(workerResult && workerResult->result == VK_SUCCESS && workerResult->pipeline);
  vkDestroyPipeline(handle<VkDevice>(1), workerResult->pipeline, nullptr);
  diagnosticWorker.reset();

  reset(VK_SUCCESS);
  auto diagnosticConsume = begin_pipeline_async(state(), 9, 3, "guest_pipeline");
  assert(diagnosticConsume);
  release();
  (void)finish_pipeline_demand(handle<VkPipelineCache>(11));
  failNextCallerAllocation = true;
  auto consumeResult = diagnosticConsume->wait_for(0);
  assert(!failNextCallerAllocation);
  assert(consumeResult && consumeResult->result == VK_SUCCESS && consumeResult->pipeline);
  vkDestroyPipeline(handle<VkDevice>(1), consumeResult->pipeline, nullptr);
  diagnosticConsume.reset();
  // Prerequisites run on the worker, before any native driver creation. Their
  // owned state mutation is visible to the driver but never changes the caller.
  reset(VK_SUCCESS);
  auto unprepared=state();
  unprepared.set_stage_module(0,handle<VkShaderModule>(80));
  std::mutex prerequisiteMutex; std::condition_variable prerequisiteWake;
  bool prerequisiteEntered=false, prerequisiteReleased=false;
  const auto beforePrerequisites=compileCalls.load();
  auto prepared=begin_pipeline_async(unprepared,10,10,"guest_pipeline",[&](GraphicsPipelineState& owned) {
    assert(std::this_thread::get_id()!=caller);
    std::unique_lock lock(prerequisiteMutex); prerequisiteEntered=true;prerequisiteWake.notify_all();
    prerequisiteWake.wait(lock,[&]{return prerequisiteReleased;});
    owned.set_stage_module(0,handle<VkShaderModule>(8));
    owned.set_stage_module(1,handle<VkShaderModule>(9));
  });
  assert(prepared);
  { std::unique_lock lock(prerequisiteMutex);
    assert(prerequisiteWake.wait_for(lock,std::chrono::seconds(2),[&]{return prerequisiteEntered;})); }
  assert(compileCalls==beforePrerequisites && !prepared->wait_for(0));
  assert(unprepared.get()->pStages[0].module==handle<VkShaderModule>(80));
  release();
  { std::lock_guard lock(prerequisiteMutex); prerequisiteReleased=true; }
  prerequisiteWake.notify_all();
  auto preparedResult=prepared->wait({});assert(preparedResult.pipeline);
  vkDestroyPipeline(handle<VkDevice>(1),preparedResult.pipeline,nullptr);prepared.reset();
  (void)finish_pipeline_demand(handle<VkPipelineCache>(11));
  const auto beforePreparationFailure=compileCalls.load();
  auto preparationFailure=begin_pipeline_async(state(),11,11,"guest_pipeline",[](GraphicsPipelineState&) {
    assert(std::this_thread::get_id()!=caller);throw std::runtime_error("shader preparation failed");
  });
  bool preparationThrew=false;
  try { (void)preparationFailure->wait({}); } catch(const std::runtime_error&) { preparationThrew=true; }
  assert(preparationThrew && compileCalls==beforePreparationFailure);
  preparationFailure.reset();(void)finish_pipeline_demand(handle<VkPipelineCache>(11));
  bool badIndex=false,nullModule=false;
  try { unprepared.set_stage_module(99,handle<VkShaderModule>(8)); } catch(const std::out_of_range&) {badIndex=true;}
  try { unprepared.set_stage_module(0,VK_NULL_HANDLE); } catch(const std::invalid_argument&) {nullModule=true;}
  assert(badIndex && nullModule);
  // Live snapshots skip an in-flight native compile without stopping admission.
  reset(VK_SUCCESS);
  const auto oldCompiles = compileCalls.load();
  auto live = begin_pipeline_async(state(), 20, 20, "guest_pipeline");
  while (compileCalls == oldCompiles) std::this_thread::yield();
  assert(!merge_pipeline_demand_cache(handle<VkPipelineCache>(11)));
  release();
  auto liveResult = live->wait({});
  vkDestroyPipeline(handle<VkDevice>(1), liveResult.pipeline, nullptr); live.reset();
  mergeDesired = VK_ERROR_UNKNOWN;
  assert(!merge_pipeline_demand_cache(handle<VkPipelineCache>(11), true));
  mergeDesired = VK_SUCCESS;
  assert(merge_pipeline_demand_cache(handle<VkPipelineCache>(11), true));
  assert(!merge_pipeline_demand_cache(handle<VkPipelineCache>(11), true));
  // More work after the snapshot stays dirty and merges again at shutdown.
  reset(VK_SUCCESS);
  auto newer = build_pipeline_async(state(), 21, 21, "guest_pipeline", release);
  vkDestroyPipeline(handle<VkDevice>(1), newer.pipeline, nullptr);
  assert(finish_pipeline_demand(handle<VkPipelineCache>(11)));
  stutter::trace().flush();
  std::ifstream trace(tracePath);
  assert(trace.good());
  std::string line;
  unsigned timedWaits = 0, exactWaits = 0, lifetimes = 0, admissionWaits = 0, labelledCompiles = 0;
  uint64_t prerequisiteEnd=0, preparedNativeStart=0, preparedNativeDuration=0;
  std::getline(trace, line); // CSV header.
  while (std::getline(trace, line)) {
    std::istringstream row(line);
    std::array<std::string, 8> fields;
    for (auto& field : fields) std::getline(row, field, ',');
    if(fields[1]=="10" && fields[2]=="000000000000000a") {
      if(fields[0]=="pipeline_prerequisites") prerequisiteEnd=std::stoull(fields[4]);
      if(fields[0]=="guest_pipeline") {
        preparedNativeStart=std::stoull(fields[3]);
        preparedNativeDuration=std::stoull(fields[4])-preparedNativeStart;
      }
    }
    if (fields[0] == "pipeline_admission_wait" && fields[1] == "6" &&
        fields[2] == "0000000000000063") ++admissionWaits;
    if (fields[1] != "5" || fields[2] != "0000000000000006") continue;
    if (fields[0] == "guest_pipeline") ++labelledCompiles;
    if (fields[0] == "pipeline_wait") {
      ++timedWaits;
      assert(std::stoull(fields[4]) >= std::stoull(fields[3]));
    }
    if (fields[0] == "pipeline_pending") ++exactWaits;
    if (fields[0] == "pipeline_ticket_lifetime") ++lifetimes;
  }
  // Three zero-budget polls emit nothing. One bounded poll and one enclosing
  // exact wait emit two actual wait intervals, with no nested 1 ms duplicates.
  assert(timedWaits == 2 && exactWaits == 1 && lifetimes == 1 && admissionWaits == 1 && labelledCompiles == 1);
  assert(prerequisiteEnd && preparedNativeStart>=prerequisiteEnd && preparedNativeDuration==preparedResult.compileNs);
  trace.close();
  std::filesystem::remove(tracePath);

}
