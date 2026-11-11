// SPDX-License-Identifier: MPL-2.0
// Cafe OS message queues: the actual threads.cpp OSInitMessageQueue / OSSendMessage / OSReceiveMessage, built four
// ways by CMakeLists.txt (msgq_test_{normal,fast}_{off,on}: WWHD_DECOMP_FAST and WWHD_MSGQ_FAST each off or on).
//  1. semantics: one fixed sequence of NOBLOCK sends, jams (OS_MESSAGE_FLAG_HIGH_PRIORITY) and receives on a 3-slot
//     queue, every result and every received message compared with the expected list (the same in all four builds):
//     FIFO order, jam to the front, full queue -> 0, empty queue -> 0, a jam on a full queue -> 0;
//  2. blocking: a BLOCK send on a full queue waits until a receive makes room, a BLOCK receive on an empty queue waits
//     until a send, each from a guest thread (t_self set, as the game's threads) and from a plain host thread;
//  3. fast builds: a receiver allowed to spin (update_ubo-like) gets a message sent during its spin, during its nap
//     (WWHD_MSGQ_FAST) and after it fell asleep; the right message every time;
//  4. stress: 4 producers (BLOCK and NOBLOCK-with-retry sends) and 3 consumers (guest threads on cores 0-2, allowed to
//     spin) on an 8-slot queue, 4 x 40000 messages: every message received exactly once, each producer's messages in
//     order at every consumer, no thread stuck (watchdog).
#undef NDEBUG  // the checks call the functions under test
#include "runtime.h"
#include <string>
#include <cstdint>
#include <utility>
#include <cstring>
#include <sys/mman.h>
#undef HLE
// the imports as plain functions (the test has no dispatch registration, profiler or exception boundary)
#define HLE(lib, name)                                   \
    static void hle_impl_##lib##_##name(Cpu* c);         \
    extern "C" void imp_##lib##_##name(Cpu* c) { hle_impl_##lib##_##name(c); } \
    static void hle_impl_##lib##_##name(Cpu* c)
#include "../src/threads.cpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <random>
void log_msg(const char*, ...) {}
namespace mem { std::string read_cstr(uint32_t) { return "test"; } }
namespace rprof { uint64_t thread_cpu_ns() { return 0; } }

#ifdef WWHD_DECOMP_FAST
static constexpr bool kFast = true;
#else
static constexpr bool kFast = false;
#endif
#ifdef WWHD_MSGQ_FAST
static constexpr bool kOn = true;
#else
static constexpr bool kOn = false;
#endif

static constexpr uint32_t kMem = 0x10000000u, kMemSize = 0x100000u;  // guest memory the test maps
static constexpr uint32_t kBlock = 1, kJam = 2;                       // OS_MESSAGE_FLAG_BLOCKING, _HIGH_PRIORITY

static void map_guest_memory() {
    void* want = ppc_ptr(kMem);
    void* p = mmap(want, kMemSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p != want) { std::printf("msgq_test: cannot map guest memory at %p\n", want); std::exit(2); }
}
static void init_queue(uint32_t q, uint32_t slots) {
    Cpu c{};
    c.r[3] = q; c.r[4] = q + 0x40; c.r[5] = slots;
    imp_coreinit_OSInitMessageQueue(&c);
}
// a 16-byte message at buffer B: words v, v+1, v+2, v+3
static uint32_t send(Cpu& c, uint32_t q, uint32_t b, uint32_t v, uint32_t flags) {
    for (uint32_t i = 0; i < 4; i++) st32(b + 4 * i, v + i);
    c.r[3] = q; c.r[4] = b; c.r[5] = flags;
    imp_coreinit_OSSendMessage(&c);
    return c.r[3];
}
// result, and the message's first word in V (all four words checked)
static uint32_t recv(Cpu& c, uint32_t q, uint32_t b, uint32_t flags, uint32_t* v) {
    for (uint32_t i = 0; i < 4; i++) st32(b + 4 * i, 0xDEADBEEF);
    c.r[3] = q; c.r[4] = b; c.r[5] = flags;
    imp_coreinit_OSReceiveMessage(&c);
    const uint32_t r = c.r[3];
    if (r) {
        *v = ld32(b);
        for (uint32_t i = 1; i < 4; i++) assert(ld32(b + 4 * i) == *v + i);
    } else {
        for (uint32_t i = 0; i < 4; i++) assert(ld32(b + 4 * i) == 0xDEADBEEF);  // nothing written
    }
    return r;
}

// a guest thread for the calling host thread (as threads.cpp's thread_main sets it up), bound to CORE
struct Guest {
    HostThread t;
    Guest(uint32_t core, int prio, bool may_spin) {
        t.core = t.cpu.core = core;
        t.prio = prio;
        t.cpu.thread = &t;
#ifdef WWHD_DECOMP_FAST
        t.spin_ok = may_spin ? 1 : 0;
#else
        (void)may_spin;
#endif
        t_self = &t;
        t_cpu = &t.cpu;
    }
    ~Guest() {
        threads::release_core();
        t_self = nullptr;
        t_cpu = nullptr;
    }
};

static void semantics() {
    const uint32_t q = kMem + 0x1000, b = kMem + 0x2000;
    init_queue(q, 3);
    assert(ld32(q) == 0x6D536751);
    Cpu c{};
    std::vector<std::string> got;
    auto rec = [&](const char* what, uint32_t r, uint32_t v = 0) {
        char buf[96];
        if (r && v) std::snprintf(buf, sizeof buf, "%s -> %u (%u)", what, r, v);
        else std::snprintf(buf, sizeof buf, "%s -> %u", what, r);
        got.push_back(buf);
    };
    uint32_t v = 0;
    rec("recv empty", recv(c, q, b, 0, &v));
    rec("send 100", send(c, q, b, 100, 0));
    rec("send 200", send(c, q, b, 200, 0));
    rec("jam 300", send(c, q, b, 300, kJam));
    rec("send 400 full", send(c, q, b, 400, 0));
    rec("jam 500 full", send(c, q, b, 500, kJam));
    v = 0; rec("recv", recv(c, q, b, 0, &v), v);
    rec("jam 600", send(c, q, b, 600, kJam));
    for (int i = 0; i < 3; i++) { v = 0; rec("recv", recv(c, q, b, 0, &v), v); }
    rec("recv empty", recv(c, q, b, 0, &v));
    rec("send 700", send(c, q, b, 700, kBlock));
    v = 0; rec("recv", recv(c, q, b, kBlock, &v), v);
    init_queue(q, 2);  // re-initialised in place: empty again
    rec("send 800", send(c, q, b, 800, 0));
    init_queue(q, 2);
    rec("recv after init", recv(c, q, b, 0, &v));
    static const char* const expect[] = {
        "recv empty -> 0", "send 100 -> 1", "send 200 -> 1", "jam 300 -> 1", "send 400 full -> 0", "jam 500 full -> 0",
        "recv -> 1 (300)", "jam 600 -> 1", "recv -> 1 (600)", "recv -> 1 (100)", "recv -> 1 (200)", "recv empty -> 0",
        "send 700 -> 1", "recv -> 1 (700)", "send 800 -> 1", "recv after init -> 0"};
    assert(got.size() == sizeof expect / sizeof expect[0]);
    for (size_t i = 0; i < got.size(); i++) {
        if (got[i] != expect[i]) {
            std::printf("msgq_test: step %zu: %s, expected %s\n", i, got[i].c_str(), expect[i]);
            std::exit(1);
        }
    }
    std::printf("msgq_test: semantics, %zu steps as expected (FIFO, jam to the front, full/empty returns, re-init)\n", got.size());
}

// 2. BLOCK send on a full queue, BLOCK receive on an empty queue; the waiter a guest thread or not
static void blocking(bool guest) {
    const uint32_t q = kMem + 0x3000 + (guest ? 0x100 : 0), b = kMem + 0x4000;
    init_queue(q, 1);
    Cpu c{};
    assert(send(c, q, b, 10, 0) == 1);
    std::atomic<int> stage{0};
    std::thread s([&] {
        std::unique_ptr<Guest> g;
        if (guest) g = std::make_unique<Guest>(1, 16, false);
        Cpu c2{};
        stage = 1;
        const uint32_t r = send(c2, q, b + 0x100, 20, kBlock);  // full: waits for room
        assert(r == 1);
        stage = 2;
    });
    while (stage < 1) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(stage == 1);
    uint32_t v = 0;
    assert(recv(c, q, b + 0x200, 0, &v) == 1 && v == 10);
    s.join();
    assert(stage == 2);
    assert(recv(c, q, b + 0x200, 0, &v) == 1 && v == 20);
    std::thread r([&] {
        std::unique_ptr<Guest> g;
        if (guest) g = std::make_unique<Guest>(2, 16, false);
        Cpu c2{};
        uint32_t w = 0;
        stage = 3;
        assert(recv(c2, q, b + 0x300, kBlock, &w) == 1 && w == 30);  // empty: waits for a message
        stage = 4;
    });
    while (stage < 3) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(stage == 3);
    assert(send(c, q, b, 30, 0) == 1);
    r.join();
    assert(stage == 4);
    std::printf("msgq_test: blocking send on a full queue and receive on an empty queue (%s waiter)\n", guest ? "guest" : "host");
}

// 3. a receiver allowed to spin: a message during its spin, during its nap, after it sleeps
static void spin_nap_sleep() {
    const uint32_t q = kMem + 0x5000, b = kMem + 0x6000;
    init_queue(q, 4);
    const unsigned delays_us[] = {5, 400, 1000, 8000, 20000, 0, 3};
    std::atomic<int> ready{0};
    std::thread r([&] {
        Guest g(2, 16, true);
        Cpu& c2 = g.t.cpu;
        for (unsigned i = 0; i < sizeof delays_us / sizeof delays_us[0]; i++) {
            uint32_t v = 0;
            ready = (int)i + 1;
            assert(recv(c2, q, b + 0x100, kBlock, &v) == 1);
            assert(v == 1000 * (i + 1));
        }
    });
    Cpu c{};
    for (unsigned i = 0; i < sizeof delays_us / sizeof delays_us[0]; i++) {
        while (ready < (int)i + 1) std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::microseconds(delays_us[i]));
        assert(send(c, q, b, 1000 * (i + 1), 0) == 1);
    }
    r.join();
    std::printf("msgq_test: receiver allowed to spin: messages after 0-20 ms delays received in order\n");
}

// 4. stress
static void stress() {
    const uint32_t q = kMem + 0x8000;
    init_queue(q, 8);
    constexpr int kProd = 4, kCons = 3;
    constexpr uint32_t kPer = 40000;
    std::atomic<uint32_t> total{0};
    std::vector<std::vector<uint32_t>> seen(kProd, std::vector<uint32_t>(kPer, 0));
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> progress{0};
    std::vector<std::thread> ts;
    for (int p = 0; p < kProd; p++)
        ts.emplace_back([&, p] {
            Cpu c{};
            const uint32_t b = kMem + 0x10000 + 0x100 * p;
            std::mt19937 rng(p + 7);
            for (uint32_t i = 0; i < kPer; i++) {
                const uint32_t v = (uint32_t)(p + 1) << 24 | i;  // 4 words v..v+3, so i < 2^24 - 3
                if (rng() & 1) {
                    assert(send(c, q, b, v, kBlock) == 1);
                } else {
                    while (send(c, q, b, v, 0) == 0) std::this_thread::yield();  // full: retry (as enqueue's caller could)
                }
                if ((rng() & 1023) == 0) std::this_thread::sleep_for(std::chrono::microseconds(rng() % 3000));  // gaps
                progress++;
            }
        });
    std::vector<std::vector<uint32_t>> last(kCons, std::vector<uint32_t>(kProd, 0));
    for (int k = 0; k < kCons; k++)
        ts.emplace_back([&, k] {
            Guest g((uint32_t)k, 16, true);
            Cpu& c = g.t.cpu;
            const uint32_t b = kMem + 0x20000 + 0x100 * k;
            for (;;) {
                uint32_t v = 0;
                assert(recv(c, q, b, kBlock, &v) == 1);
                if (v == 0xFFFFFF00u) break;  // end marker
                const uint32_t p = (v >> 24) - 1, i = v & 0xFFFFFF;
                assert(p < (uint32_t)kProd && i < kPer);
                assert(!seen[p][i]);
                seen[p][i] = 1;
                assert(last[k][p] == 0 || i + 1 > last[k][p]);  // each producer's order at this consumer
                last[k][p] = i + 1;
                total++;
                progress++;
            }
        });
    // watchdog: progress must move every 5 s
    std::thread wd([&] {
        uint64_t before = progress;
        while (!stop) {
            for (int i = 0; i < 50 && !stop; i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (stop) break;
            if (progress == before) { std::printf("msgq_test: stress stuck at %u received\n", total.load()); std::_Exit(1); }
            before = progress;
        }
    });
    for (int p = 0; p < kProd; p++) ts[p].join();
    Cpu c{};
    for (int k = 0; k < kCons; k++) assert(send(c, q, kMem + 0x30000, 0xFFFFFF00u, kBlock) == 1);
    for (int k = 0; k < kCons; k++) ts[kProd + k].join();
    stop = true;
    wd.join();
    assert(total == kProd * kPer);
    for (int p = 0; p < kProd; p++)
        for (uint32_t i = 0; i < kPer; i++) assert(seen[p][i]);
    std::printf("msgq_test: stress, %d producers x %u messages, %d consumers: each received once, in order per producer\n",
                kProd, kPer, kCons);
}

int main() {
#ifdef WWHD_MSGQ_FAST
    setenv("WWHD_MSGQ_STATS", "1", 1);  // (read at first use)
#endif
    std::printf("msgq_test: %s build, WWHD_MSGQ_FAST %s\n", kFast ? "decomp fast" : "normal", kOn ? "on" : "off");
    map_guest_memory();
    semantics();
    blocking(false);
    blocking(true);
    if (kFast) spin_nap_sleep();
    stress();
#ifdef WWHD_MSGQ_FAST
    uint64_t s[MS_N];
    for (int i = 0; i < MS_N; i++) s[i] = g_msgq_stat[i].load();
    std::printf("msgq_test: counts: sends %llu (waking a sleeper %llu); blocking receives on an empty queue %llu: spin %llu, "
                "nap %llu, slept %llu\n", (unsigned long long)s[MS_SENDS], (unsigned long long)s[MS_WAKES],
                (unsigned long long)s[MS_RECV_BLOCK], (unsigned long long)s[MS_RECV_SPIN], (unsigned long long)s[MS_RECV_NAP],
                (unsigned long long)s[MS_RECV_SLEEP]);
    assert(s[MS_WAKES] < s[MS_SENDS]);  // the gate: not every send notifies
    if (kFast) assert(s[MS_RECV_SPIN] > 0 && s[MS_RECV_NAP] > 0 && s[MS_RECV_SLEEP] > 0);  // all three receive paths ran
    // the lookup cache answers with the table's objects
    for (auto& [a, p] : sorted<HQueue>(g_queues)) assert(g_queues.cache.find(a) == p && g_queues.get(a) == p);
#endif
    std::printf("msgq_test: PASS\n");
    return 0;
}
