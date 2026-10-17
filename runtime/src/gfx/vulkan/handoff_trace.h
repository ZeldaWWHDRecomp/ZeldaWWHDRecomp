#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace gfxvk::handoff_trace {
enum class Lane : uint8_t { Producer, Worker };
enum class Kind : uint8_t {
    PublishEnter, CapacityWaitBegin, CapacityWaitEnd, Published,
    IdleWaitBegin, IdleWaitEnd, Dequeued, ReplayBegin, ReplayEnd,
    Completed, SerialWaitBegin, SerialWaitEnd, QueueSubmitBegin,
    QueueSubmitEnd, PresentBegin, PresentEnd, FenceWaitBegin, FenceWaitEnd,
    AcquireBegin, AcquireEnd, DrainBegin, DrainEnd, UploadGrowBegin, UploadGrowEnd, ReplayCPU, OpcodeInventory, SlowOpcode, SlowOpcodeCPU
};
enum class Reason : uint8_t { Unspecified, Present, SlotReuse, Upload, Fallback, ExplicitDrain, Shutdown, ExternalSyncPrefix, PresentPrefix, CpuSubmissionAck, SwapPrefix };
struct Event {
    uint64_t ns, serial, producer_frame, a, b;
    Kind kind;
};
// Never overwrite: readers only inspect release-published records. A missing
// publication is reported, never read concurrently or silently filled.
template<size_t Capacity> struct Buffer {
    std::array<Event, Capacity> events{};
    std::array<std::atomic<bool>, Capacity> ready{};
    std::atomic<size_t> reserved{0};
    std::atomic<uint64_t> dropped{0};
    bool append(const Event& event) {
        const size_t i = reserved.fetch_add(1, std::memory_order_relaxed);
        if (i >= Capacity) { dropped.fetch_add(1, std::memory_order_relaxed); return false; }
        events[i] = event;
        ready[i].store(true, std::memory_order_release);
        return true;
    }
    template<class F> void visit(F&& f) const {
        const size_t n = reserved.load(std::memory_order_acquire);
        for (size_t i = 0; i < n && i < Capacity; ++i)
            if (ready[i].load(std::memory_order_acquire)) f(events[i]);
    }
};
bool enabled();
uint64_t native_now_ns();
void slow_opcode(uint32_t opcode, uint64_t wall_start, uint64_t wall_end, uint64_t cpu_start, uint64_t cpu_end);
void emit(Lane lane, Kind kind, uint64_t serial = 0, uint64_t producer_frame = 0,
          uint64_t a = 0, uint64_t b = 0);
void set_producer_frame(uint64_t frame); // producer-only, diagnostic metadata
uint64_t producer_frame();
void set_worker_job(uint64_t serial, uint64_t frame);
void emit_worker(Kind kind, uint64_t a = 0, uint64_t b = 0);
bool flush(); // caller drains worker first; bounded JSON, no reset/reuse
}
