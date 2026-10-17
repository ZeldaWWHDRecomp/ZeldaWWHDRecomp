#include "handoff_trace.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#ifdef __APPLE__
extern "C" double CACurrentMediaTime(void);
#endif
namespace gfxvk::handoff_trace {
namespace {
constexpr size_t Capacity = 262144;
thread_local uint64_t jobSerial = 0, jobFrame = 0;
struct State {
    std::string path;
    Buffer<Capacity> lanes[2];
    uint64_t frame = 0;
    std::atomic<bool> recording{true};
    std::atomic<unsigned> writers{0};
    explicit State(const char* p) : path(p) {}
};
State* state() {
    static const auto value = []() -> std::unique_ptr<State> {
        const char* p = std::getenv("WWHD_VK_HANDOFF_TRACE");
        return p && *p ? std::make_unique<State>(p) : nullptr;
    }();
    return value.get();
}
uint64_t now() {
#ifdef __APPLE__
    // Same clock and rounding as present_observer::native_now_ns.
    return static_cast<uint64_t>(std::llround(CACurrentMediaTime() * 1e9));
#else
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
}
uint64_t native_now_ns() { return now(); }
bool enabled() { return state() != nullptr; }
void set_producer_frame(uint64_t frame) { if (auto* s = state()) s->frame = frame; }
uint64_t producer_frame() { auto* s = state(); return s ? s->frame : 0; }
void emit(Lane lane, Kind kind, uint64_t serial, uint64_t frame, uint64_t a, uint64_t b) {
    auto* s = state();
    if (!s || !s->recording.load(std::memory_order_seq_cst)) return;
    s->writers.fetch_add(1, std::memory_order_seq_cst);
    if (s->recording.load(std::memory_order_seq_cst))
        s->lanes[static_cast<size_t>(lane)].append({now(), serial, frame, a, b, kind});
    s->writers.fetch_sub(1, std::memory_order_seq_cst);
}
void set_worker_job(uint64_t serial, uint64_t frame) { jobSerial = serial; jobFrame = frame; }
void emit_worker(Kind kind, uint64_t a, uint64_t b) { emit(Lane::Worker, kind, jobSerial, jobFrame, a, b); }
void slow_opcode(uint32_t opcode, uint64_t start, uint64_t end, uint64_t cpu_start, uint64_t cpu_end) {
    if (end < start || end - start < 250000) return;
    // Delayed admission preserves raw start=end-duration without backdating
    // event order. CPU zero is explicitly unavailable, never evidence of idle.
    auto* s = state();
    if (!s || !s->recording.load(std::memory_order_seq_cst)) return;
    s->writers.fetch_add(1, std::memory_order_seq_cst);
    if (s->recording.load(std::memory_order_seq_cst)) {
        auto& buffer = s->lanes[static_cast<size_t>(Lane::Worker)];
        buffer.append({end, jobSerial, jobFrame, opcode, end-start, Kind::SlowOpcode});
        buffer.append({end, jobSerial, jobFrame, opcode,
            cpu_start && cpu_end >= cpu_start ? cpu_end-cpu_start : 0, Kind::SlowOpcodeCPU});
    }
    s->writers.fetch_sub(1, std::memory_order_seq_cst);
}
bool flush() {
    auto* s = state();
    if (!s) return true;
    s->recording.store(false, std::memory_order_seq_cst);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (s->writers.load(std::memory_order_seq_cst)) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::yield();
    }
    FILE* f = std::fopen(s->path.c_str(), "w");
    if (!f) return false;
    bool ok = std::fprintf(f, "{\"schema\":2,\"clock\":\"%s\",\"capacity_per_lane\":%zu,\"job_frame_semantics\":\"producer frame at publication; job may span frames\",\"lanes\":[",
#ifdef __APPLE__
        "CACurrentMediaTime",
#else
        "steady_clock; native presentation clock join unavailable",
#endif
        Capacity) >= 0;
    for (size_t lane = 0; lane < 2; ++lane) {
        auto& buffer = s->lanes[lane];
        const size_t reserved = buffer.reserved.load(std::memory_order_acquire);
        size_t published = 0;
        ok &= std::fprintf(f, "%s{\"lane\":%zu,\"reserved\":%zu,\"dropped\":%llu,\"events\":[", lane ? "," : "", lane, reserved,
            static_cast<unsigned long long>(buffer.dropped.load())) >= 0;
        buffer.visit([&](const Event& e) {
            ok &= std::fprintf(f, "%s[%llu,%u,%llu,%llu,%llu,%llu]", published++ ? "," : "",
                static_cast<unsigned long long>(e.ns), unsigned(e.kind),
                static_cast<unsigned long long>(e.serial), static_cast<unsigned long long>(e.producer_frame),
                static_cast<unsigned long long>(e.a), static_cast<unsigned long long>(e.b)) >= 0;
        });
        const size_t admitted = reserved < Capacity ? reserved : Capacity;
        ok &= std::fprintf(f, "],\"unpublished\":%zu}", admitted - published) >= 0;
    }
    ok &= std::fprintf(f, "],\"kind_names\":[\"PublishEnter\",\"CapacityWaitBegin\",\"CapacityWaitEnd\",\"Published\",\"IdleWaitBegin\",\"IdleWaitEnd\",\"Dequeued\",\"ReplayBegin\",\"ReplayEnd\",\"Completed\",\"SerialWaitBegin\",\"SerialWaitEnd\",\"QueueSubmitBegin\",\"QueueSubmitEnd\",\"PresentBegin\",\"PresentEnd\",\"FenceWaitBegin\",\"FenceWaitEnd\",\"AcquireBegin\",\"AcquireEnd\",\"DrainBegin\",\"DrainEnd\",\"UploadGrowBegin\",\"UploadGrowEnd\",\"ReplayCPU\",\"OpcodeInventory\",\"SlowOpcode\",\"SlowOpcodeCPU\"],\"reason_names\":[\"Unspecified\",\"Present\",\"SlotReuse\",\"Upload\",\"Fallback\",\"ExplicitDrain\",\"Shutdown\",\"ExternalSyncPrefix\",\"PresentPrefix\",\"CpuSubmissionAck\",\"SwapPrefix\"],\"small_job_max_bytes\":2048,\"slow_opcode_threshold_ns\":250000,\"thread_cpu_clock\":\"rprof::thread_cpu_ns; zero unavailable\",\"opcode_names\":[\"unused\",\"vkCmdBindPipeline\",\"vkCmdBindIndexBuffer\",\"vkCmdDraw\",\"vkCmdDrawIndexed\",\"vkCmdDispatch\",\"vkCmdEndRendering\",\"vkCmdSetStencilReference\",\"vkCmdSetStencilWriteMask\",\"vkCmdResetQueryPool\",\"vkCmdWriteTimestamp\",\"vkCmdSetViewport\",\"vkCmdSetScissor\",\"vkCmdCopyBuffer\",\"vkCmdCopyBufferToImage\",\"vkCmdCopyImageToBuffer\",\"vkCmdCopyImage\",\"vkCmdBlitImage\",\"vkCmdBindVertexBuffers\",\"vkCmdBindDescriptorSets\",\"vkCmdPushConstants\",\"vkCmdSetBlendConstants\",\"vkCmdClearColorImage\",\"vkCmdClearDepthStencilImage\",\"vkCmdBeginRendering\",\"vkCmdPipelineBarrier\",\"vkCmdBeginRenderPass\",\"vkCmdEndRenderPass\",\"beginCommandBuffer\",\"submit\"],\"event_fields\":[\"ns\",\"kind\",\"serial\",\"producer_frame\",\"a\",\"b\"]}\n") >= 0;
    ok &= std::fclose(f) == 0;
    return ok;
}
}
