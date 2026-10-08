// The tick only requests preemption; HLE deadlines remain owned by their host waits.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <mutex>

namespace threads {
class SchedulerTick {
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<unsigned> ready{0}, timed{0};
    uint64_t generation = 0;
public:
    void ready_delta(int delta) {
        if (ready.fetch_add(delta) == 0 && delta > 0) notify();
    }
    void timed_delta(int delta) {
        if (timed.fetch_add(delta) == 0 && delta > 0) notify();
    }
    void notify() {
        { std::lock_guard<std::mutex> lock(mutex); ++generation; }
        cv.notify_one();
    }
    // Never hold this mutex while inspecting a core's ready queue (opposite lock order).
    bool wait_idle() {
        if (ready.load() || timed.load()) return false;
        std::unique_lock<std::mutex> lock(mutex);
        if (ready.load() || timed.load()) return false;
        auto before = generation;
        cv.wait_for(lock, std::chrono::milliseconds(100), [&] { return ready.load() || timed.load() || generation != before; });
        return true;
    }
    class TimedWait {
        SchedulerTick* tick;
    public:
        explicit TimedWait(SchedulerTick& owner, bool active = true) : tick(active ? &owner : nullptr) {
            if (tick) tick->timed_delta(1);
        }
        ~TimedWait() { if (tick) tick->timed_delta(-1); }
        TimedWait(const TimedWait&) = delete;
        TimedWait& operator=(const TimedWait&) = delete;
    };
};
} // namespace threads
