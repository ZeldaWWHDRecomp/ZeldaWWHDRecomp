// OSRendezvous semantics (runtime/src/rendezvous.h): arrivals are per core, a waiter passes once
// every core in its mask has arrived. The harness below waits the way the HLE in threads.cpp does
// (the shared state behind one mutex, waiters on a condition variable, the caller's core marked
// before the wait).
#include "rendezvous.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <future>
#include <mutex>
#include <thread>
using namespace std::chrono_literals;

namespace {
struct Rendezvous {
    std::mutex m;
    std::condition_variable cv;
    threads::RendezvousCores cores;
    void init() {
        std::lock_guard<std::mutex> lk(m);
        cores.reset();
    }
    // timeout < 0: forever
    bool wait(uint32_t core, uint32_t mask, std::chrono::milliseconds timeout = -1ms) {
        std::unique_lock<std::mutex> lk(m);
        cores.arrive(core);
        cv.notify_all();
        auto done = [&] { return cores.complete(mask); };
        if (timeout < 0ms) {
            cv.wait(lk, done);
            return true;
        }
        return cv.wait_for(lk, timeout, done);
    }
};
std::future<bool> wait_async(Rendezvous& r, uint32_t core, uint32_t mask, std::chrono::milliseconds timeout = -1ms) {
    return std::async(std::launch::async, [&r, core, mask, timeout] { return r.wait(core, mask, timeout); });
}
bool blocked(std::future<bool>& f) { return f.wait_for(50ms) == std::future_status::timeout; }
bool passes(std::future<bool>& f) { return f.wait_for(2s) == std::future_status::ready && f.get(); }
}  // namespace

int main() {
    // pure state
    {
        threads::RendezvousCores s;
        assert(s.complete(0));          // empty mask: nothing to wait for
        assert(!s.complete(1));
        s.arrive(1);
        assert(s.complete(2));
        assert(!s.complete(1));         // core 1's arrival does not stand in for core 0
        assert(!s.complete(3));
        s.arrive(1);                    // a second arrival on the same core is still one core
        assert(!s.complete(3));
        s.arrive(0);
        assert(s.complete(3) && s.complete(1) && !s.complete(7));
        assert(s.complete(0x8 | 3));    // bits above core 2 are ignored
        s.arrive(5);                    // no such core
        assert(s.arrived == 3);
        s.reset();
        assert(s.arrived == 0 && !s.complete(1));
    }
    // one-core mask, the caller on another core: must wait (the WWHD sound start-up: the main
    // thread on core 1 waits with mask 1 for the thread it started on core 0)
    {
        Rendezvous r;
        r.init();
        auto main_wait = wait_async(r, 1, 1);
        assert(blocked(main_wait));
        auto worker = wait_async(r, 0, 1 | 2);
        assert(passes(worker) && passes(main_wait));
    }
    // the same pair, worker first: the worker waits for core 1, the main thread then passes at once
    {
        Rendezvous r;
        r.init();
        auto worker = wait_async(r, 0, 1 | 2);
        assert(blocked(worker));
        assert(r.wait(1, 1));
        assert(passes(worker));
    }
    // two threads on the same core are one arrival
    {
        Rendezvous r;
        r.init();
        auto a = wait_async(r, 1, 3), b = wait_async(r, 1, 3);
        assert(blocked(a) && blocked(b));
        assert(r.wait(0, 3));
        assert(passes(a) && passes(b));
    }
    // three-core mask, arrivals in every order
    for (int order = 0; order < 6; order++) {
        static const uint32_t perms[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
        Rendezvous r;
        r.init();
        auto first = wait_async(r, perms[order][0], 7);
        auto second = wait_async(r, perms[order][1], 7);
        assert(blocked(first) && blocked(second));
        auto third = wait_async(r, perms[order][2], 7);
        assert(passes(first) && passes(second) && passes(third));
    }
    // a mask without the caller's core still marks the caller's core
    {
        Rendezvous r;
        r.init();
        auto core2 = wait_async(r, 2, 1);  // waits for core 0 only
        assert(blocked(core2));
        assert(r.wait(0, 4));              // core 2 has arrived already
        assert(passes(core2));
    }
    // own core only / empty mask: no wait
    {
        Rendezvous r;
        r.init();
        assert(r.wait(1, 2));
        assert(r.wait(2, 0));
    }
    // timeout: false, and the caller's flag stays set
    {
        Rendezvous r;
        r.init();
        auto start = std::chrono::steady_clock::now();
        assert(!r.wait(1, 1, 20ms));
        assert(std::chrono::steady_clock::now() - start >= 20ms);
        assert(r.wait(0, 2, 0ms));         // core 1 arrived (and timed out) earlier
        assert(r.wait(1, 1, 0ms));         // core 0 has arrived now
    }
    // reuse: OSInitRendezvous clears the flags of the previous round
    {
        Rendezvous r;
        r.init();
        auto worker = wait_async(r, 0, 3);
        assert(r.wait(1, 1) && passes(worker));
        assert(r.wait(1, 1, 0ms));         // without a reset the old flags still satisfy it
        r.init();
        auto main_wait = wait_async(r, 1, 1);
        assert(blocked(main_wait));        // a new round waits again
        auto worker2 = wait_async(r, 0, 3);
        assert(passes(worker2) && passes(main_wait));
    }
    // the start-up pair, many rounds with racing order: both always pass, the main thread never
    // passes before the worker has arrived
    for (int i = 0; i < 200; i++) {
        Rendezvous r;
        r.init();
        std::atomic<bool> worker_arrived{false};
        auto worker = std::async(std::launch::async, [&] {
            if (i & 1) std::this_thread::yield();
            {
                std::lock_guard<std::mutex> lk(r.m);
                worker_arrived = true;  // published under the lock, before the arrival
            }
            return r.wait(0, 3);
        });
        assert(r.wait(1, 1));
        assert(worker_arrived);
        assert(passes(worker));
    }
    puts("rendezvous: ok");
    return 0;
}
