// tp-cache churn test (run.sh), library half (dlopened like libmain.so): the block, the table, the miss path as glue.cpp has them
#include "gabi_tp_cache.h"
#include <csignal>
struct Block { long v[8]; };
thread_local constinit Block tls_block{};
extern "C" __attribute__((visibility("protected"))) GabiTpSlot g_tab[GABI_TP_SLOTS] = {};
namespace {
thread_local bool tp_dead = false;
struct CountGuard : GabiTpGuard { ~CountGuard(); };
thread_local CountGuard tp_guard;
std::atomic<long> n_armed{0}, n_cleared{0}, n_dtor{0};
void fork_child() { gabi_tp_fork_child(g_tab); }
}
CountGuard::~CountGuard() { n_dtor++; if (slot) n_cleared++; }
[[gnu::noinline]] static Block* miss() {
    static int once = (pthread_atfork(nullptr, nullptr, fork_child), 0); (void)once;
    Block* p = &tls_block;
    bool had = tp_guard.slot; if (p->v[0]) gabi_tp_claim(g_tab, gabi_tp_key(), p, tp_guard, tp_dead);
    if (!had && tp_guard.slot) n_armed++;
    return p;
}
// returns 1 on a hit, 0 on a miss; -1 when the hit's block is not this thread's own (stale)
extern "C" __attribute__((visibility("default"))) int tpc_check() {
    void* b = gabi_tp_find(g_tab, gabi_tp_key());
    if (!b) { miss(); return 0; }
    if (b == (void*)&tls_block) return 1;
    static std::atomic<int> shown{0};
    if (shown++ < 6) __builtin_printf("STALE key %lx slot %u b %p own %p guard.slot %p dead %d\n", (unsigned long)gabi_tp_key(),
        gabi_tp_index(gabi_tp_key()), b, (void*)&tls_block, (void*)tp_guard.slot, (int)tp_dead);
    return -1;
}
extern "C" __attribute__((visibility("default"))) uintptr_t tpc_key() { return gabi_tp_key(); }
extern "C" __attribute__((visibility("default"))) int tpc_used() {
    int n = 0; for (auto& s : g_tab) n += s.key.load() > GABI_TP_BUSY; return n;
}
extern "C" __attribute__((visibility("default"))) void tpc_counts() { __builtin_printf("armed %ld cleared %ld dtors %ld\n", n_armed.load(), n_cleared.load(), n_dtor.load()); }
extern "C" __attribute__((visibility("default"))) void tpc_enter() { tls_block.v[0] = 1; } /* as EntryScope sets cpu */
