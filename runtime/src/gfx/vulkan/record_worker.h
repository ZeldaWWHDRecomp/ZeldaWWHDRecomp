#pragma once
#include "pod_commands.h"
#include "handoff_trace.h"
#include "fallback_stats.h"
#include <condition_variable>
#include <array>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>

namespace gfxvk {
// Worker jobs contain owned POD bytes, sequence and diagnostic publication-frame
// metadata only (not a guarantee that all commands belong to that frame). Vulkan objects
// remain owned by the producer's submission slot until its GPU fence completes.
class RecordWorker {
public:
    RecordWorker();
    ~RecordWorker();
    RecordWorker(const RecordWorker&) = delete;
    RecordWorker& operator=(const RecordWorker&) = delete;
    uint64_t publish(PodCommands& commands);
    void wait(uint64_t serial, handoff_trace::Reason reason = handoff_trace::Reason::Unspecified);
    bool emitted(uint64_t serial);
    void drain();
    void stop();
private:
    struct Job { uint64_t serial; uint64_t producerFrame; std::vector<std::byte> bytes; };
    static constexpr size_t maxQueued = 4;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Job> jobs_;
    // Return consumed byte storage to the producer instead of reallocating and
    // growing a new command stream after every publication. Bounded by queue
    // depth; these buffers contain no live Vulkan commands or resource owners.
    std::array<std::vector<std::byte>, maxQueued + 1> recycled_;
    size_t recycledCount_ = 0;
    uint64_t published_ = 0, completed_ = 0;
    bool stopping_ = false;
    std::exception_ptr error_;
    std::thread thread_;
    void run();
};
// Producer-only, under the GX2 execution mutex. Drain the previous worker
// prefix before touching its command buffer, bypass both raw begin routing and
// the deferred facade, and restore routing after producer submission/present.
class ProducerPresentationRecording {
public:
    ProducerPresentationRecording();
    ~ProducerPresentationRecording();
    ProducerPresentationRecording(const ProducerPresentationRecording&) = delete;
    ProducerPresentationRecording& operator=(const ProducerPresentationRecording&) = delete;
private:
    bool previousDispatch_ = false;
};
// The saved Graphics > Advanced choice ("Record graphics on a second thread"), set by the
// renderer before it starts; WWHD_VK_RECORD_WORKER (exactly 1 on, anything else off) wins.
void set_record_worker_saved_choice(bool on);
bool record_worker_requested(); // no thread while disabled
// Default of Graphics > Advanced "Record graphics on a second thread" when the player never chose:
// on for Android (phones are usually limited by the render thread), off elsewhere
// Off by default everywhere: on a Xiaomi 14 (Snapdragon 8 Gen 3) it made typical frames ~0.6 ms faster but
// the slowest 1% ~3 ms slower (1% low 42 instead of 49 fps at 120 fps interpolation) than the single-thread path.
constexpr bool record_worker_default() { return false; }
bool producer_presentation_recording(); // producer-only scoped routing
bool record_worker_active(); // producer only, under GX2 execution mutex
void start_record_worker();
void stop_record_worker();
PodCommands* record_worker_commands();
uint64_t publish_record_commands();
// Append a detached eye's owned POD stream after the producer prefix without
// waiting for either stream to finish recording. Submission ownership is unchanged.
bool publish_held_record_commands(PodCommands& commands);
void publish_record_commands_at_draw_boundary();
void wait_record_commands(uint64_t serial, handoff_trace::Reason reason = handoff_trace::Reason::Unspecified);
bool record_commands_emitted(uint64_t serial);
void drain_record_commands(fallback_stats::Boundary boundary = fallback_stats::Boundary::ExplicitState,
    uint32_t opcode = 0, PodFailureReason reason = PodFailureReason::UnclassifiedEncoderReject);
uint64_t submit_record_commands(VkDevice device, VkQueue queue, VkCommandBuffer cmd,
    VkFence fence, VkSemaphore wait, VkSemaphore signal,
    VkSemaphore extraWait = VK_NULL_HANDLE, VkSemaphore extraSignal = VK_NULL_HANDLE);
}
