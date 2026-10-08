#include "scheduler_tick.h"
#include <atomic>
#include <cassert>
#include <future>
#include <thread>
#include <cstdio>
using namespace std::chrono_literals;
int main() {
    threads::SchedulerTick tick;
    tick.ready_delta(1);
    assert(!tick.wait_idle()); // ready contender must retain time-slice polling
    tick.ready_delta(-1);
    {
        threads::SchedulerTick::TimedWait timed(tick);
        assert(!tick.wait_idle());
        {
            threads::SchedulerTick::TimedWait second(tick);
            assert(!tick.wait_idle());
        }
        assert(!tick.wait_idle()); // one wait ending must not hide another deadline
    }
    auto start = std::chrono::steady_clock::now();
    assert(tick.wait_idle()); // safety wake, even if all notifications were missed
    assert(std::chrono::steady_clock::now() - start >= 90ms);
    for (int i = 0; i < 100; ++i) {
        std::promise<void> entering;
        auto entered = entering.get_future();
        auto waiter = std::async(std::launch::async, [&] { entering.set_value(); return tick.wait_idle(); });
        entered.wait();
        tick.ready_delta(1); // race predicate check against notification
        assert(waiter.wait_for(80ms) == std::future_status::ready);
        waiter.get();
        tick.ready_delta(-1);
    }
    for (int kind = 0; kind < 2; ++kind) {
        auto waiter = std::async(std::launch::async, [&] { return tick.wait_idle(); });
        std::this_thread::sleep_for(5ms);
        if (kind == 0) tick.notify(); // new/rearmed alarm
        else tick.timed_delta(1); // deadline wait begins
        assert(waiter.wait_for(80ms) == std::future_status::ready);
        waiter.get();
        if (kind) tick.timed_delta(-1);
    }
    // HLE timeouts are host deadlines, unaffected by an idle scheduler tick.
    std::mutex mutex;
    std::condition_variable alarm;
    auto deadline = std::chrono::steady_clock::now() + 10ms;
    {
        threads::SchedulerTick::TimedWait timed(tick);
        std::unique_lock<std::mutex> lock(mutex);
        alarm.wait_until(lock, deadline, [] { return false; });
    }
    assert(std::chrono::steady_clock::now() >= deadline);
    puts("idle cap, ready contention, timed wait and alarm notifications pass");
}
