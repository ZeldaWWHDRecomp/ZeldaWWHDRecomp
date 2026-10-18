#include "record_worker.h"
#include "deferred_commands.h"
#include "platform/host.h"
#include "render_prof.h"
#include "platform/r5_slow_render.h"
#include "platform/perf_hint.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <memory>
#include <stdexcept>

namespace gfxvk {
namespace trace = handoff_trace;
using trace::Kind;
using trace::Lane;
namespace {
bool no_record_boost() {
    static const bool value = [] {
        const char* e = std::getenv("WWHD_R5_NO_RECORD_BOOST");
        return e && !std::strcmp(e, "1");
    }();
    return value;
}

}
RecordWorker::RecordWorker() : thread_([this] { run(); }) {}
RecordWorker::~RecordWorker() {
    try { stop(); } catch (...) {} // explicit stop propagates worker errors
}
uint64_t RecordWorker::publish(PodCommands& commands) {
    const uint64_t frame = trace::producer_frame();
    trace::emit(Lane::Producer, Kind::PublishEnter, 0, frame);
    std::unique_lock lock(mutex_);
    const bool blocked = jobs_.size() >= maxQueued;
    if (blocked) trace::emit(Lane::Producer, Kind::CapacityWaitBegin, 0, frame, jobs_.size());
    changed_.wait(lock, [&] { return jobs_.size() < maxQueued || error_ || stopping_; });
    if (blocked) trace::emit(Lane::Producer, Kind::CapacityWaitEnd, 0, frame, jobs_.size());
    if (error_) std::rethrow_exception(error_);
    if (stopping_) throw std::logic_error("record worker already stopped");
    const uint64_t serial = published_ + 1;
    // Allocate queue storage BEFORE moving the producer tail. Allocation failure
    // leaves every pending command owned by the producer for explicit recovery.
    jobs_.push_back({serial, frame, {}});
    jobs_.back().bytes = commands.take();
    published_ = serial;
    trace::emit(Lane::Producer, Kind::Published, serial, frame, jobs_.size(), jobs_.back().bytes.size());
    changed_.notify_all();
    return serial;
}
void RecordWorker::wait(uint64_t serial, trace::Reason reason) {
    std::unique_lock lock(mutex_);
    if (serial > published_) throw std::logic_error("unknown record worker sequence");
    const bool blocked = completed_ < serial && !error_;
    if (blocked) trace::emit(Lane::Producer, Kind::SerialWaitBegin, serial, trace::producer_frame(), uint64_t(reason), completed_);
    changed_.wait(lock, [&] { return completed_ >= serial || error_; });
    if (blocked) trace::emit(Lane::Producer, Kind::SerialWaitEnd, serial, trace::producer_frame(), uint64_t(reason), completed_);
    if (error_) std::rethrow_exception(error_);
}
bool RecordWorker::emitted(uint64_t serial) {
    std::lock_guard lock(mutex_);
    if (serial > published_) throw std::logic_error("unknown record worker sequence");
    if (error_) std::rethrow_exception(error_);
    return completed_ >= serial;
}
void RecordWorker::drain() {
    uint64_t serial;
    { std::lock_guard lock(mutex_); serial = published_; }
    wait(serial, trace::Reason::ExplicitDrain);
}
void RecordWorker::stop() {
    { std::lock_guard lock(mutex_); stopping_ = true; changed_.notify_all(); }
    if (thread_.joinable()) thread_.join();
    std::lock_guard lock(mutex_);
    if (error_) std::rethrow_exception(error_);
}
void RecordWorker::run() {
    host::set_thread_name("Vulkan record");
    const bool forcedSlow = r5_slow_render::apply("Vulkan record");
    // The worker records and submits every frame: the render thread's present waits for it, so it is
    // scheduled like the render thread (gx2_core.cpp): raised priority, the fast cores and, on Android,
    // the performance hint session. Left out, a Xiaomi 14 (Snapdragon 8 Gen 3) ran it on any core
    // at the default priority. WWHD_R5_NO_RECORD_BOOST=1 leaves it as before (measurements).
    const bool boost = !forcedSlow && !no_record_boost();
    if (boost) {
        host::boost_thread_priority();
        perf_hint::add_current_thread();
    }
    std::fprintf(stderr,
        "[R5 isolation] no_record_boost=%u legacy_publish=%u forced_slow=%u boost_requested=%u\n",
        unsigned(no_record_boost()), 1u, unsigned(forcedSlow), unsigned(boost));
    struct LeaveHintSession {
        bool on;
        ~LeaveHintSession() { if (on) perf_hint::remove_current_thread(); }
    } leave{boost};
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            const bool idle = jobs_.empty() && !stopping_;
            if (idle) trace::emit(Lane::Worker, Kind::IdleWaitBegin);
            changed_.wait(lock, [&] { return !jobs_.empty() || stopping_; });
            if (idle) trace::emit(Lane::Worker, Kind::IdleWaitEnd);
            if (jobs_.empty()) return;
            job = std::move(jobs_.front()); jobs_.pop_front();
            trace::emit(Lane::Worker, Kind::Dequeued, job.serial, job.producerFrame, jobs_.size(), job.bytes.size());
            changed_.notify_all();
        }
        if (trace::enabled()) trace::set_worker_job(job.serial, job.producerFrame);
        trace::emit(Lane::Worker, Kind::ReplayBegin, job.serial, job.producerFrame);
        const uint64_t cpuStart = trace::enabled() ? rprof::thread_cpu_ns() : 0;
        try { host::with_autorelease_pool([&] { PodCommands::replay(job.bytes); }); }
        catch (...) {
            std::lock_guard lock(mutex_);
            error_ = std::current_exception(); stopping_ = true;
            jobs_.clear(); changed_.notify_all(); return;
        }
        const uint64_t cpuEnd = trace::enabled() ? rprof::thread_cpu_ns() : 0;
        trace::emit(Lane::Worker, Kind::ReplayEnd, job.serial, job.producerFrame);
        if (trace::enabled()) trace::emit_worker(Kind::ReplayCPU, cpuStart, cpuEnd);
        {
            std::lock_guard lock(mutex_);
            completed_ = job.serial;
            trace::emit(Lane::Worker, Kind::Completed, job.serial, job.producerFrame); changed_.notify_all();
        }
    }
}
namespace {
// Explicit lifetime, like the Vulkan device: no static destructor may enter a
// driver after host framework finalization. Orderly shutdown calls stop below.
RecordWorker* worker = nullptr;
PodCommands pending;
thread_local bool producerPresentationRecording = false;
bool savedChoice = false;
}
void set_record_worker_saved_choice(bool on) { savedChoice = on; }
bool record_worker_requested() {
    // -1: no environment override; the variable takes precedence over the saved setting
    static const int environment = [] {
        const char* e = std::getenv("WWHD_VK_RECORD_WORKER");
        return e ? int(!std::strcmp(e, "1")) : -1;
    }();
    return environment >= 0 ? environment == 1 : savedChoice;
}
bool producer_presentation_recording() { return producerPresentationRecording; }
bool record_worker_active() { return bool(worker); }
PodCommands* record_worker_commands() {
    return worker && !producerPresentationRecording ? &pending : nullptr;
}
ProducerPresentationRecording::ProducerPresentationRecording() {
    if (!worker || producerPresentationRecording)
        throw std::logic_error("invalid producer presentation recording scope");
    drain_record_commands(fallback_stats::Boundary::ProducerPresentation);
    previousDispatch_ = isolated_command_dispatch;
    producerPresentationRecording = true;
    isolated_command_dispatch = true;
}
ProducerPresentationRecording::~ProducerPresentationRecording() {
    isolated_command_dispatch = previousDispatch_;
    producerPresentationRecording = false;
}
void start_record_worker() {
    if (!record_worker_requested() || worker) return;
    // Resolve isolation controls before starting the worker, never while Off.
    (void)no_record_boost();
    const char* scalarOption = std::getenv("WWHD_R5_DIRECT_SCALARS");
    const bool directScalars = scalarOption && !std::strcmp(scalarOption, "1");
    pending.set_direct_scalars(directScalars);
    if (directScalars) std::fprintf(stderr, "[R5 scalar encoding] requested=1 effective=1 opcodes=1,2,3,4\n");
    fallback_stats::initialize(); // producer-only, default-off, before worker creation
    (void)trace::enabled(); // preallocate diagnostic lanes on producer before thread startup
    auto next = std::make_unique<RecordWorker>();
    deferred_commands().set_pod_commands(&pending);
    worker = next.release();
}
uint64_t publish_record_commands() {
    return worker && !pending.empty() ? worker->publish(pending) : 0;
}
void publish_record_commands_at_draw_boundary() {
    // Restore the previous policy: only a complete draw crossing 32 KiB
    // publishes early. No draw-count or pass-end trigger is retained.
    constexpr size_t earlyPublishBytes = 32 * 1024;
    if (worker && !producerPresentationRecording && pending.payload_bytes() >= earlyPublishBytes)
        worker->publish(pending);
}
void wait_record_commands(uint64_t serial, trace::Reason reason) {
    if (!serial) return;
    if (!worker) throw std::logic_error("record submission outlived its worker");
    worker->wait(serial, reason);
}
bool record_commands_emitted(uint64_t serial) {
    if (!serial) return true;
    if (!worker) throw std::logic_error("record submission outlived its worker");
    return worker->emitted(serial);
}
void drain_record_commands(fallback_stats::Boundary boundary, uint32_t opcode, PodFailureReason reason) {
    if (!worker) return;
    fallback_stats::Drain observation(boundary, opcode, reason, pending.empty());
    publish_record_commands(); worker->drain();
    observation.complete();
}
void stop_record_worker() {
    if (!worker) return;
    std::exception_ptr failure;
    try { drain_record_commands(fallback_stats::Boundary::Shutdown); } catch (...) { failure = std::current_exception(); }
    try { worker->stop(); } catch (...) { if (!failure) failure = std::current_exception(); }
    // Even a failed Vulkan replay must join the thread and withdraw the facade.
    // The failed renderer's unsubmitted tail is discarded only after joining;
    // it must never outlive the device or be replayed as a successful fallback.
    deferred_commands().set_pod_commands(nullptr);
    delete worker; worker = nullptr; pending.clear();
    if (failure) std::rethrow_exception(failure);
}
uint64_t submit_record_commands(VkDevice device, VkQueue queue, VkCommandBuffer cmd,
    VkFence fence, VkSemaphore wait, VkSemaphore signal,
    VkSemaphore extraWait, VkSemaphore extraSignal) {
    if (!worker) throw std::logic_error("record worker submission while disabled");
    if (!pending.submit(device, queue, cmd, fence, wait, signal, extraWait, extraSignal)) {
        // Complete the older prefix before admitting the fixed-size submission.
        // No pointer into the producer's state crosses this boundary.
        const auto reason = pending.failure_reason();
        drain_record_commands(fallback_stats::Boundary::EncoderFallback, 29, reason);
        if (!pending.submit(device, queue, cmd, fence, wait, signal, extraWait, extraSignal)) {
            fallback_stats::fatal_submit_retry(pending.failure_reason());
            throw std::runtime_error("record worker submit exceeds stream limits");
        }
    }
    return publish_record_commands();
}
}
