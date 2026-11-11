// SPDX-License-Identifier: MPL-2.0
// Decomp build fast mode, WWHD_DECOMP_FAST_MUTEX: the actual threads.cpp OSMutex code with the lock-free address
// cache (addr_cache.h) and the imports' direct host entries (gabi.h GABI_FAST_MUTEX).
//  1. direct entries == the imports: the same sequence of lock / trylock / unlock calls (recursion, a second thread
//     refused and blocked, unlock order) through each import (argument in r3, as ppc_host_call runs it) and through
//     its direct entry gives the same results, r3 and host mutex state (owner, count) after every step;
//  2. the cache never answers with another address's object: many threads look up overlapping address sets while
//     new mutexes are created, including more addresses than the cache holds and colliding probe windows; every answer
//     is the table's own object for that address;
//  3. fast_mutex_entries pairs each import with its direct entry.
#define WWHD_DECOMP_FAST 1
#define WWHD_DECOMP_FAST_MUTEX 1
#include "runtime.h"
#include <string>
#include <cstdint>
#include <utility>
#include <cstring>
#undef HLE
// the imports as plain functions (the test has no dispatch registration, profiler or exception boundary)
#define HLE(lib, name)                                   \
    static void hle_impl_##lib##_##name(Cpu* c);         \
    extern "C" void imp_##lib##_##name(Cpu* c) { hle_impl_##lib##_##name(c); } \
    static void hle_impl_##lib##_##name(Cpu* c)
#include "../src/threads.cpp"
#include <cassert>
#include <cstdio>
#include <future>
#include <random>
void log_msg(const char*, ...) {}
namespace mem { std::string read_cstr(uint32_t) { return "test"; } }
namespace rprof { uint64_t thread_cpu_ns() { return 0; } }

using Direct = uint32_t (*)(Cpu*, uint32_t);
static PpcFunc g_imp[3];
static Direct g_dir[3];
static unsigned g_n = 0;

struct State { const void* owner; int count; };
static State state(uint32_t a) {
    HMutex* m = g_mutexes.get(a);
    std::lock_guard<std::mutex> l(m->m);
    return {m->owner, m->count};
}
// one call: K 0 lock, 1 trylock, 2 unlock; DIRECT: through the direct entry; returns r3 afterwards
static uint32_t op(Cpu& c, int k, uint32_t a, bool direct) {
    if (direct) {
        const uint32_t v = g_dir[k](&c, a);
        assert(v == c.r[3]);
        return v;
    }
    c.r[3] = a;  // ArgPack
    g_imp[k](&c);
    return c.r[3];
}

static void sequence(bool direct, uint32_t base, std::vector<std::string>& log) {
    Cpu c1{}, c2{};
    const uint32_t a = base, b = base + 0x40;
    auto rec = [&](const char* what, uint32_t r3) {
        const State sa = state(a), sb = state(b);
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s r3=%08X a:%s/%d b:%s/%d", what, r3 - (r3 >= base ? base : 0),
                      sa.owner ? "held" : "free", sa.count, sb.owner ? "held" : "free", sb.count);
        log.push_back(buf);
    };
    rec("lock a", op(c1, 0, a, direct));
    rec("lock a again (recursive)", op(c1, 0, a, direct));
    rec("trylock a again", op(c1, 1, a, direct));
    rec("trylock b", op(c1, 1, b, direct));
    // another thread: trylock refused, lock blocks until the owner fully unlocks
    {
        auto f = std::async(std::launch::async, [&] { return op(c2, 1, a, direct); });
        rec("other thread trylock a", f.get());
    }
    std::promise<void> started;
    std::atomic<bool> got{false};
    std::thread other([&] {
        started.set_value();
        op(c2, 0, a, direct);
        got = true;
        const State s = state(a);
        assert(s.count == 1 && s.owner == (const void*)&t_cpu);
        op(c2, 2, a, direct);
    });
    started.get_future().wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(!got);
    rec("unlock a (3 -> 2)", op(c1, 2, a, direct));
    rec("unlock a (2 -> 1)", op(c1, 2, a, direct));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(!got);
    op(c1, 2, a, direct);  // 1 -> 0: the other thread gets it
    other.join();
    assert(got);
    rec("after the other thread's lock + unlock", 0);
    rec("unlock b", op(c1, 2, b, direct));
    rec("trylock a", op(c1, 1, a, direct));
    rec("unlock a", op(c1, 2, a, direct));
}

int main() {
    threads::fast_mutex_entries([](PpcFunc imp, uint32_t (*direct)(Cpu*, uint32_t)) {
        g_imp[g_n] = imp;
        g_dir[g_n] = direct;
        g_n++;
    });
    // 3. the pairs, in lock / trylock / unlock order
    assert(g_n == 3);
    assert(g_imp[0] == imp_coreinit_OSLockMutex && g_imp[1] == imp_coreinit_OSTryLockMutex && g_imp[2] == imp_coreinit_OSUnlockMutex);
    for (int k = 0; k < 3; k++) assert(g_dir[k] && (void*)g_dir[k] != (void*)g_imp[k]);
    std::printf("fast_mutex_test: 3 direct entries paired with their imports\n");

    // 1. the same sequence through the imports and through the direct entries (fresh mutexes each)
    std::vector<std::string> li, ld;
    sequence(false, 0x10100000u, li);
    sequence(true, 0x10200000u, ld);
    assert(li.size() == ld.size());
    for (size_t i = 0; i < li.size(); i++) {
        if (li[i] != ld[i]) {
            std::printf("step %zu differs:\n  import: %s\n  direct: %s\n", i, li[i].c_str(), ld[i].c_str());
            return 1;
        }
    }
    for (auto& s : li) std::printf("  %s\n", s.c_str());
    std::printf("fast_mutex_test: %zu steps, imports and direct entries agree (results, r3, owner, count)\n", li.size());

    // 2. the cache under concurrent creation: 6000 addresses (more than its 4096 entries), word-spaced and
    // 64 KB-spaced (colliding home slots), looked up by 8 threads in different orders while being created
    std::vector<uint32_t> addrs;
    for (uint32_t i = 0; i < 3000; i++) addrs.push_back(0x10400000u + 4 * i);
    for (uint32_t i = 0; i < 3000; i++) addrs.push_back(0x10800000u + 0x10000 * i);
    std::vector<std::thread> ts;
    std::vector<std::vector<HMutex*>> seen(8, std::vector<HMutex*>(addrs.size()));
    std::atomic<int> go{0};
    for (int t = 0; t < 8; t++)
        ts.emplace_back([&, t] {
            std::vector<size_t> order(addrs.size());
            for (size_t i = 0; i < order.size(); i++) order[i] = i;
            std::mt19937 rng(t + 1);
            std::shuffle(order.begin(), order.end(), rng);
            go++;
            while (go < 8) {}
            for (int round = 0; round < 3; round++)
                for (size_t i : order) {
                    HMutex* p = g_mutexes.get(addrs[i]);
                    if (round == 0) seen[t][i] = p;
                    else assert(seen[t][i] == p);
                }
        });
    for (auto& t : ts) t.join();
    unsigned cached = 0;
    for (size_t i = 0; i < addrs.size(); i++) {
        HMutex* canon;
        {
            std::lock_guard<std::mutex> lk(g_mutexes.m);
            canon = g_mutexes.map.at(addrs[i]);
        }
        for (int t = 0; t < 8; t++) assert(seen[t][i] == canon);
        if (HMutex* c = g_mutexes.cache.find(addrs[i])) {
            assert(c == canon);
            cached++;
        }
    }
    assert(!g_mutexes.cache.find(0));
    std::printf("fast_mutex_test: %zu addresses x 8 threads x 3 rounds, every answer the table's object (%u served "
                "from the cache, the rest through the locked map)\n", addrs.size(), cached);
    std::printf("fast_mutex_test: PASS\n");
    return 0;
}
