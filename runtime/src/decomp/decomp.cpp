// Decomp build: the environment the decompiled source is written against (gabi.h in the decompilation,
// decomp/tools/verify/include) on the running game, and the per-function slots (decomp.h).
//
// gabi.h's environment functions gmem_* are provided by the verification harness there and by this
// file here:
//   - gmem_ld/st: guest memory (big-endian, PPC_MEM_BASE window), as the recompiled code accesses it;
//   - gmem_stack_fresh: a gabi::Local slot starts zeroed (the harness's scratch stack default);
//   - gmem_call: a guest call through the dispatch table (ppc_dispatch), so a call reaches the
//     recompiled function, a runtime hook, an HLE import, or another decompiled function.
// gabi's do_call has already placed the arguments (r3..r10, f1..f8, stack words at SP+8 of a new
// outgoing frame) and reserved the 16-byte linkage area, exactly as in the harness.
//
// Debug switches (none is needed to play; all off by default, and without them the slots cost one
// predictable branch per call):
//   WWHD_DECOMP=all | none | unit,unit,...        game units (or any verify unit of one) that run their
//                                                 decompiled functions (default all); "none" runs the
//                                                 recompiled code everywhere (same executable)
//   WWHD_DECOMP_FUNCS=ADDR,... | @file            only these functions (hex addresses) run decompiled
//   WWHD_DECOMP_EXCLUDE=ADDR|unit,... | @file     these stay recompiled (bisecting)
//   WWHD_DECOMP_STATS=path                        per-function call counts, rewritten every 2 s and at exit
//   WWHD_DECOMP_GUARD=log|restore                 report decompiled functions that write into their
//                                                 caller's frame (restore: and undo it)
//   WWHD_DECOMP_FRAMES=0                          no native frames (frames.cpp)
//   WWHD_DECOMP_AUTOFRAME_FAST=0                  automatic frames looked up per entry (gmem_frame_info), not by slot
//   WWHD_DECOMP_AUTOFRAME_CHECK=1                 compare the per-slot frame table with gmem_frame_info (log)
//   WWHD_DECOMP_DIRECT=0 | all                    0: no direct calls between decompiled functions (decomp.h);
//                                                 all: frame-exact functions get them too (measurement)
//   WWHD_DECOMP_FRAME_EXACT=ADDR,... | @file      more frame-exact functions (no direct calls into or from them)
//   WWHD_DECOMP_NATIVE=0                          no native calls between decompiled functions (gabi.h form N)
//   WWHD_DECOMP_NATIVE_FUNCS / _EXCLUDE=ADDR,...|@file  only / not these callees natively (bisecting)
//   WWHD_DECOMP_NATIVE_EXACT=0                    native calls keep the guest registers (ArgPack / store_ret) for
//                                                 register-exact functions too, and no real native calls between units
//   WWHD_DECOMP_NATIVE_EXACT=n | l                bisecting: n only inside units (no native entries between units),
//                                                 l only the native entries (registers still packed / stored)
//   WWHD_DECOMP_POISON=1 | args                   test aid: a register-exact native entry poisons the argument
//                                                 registers, its exit the result registers (args: entry only)
//   WWHD_DECOMP_CALL_LR_CHECK=1                   compare call_lr's start-up table with call_lr_scan (log)
//   WWHD_DECOMP_SITES=0                           guest calls ask call_lr and the direct table per call (decomp.h call_to)
//   WWHD_DECOMP_SITES_CHECK=1                     compare the call-site tables with call_lr + direct_slot (log)
#include "decomp/decomp.h"
#include "decomp/call_lr.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <map>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "../runtime.h"
#if defined(WWHD_DECOMP_FAST_COUNT) || defined(WWHD_DECOMP_FAST_CHECKCOUNT)
#include "../interp.h"
#endif
#include "recomp_table.h"

extern "C" {
// recomp.py --decomp-hooks (generated table.c): listed functions the recompiler kept as they are (not a
// function entry in this game, or containing an instruction-level hook); their slots stay off
extern const uint32_t g_recomp_decomp_kept[];
extern const unsigned g_recomp_decomp_kept_count;
// every call site of an imported function -> its import slot (sorted by site)
extern const uint32_t g_recomp_decomp_import_calls[][2];
extern const unsigned g_recomp_decomp_import_call_count;
// guest mods (runtime/src/mods/guest_mods.cpp): per-function flags, null without --mod-hooks code
extern uint8_t* g_mod_hook_flags;

uint8_t gmem_ld8(uint32_t ea) { return ld8(ea); }
uint16_t gmem_ld16(uint32_t ea) { return ld16(ea); }
uint32_t gmem_ld32(uint32_t ea) { return ld32(ea); }
uint64_t gmem_ld64(uint32_t ea) { return ld64(ea); }
void gmem_st8(uint32_t ea, uint8_t v) { st8(ea, v); }
void gmem_st16(uint32_t ea, uint16_t v) { st16(ea, v); }
void gmem_st32(uint32_t ea, uint32_t v) { st32(ea, v); }
void gmem_st64(uint32_t ea, uint64_t v) { st64(ea, v); }
void gmem_stf32(uint32_t ea, uint32_t v) { st32(ea, v); }
void gmem_stf64(uint32_t ea, uint64_t v) { st64(ea, v); }
void gmem_stack_fresh(uint32_t ea, uint32_t n, uint32_t) { memset(ppc_ptr(ea), 0, n); }
// the harness's stack guard hooks (gabi.h do_call); the game has WWHD_DECOMP_GUARD instead
void gmem_stack_args(int) {}
void gmem_call_top(uint32_t) {}
#ifdef WWHD_DECOMP_FAST
extern "C" unsigned hostrt_fast_entries(void (*add)(uint32_t addr, uint32_t sig, void* fn));  // hostrt_fast.cpp
extern "C" PpcFunc hostrt_fast_direct(uint32_t addr);  // hostrt_fast.cpp
// fast mode diagnostics (WWHD_DECOMP_FAST_STATS=PATH): every guest call that still leaves a decompiled body (gmem_call,
// decomp_lcall), counted by (caller slot, target, kind), written to PATH every 2 s. Off: one predictable branch.
static bool g_fstats = false;
static std::mutex g_fstats_mu;
static std::map<std::tuple<uint32_t, uint32_t, int>, uint64_t>* g_fstats_map = nullptr;
static void fstat(uint32_t f, uint32_t target, int kind) {
    std::lock_guard<std::mutex> l(g_fstats_mu);
    (*g_fstats_map)[{f, target, kind}]++;
}
static void fstats_write(const std::string& path);
#define WWHD_FSTAT(f, t, k) do { if (__builtin_expect(g_fstats, 0)) fstat(f, t, k); } while (0)
#else
#define WWHD_FSTAT(f, t, k) do { } while (0)
#endif
#ifdef WWHD_DECOMP_FAST_LEAF_CHECK
// GABI_LEAF_CHECK: a leaf body runs with Cpu::decomp_running poisoned to 0xFFFF; any guest call or LR answer from it traps
#define WWHD_LEAF_TRAP(f) do { if (__builtin_expect((f) == 0xFFFF, 0)) { LOG("[decomp] leaf check: a call from a leaf body"); __builtin_trap(); } } while (0)
#else
#define WWHD_LEAF_TRAP(f) do { } while (0)
#endif
void gmem_call(Cpu* c, uint32_t target, int kind, int, int) {
    WWHD_LEAF_TRAP(c->decomp_running);
    WWHD_FSTAT(c->decomp_running, target, kind);
#ifdef WWHD_DECOMP_SITE_LR
    // kind | 4 (game build, gabi.h site_take): LR already holds the return address of the call site (site-exact LR)
    const bool site = kind & 4;
    kind &= 3;
#endif
    // kind 1: through a function pointer (bctrl: CTR holds the target)
    if (kind == 1) c->ctr = target;
    // LR as the original's bl/bctrl leaves it: a runtime hook, an HLE import or a mod that tells its
    // callers apart by LR (story_mail's text-length check, true60's call-site checks) sees the same
    // caller as in the normal build (before, LR still held the return address into the decompiled
    // function's own caller)
#ifdef WWHD_DECOMP_FAST
    // fast mode: an import stub (HLE: the core's slot table 0xC0000000..0xC0040000) needs no call_lr answer; LR is
    // read there only by log lines, debug tools and the save-state park check, which fast mode does not keep
    if (target - 0xC0000000u < 0x40000u) {
        c->pc = target;
        ppc_dispatch(c);
        return;
    }
    // a host runtime root (hostrt_fast.cpp): its f_X straight away (the two address ranges of the roots)
    if (target - 0x028EA000u < 0xC000u || target - 0x0286C000u < 0x2000u) {
        if (const PpcFunc hf = hostrt_fast_direct(target)) {
            c->pc = target;
            hf(c);
            return;
        }
    }
#endif
    const uint32_t f = c->decomp_running;
    // a decompiled function that runs decompiled: straight into its unit's entry, as decomp::call does
    // (decomp.h direct_slot; the guest state is the same as through the dispatch table); not from a
    // frame-exact function. Both answers by call site (decomp.h call_to)
    decomp::Slot* s;
    if (f) {
        const decomp::CallTo t = decomp::call_to(f - 1, target, kind, c->lr);
#ifdef WWHD_DECOMP_SITE_LR
        if (!site) c->lr = t.lr;
#else
        c->lr = t.lr;
#endif
        s = t.s;
    } else {
        s = decomp::direct_slot(target);
    }
    c->pc = target;
    if (s) {
        c->decomp_running = uint16_t(s - decomp::g_slots + 1);
        s->cand(c);
        c->decomp_running = uint16_t(f);
        return;
    }
    ppc_dispatch(c);
}

// native calls (gabi.h NativeScope): what gmem_call does to the guest state for a direct guest call from the running
// decompiled function to `target` (pc, and LR as the original's bl leaves it), so a native entry leaves the same LR
// (the callee's guest calls whose LR call_lr cannot name, and its caller after the return, see it)
// call_lr through a one-entry cache that belongs to one call target in one unit (gabi.h: a static of lcall<ADDR> or
// of the callee's WWHD_GLUE_FUNC): {running slot + 1 : answer, 1 = LR unchanged}. One relaxed 64-bit word: threads
// that race only miss; no thread-local, no table walk while the same caller calls again.
static inline void cached_lr(Cpu* c, uint32_t f, uint32_t target, uint64_t* cache) {
    const uint64_t e = __atomic_load_n(cache, __ATOMIC_RELAXED);
    uint32_t ret;
    if (uint32_t(e >> 32) == f) {
        ret = uint32_t(e);
    } else {
        ret = decomp::call_lr_by_site(f - 1, target, 1u);  // a return address is never odd: 1 = not found
        __atomic_store_n(cache, uint64_t(f) << 32 | ret, __ATOMIC_RELAXED);
    }
    if (ret != 1u) c->lr = ret;
}
extern "C" __attribute__((visibility("default"))) uint32_t decomp_native_lr(Cpu* c, uint32_t target, uint64_t* cache) {
    c->pc = target;
    const uint32_t f = c->decomp_running;
    WWHD_LEAF_TRAP(f);
    if (f) cached_lr(c, f, target, cache);
    return c->lr;
}

// lean calls (gabi.h lcall, native-calls form L): a decompiled function's guest call to the decompiled function in
// slot `slot` (a call to another game unit). The guest state is gmem_call's for a direct call (pc, LR from call_lr,
// the running function; a frame-exact caller takes the dispatch table); the callee's gabi entry runs straight away
// (Entry::run keeps cpu, depth, LR and Activation::current itself, as decomp_run would have)
// cache null: LR already holds the return address of the call site (gabi.h site_take, site-exact LR)
extern "C" __attribute__((visibility("default"))) void decomp_lcall(Cpu* c, uint32_t slot, uint32_t addr, uint64_t* cache) {
    const uint32_t f = c->decomp_running;
    WWHD_LEAF_TRAP(f);
    WWHD_FSTAT(f, addr, 2);
#ifdef WWHD_DECOMP_SITE_LR
    if (f && cache) cached_lr(c, f, addr, cache);
#else
    if (f) cached_lr(c, f, addr, cache);
#endif
    c->pc = addr;
    if (__builtin_expect(f && decomp::g_slots[f - 1].frame_exact, 0)) {
        ppc_dispatch(c);
        return;
    }
    c->decomp_running = uint16_t(slot + 1);
    decomp::g_slots[slot].cand(c);
    c->decomp_running = uint16_t(f);
}

#ifdef WWHD_DECOMP_EDGE_LCALL
// edges (gabi.h edge_lcall, level 1 step 3; cmake WWHD_DECOMP_EDGE_LCALL): a decompiled function's guest call to a
// constant address that is no decompiled function (recompiled code, an import stub). The guest state is gmem_call's
// for kind 0: pc, LR = call_lr's answer for the running caller (call_lr_by_site, through the call site's one-word
// cache instead of call_to's lookup in the caller's site table per call), and no direct slot (the target has none:
// direct_slot answers null), so the dispatch table. WWHD_DECOMP_EDGE_CHECK=1: compare each answer with call_to's
// (log, first 20 differences).
static bool g_edge_check = false;
static std::atomic<unsigned> g_edge_bad{0};
static std::atomic<uint64_t> g_edge_n{0};
extern "C" __attribute__((visibility("default"))) void decomp_edge_call(Cpu* c, uint32_t addr, uint64_t* cache) {
    const uint32_t f = c->decomp_running;
    if (__builtin_expect(g_edge_check, 0)) {
        const uint64_t n = g_edge_n.fetch_add(1, std::memory_order_relaxed) + 1;
        if (!(n & (n - 1)) && n >= 4096) LOG("[decomp] edge check: %llu edge calls, %u differ", (unsigned long long)n, g_edge_bad.load());
        const uint32_t lr0 = c->lr;
        const decomp::CallTo want = f ? decomp::call_to(f - 1, addr, 0, lr0) : decomp::CallTo{lr0, decomp::direct_slot(addr)};
        if (f) cached_lr(c, f, addr, cache);
        if ((want.lr != c->lr || want.s) && g_edge_bad.fetch_add(1) < 20)
            LOG("[decomp] edge check: caller slot %u -> %08X: lr %08X, call_to %08X, slot %s", f, addr, c->lr, want.lr,
                want.s ? want.s->name : "-");
    } else if (f) {
        cached_lr(c, f, addr, cache);
    }
    c->pc = addr;
    ppc_dispatch(c);
}
#endif

// called by each decompiled unit's static constructors (tools/decomp_build/glue.cpp)
void decomp_register(const char* unit, uint32_t addr, const char* name, void (*fn)(Cpu*), void (*run)(Cpu*, void (*)(Cpu*))) {
    using decomp::g_slots;
    decomp::Slot* end = g_slots + decomp::kNumSlots;
    decomp::Slot* s = std::lower_bound(g_slots, end, addr, [](const decomp::Slot& a, uint32_t v) { return a.addr < v; });
    if (s == end || s->addr != addr) return;                 // excluded by gen.py
    if (strcmp(decomp::kUnits[s->unit], unit) != 0) return;  // verified in two units: gen.py chose the other
    (void)name;
    s->cand = fn;
    s->run = run;
}
}

uint8_t* g_decomp_native_ok = nullptr;
GabiNativeEntry* g_decomp_native_entry = nullptr;
#ifdef WWHD_DECOMP_FAST
const PpcFunc* g_decomp_hle_tab = nullptr;  // gabi.h: the HLE import stubs' host functions (direct_init)
#ifdef WWHD_DECOMP_FAST_MUTEX
const GabiHleDirect* g_decomp_hle_direct = nullptr;  // gabi.h GABI_FAST_MUTEX: direct entries of the OS mutex imports
namespace threads { unsigned fast_mutex_entries(void (*add)(PpcFunc imp, uint32_t (*direct)(Cpu*, uint32_t))); }
#endif
const GabiPtrEntry* g_decomp_ptr_tab = nullptr;
const GabiPtrEntry* g_decomp_call_tab = nullptr;
uint32_t g_decomp_ptr_mask = 0;
#endif
uint8_t g_decomp_exact = 0;
#ifdef WWHD_DECOMP_FAST_LOCAL
extern "C" __attribute__((visibility("default"))) uint8_t g_decomp_fast_local = 0;  // gabi.h GABI_FAST_LOCAL: 1 = the lean Local / FrameLocal form (init below)
#endif
#ifdef WWHD_DECOMP_FAST_COUNT
extern "C" __attribute__((visibility("default"))) uint64_t g_decomp_fast_count[4] = {};
#endif
#ifdef WWHD_DECOMP_FAST_STR
extern "C" __attribute__((visibility("default"))) uint8_t g_decomp_fast_str = 0;  // gabi.h GABI_FAST_STR: 1 = guest_str's per-call-site cache (init below)
#endif
#ifdef WWHD_DECOMP_FAST_A3_FN
// gabi.h GABI_A3_FN (cmake WWHD_DECOMP_FAST_A3_FN): 1 = bodies that are caller=ok only by native_rules.py --a3-fn keep their
// calls guest calls (WWHD_DECOMP_FAST_A3_FN=0, bisecting); read by every such body's native-call scope
uint8_t g_decomp_a3_off = 0;
#endif
#ifdef WWHD_DECOMP_FAST_LEAF
// gabi.h GABI_LEAF (cmake WWHD_DECOMP_FAST_LEAF): 1 = leaf bodies take the full native-call scope (WWHD_DECOMP_FAST_LEAF=0)
uint8_t g_decomp_leaf_off = 0;
#endif
#ifdef WWHD_DECOMP_FAST_NSTATS
// gabi.h GABI_NSTATS (cmake WWHD_DECOMP_FAST_NSTATS, diagnostics): native entries per slot (WWHD_DECOMP_FAST_NSTATS=PATH)
uint64_t* g_decomp_nstats = nullptr;
#endif
#ifdef WWHD_DECOMP_FAST_CHECKCOUNT
// gabi.h GABI_CHECKCOUNT (cmake WWHD_DECOMP_FAST_CHECKCOUNT, diagnostics): per slot the runs and refusals of each of
// native_enter_ok's checks (kCcKinds run counters, then kCcKinds refusal counters; WWHD_DECOMP_FAST_CHECKCOUNT=PATH)
uint64_t* g_decomp_checkcount = nullptr;
namespace {
constexpr unsigned kCcKinds = 7;  // gabi.h GABI_CC_KINDS
const char* const kCcName[kCcKinds] = {"guest", "nested", "static", "rule", "caller", "nbyte", "arg"};
// refusals by (callee slot, check, caller address, argument index): open addressing, key 0 = empty, filled lock-free
constexpr uint32_t kCcFailSize = 1u << 16;
struct CcFail {
    std::atomic<uint64_t> key{0};
    std::atomic<uint64_t> n{0};
    std::atomic<uint64_t> value{0};  // the first refused argument's value (arg), the native byte (nbyte), the flags (rule)
};
CcFail* g_cc_fail = nullptr;
std::atomic<uint64_t> g_cc_fail_lost{0};
}  // namespace
extern "C" void decomp_checkcount_fail(uint32_t slot, uint32_t kind, uint32_t caller, uint32_t arg, uint64_t value) {
    const uint64_t key = (uint64_t)caller << 32 | (uint64_t)(slot + 1) << 8 | kind << 4 | (arg & 15);
    uint32_t h = (uint32_t)((key * 0x9E3779B97F4A7C15ull) >> 48) & (kCcFailSize - 1);
    for (uint32_t i = 0; i < kCcFailSize; i++, h = (h + 1) & (kCcFailSize - 1)) {
        CcFail& e = g_cc_fail[h];
        uint64_t k = e.key.load(std::memory_order_acquire);
        if (k == 0) {
            uint64_t z = 0;
            if (e.key.compare_exchange_strong(z, key, std::memory_order_acq_rel)) {
                e.value.store(value, std::memory_order_relaxed);
                k = key;
            } else {
                k = z;
            }
        }
        if (k == key) {
            e.n.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    g_cc_fail_lost.fetch_add(1, std::memory_order_relaxed);
}
#endif
#ifdef WWHD_DECOMP_FAST_PTR_SUB
// gabi.h GABI_PTR_SUB (cmake WWHD_DECOMP_FAST_PTR_SUB): 1 = pointer calls may take a native entry that takes fewer
// registers than they pass (WWHD_DECOMP_FAST_PTR_SUB=0: equal shapes only, bisecting)
uint8_t g_decomp_ptr_sub = 1;
#endif

// the native entries the units register (gabi.h NativeReg, glue.cpp), per slot; enabled into g_decomp_native_entry by
// direct_init. Filled by static constructors, so a constant-initialised pointer allocated on first use.
namespace {
struct NativeReg {
    void* fn;
    uint32_t sig, table_sig;
};
NativeReg* g_native_reg = nullptr;
}
extern "C" void decomp_register_native(const char* unit, uint32_t addr, void* fn, uint32_t sig, uint32_t table_sig) {
    using decomp::g_slots;
    decomp::Slot* end = g_slots + decomp::kNumSlots;
    decomp::Slot* s = std::lower_bound(g_slots, end, addr, [](const decomp::Slot& a, uint32_t v) { return a.addr < v; });
    if (s == end || s->addr != addr) return;
    if (strcmp(decomp::kUnits[s->unit], unit) != 0) return;  // as decomp_register
    if (!g_native_reg) g_native_reg = new NativeReg[decomp::kNumSlots]();
    g_native_reg[s - g_slots] = NativeReg{fn, sig, table_sig};
}

namespace decomp {
void frames_init();  // frames.cpp

LrSites* g_lr_sites = nullptr;

DirectEntry* g_direct = nullptr;
CallerSites* g_callers = nullptr;

namespace {
std::vector<std::string> split_list(const char* e);  // below

// the direct calls (decomp.h direct_slot): every slot that is on and that the game-code dispatch table
// still routes to its generated entry f_X (-> hook_X -> decomp::call), once that table is sealed (no
// more writes after start-up: guest mods, runtime hooks). Without the seal, or with a debug switch
// (WWHD_DECOMP_STATS / GUARD: their counts and checks are in call_debug), every call takes the
// dispatch table. WWHD_DECOMP_DIRECT=0 turns the direct calls off (comparison runs).
void direct_init() {
    const char* e = getenv("WWHD_DECOMP_DIRECT");
    if (e && !strcmp(e, "0")) { LOG("[decomp] WWHD_DECOMP_DIRECT=0: decompiled calls go through the dispatch table"); return; }
    if (g_debug & 1) return;  // stats / guard runs: no direct calls (entry stats alone keep them)
    // frame-exact functions: entered and left through the dispatch table (measurement switch: =all)
    const bool all = e && !strcmp(e, "all");
    unsigned nfx = 0;
    if (!all) {
        std::vector<uint32_t> fx(kFrameExact, kFrameExact + kNumFrameExact);
        for (auto& w : split_list(getenv("WWHD_DECOMP_FRAME_EXACT"))) fx.push_back((uint32_t)strtoul(w.c_str(), nullptr, 16));
        for (uint32_t a : fx) {
            Slot* s = std::lower_bound(g_slots, g_slots + kNumSlots, a, [](const Slot& x, uint32_t v) { return x.addr < v; });
            if (s != g_slots + kNumSlots && s->addr == a && !s->frame_exact) { s->frame_exact = 1; nfx++; }
        }
    }
    if (!dispatch::native_text_sealed()) {
        LOG("[decomp] the dispatch table is not sealed: decompiled calls go through it");
        return;
    }
    const RecompEntry* b = g_recomp_funcs;
    const RecompEntry* end = g_recomp_funcs + g_recomp_func_count;
    DirectEntry* t = new DirectEntry[kDirectSize]();
    unsigned n = 0;
    for (unsigned i = 0; i < kNumSlots; i++) {
        Slot& s = g_slots[i];
        if (!s.on || !s.run || !s.cand || s.frame_exact || s.hooked) continue;  // hooked: only behind hook_X
        const RecompEntry* r = std::lower_bound(b, end, s.addr, [](const RecompEntry& x, uint32_t a) { return x.addr < a; });
        if (r == end || r->addr != s.addr || dispatch::lookup(s.addr) != r->fn) continue;
        uint32_t h = direct_hash(s.addr);
        while (t[h].addr) h = (h + 1) & (kDirectSize - 1);
        t[h].addr = s.addr;
        t[h].slot = &s;
        t[h].ordinal = (uint32_t)(r - b);
        s.ordinal = t[h].ordinal;
        n++;
    }
    g_direct = t;
    // native calls, per slot of the direct table: bit 1 form N (gabi.h WWHD_GLUE_FUNC; off: WWHD_DECOMP_NATIVE=0),
    // bit 2 form L (gabi.h lcall; off: WWHD_DECOMP_LEAN=0). Cleared by decomp_mod_hooked.
    const char* nat = getenv("WWHD_DECOMP_NATIVE");
    const char* lean = getenv("WWHD_DECOMP_LEAN");
    const bool lean_on = !(lean && !strcmp(lean, "0"));
    unsigned nn = 0, nl = 0;
    uint8_t* ok = new uint8_t[kNumSlots]();
    g_decomp_native_ok = ok;
    if (lean_on)
        for (uint32_t h = 0; h < kDirectSize; h++)
            if (t[h].addr && !(g_mod_hook_flags && g_mod_hook_flags[t[h].ordinal])) { ok[t[h].slot - g_slots] |= 2; nl++; }
    if (!(nat && !strcmp(nat, "0"))) {
        // bisecting: WWHD_DECOMP_NATIVE_FUNCS (only these callees) / WWHD_DECOMP_NATIVE_EXCLUDE (not these), ADDR,...|@file
        std::set<uint32_t> only, excl;
        for (auto& w : split_list(getenv("WWHD_DECOMP_NATIVE_FUNCS"))) only.insert((uint32_t)strtoul(w.c_str(), nullptr, 16));
        for (auto& w : split_list(getenv("WWHD_DECOMP_NATIVE_EXCLUDE"))) excl.insert((uint32_t)strtoul(w.c_str(), nullptr, 16));
        for (uint32_t h = 0; h < kDirectSize; h++)
            if (t[h].addr && !(g_mod_hook_flags && g_mod_hook_flags[t[h].ordinal]) && (only.empty() || only.count(t[h].addr)) &&
                !excl.count(t[h].addr)) { ok[t[h].slot - g_slots] |= 1; nn++; }
    }
    LOG("[decomp] native calls: %u decompiled functions may be entered natively%s, %u by lean calls%s", nn,
        nat && !strcmp(nat, "0") ? " (WWHD_DECOMP_NATIVE=0: none)" : "", nl, lean_on ? "" : " (WWHD_DECOMP_LEAN=0: none)");
    // real native calls (gabi.h: register-exact functions pass no guest registers; lcall's native entries). Off with
    // WWHD_DECOMP_NATIVE=0 or WWHD_DECOMP_NATIVE_EXACT=0.
    const char* ex = getenv("WWHD_DECOMP_NATIVE_EXACT");
    const char* poison = getenv("WWHD_DECOMP_POISON");
    if (!(nat && !strcmp(nat, "0")) && !(ex && !strcmp(ex, "0"))) {
        unsigned nreg = 0, nen = 0, nbad = 0, ntypes = 0;
        GabiNativeEntry* ne = new GabiNativeEntry[kNumSlots]();
        for (unsigned i = 0; i < kNumSlots && g_native_reg; i++) {
            const NativeReg& r = g_native_reg[i];
            if (!r.table_sig) continue;
            nreg++;
            if (!r.fn) { ntypes++; continue; }  // its C++ argument or result types have no native entry
            if (r.sig != r.table_sig) {
                if (nbad++ < 10) LOG("[decomp] native entry %08X %s: signature %04X, table %04X (not entered natively)",
                                     g_slots[i].addr, g_slots[i].name, r.sig, r.table_sig);
                continue;
            }
            if (!(ok[i] & 1) || (ex && !strcmp(ex, "n"))) continue;
#ifdef WWHD_DECOMP_FAST
            // WWHD_DECOMP_FAST_NX=0: no entries for callees that are not register-exact (gabi.h bit 14; bisecting)
            static const bool nx_off = [] { const char* e = getenv("WWHD_DECOMP_FAST_NX"); return e && !strcmp(e, "0"); }();
            if (nx_off && (r.sig & 0x4000u)) continue;
#endif
            ne[i] = GabiNativeEntry{r.fn, r.sig, 0};
            nen++;
        }
        g_decomp_exact = (ex && !strcmp(ex, "l") ? 0 : 1) | (poison && !strcmp(poison, "1") ? 6 : 0) |
                         (poison && !strcmp(poison, "args") ? 2 : 0);
        __atomic_store_n(&g_decomp_native_entry, ne, __ATOMIC_RELEASE);
#ifdef WWHD_DECOMP_FAST
        {  // fast mode: the same entries by address for calls through function pointers and vtables (gabi.h call_ptr)
            uint32_t size = 1024;
            while (size < 4 * (nen + 64)) size <<= 1;
            GabiPtrEntry* pt = new GabiPtrEntry[size]();
            const uint32_t mask = size - 1;
            for (unsigned i = 0; i < kNumSlots; i++) {
                if (!ne[i].sig || !g_slots[i].addr) continue;
                const uint32_t a = g_slots[i].addr;
                uint32_t h = ((a >> 2) * 0x9E3779B1u >> 7) & mask;  // gabi.h gabi_ptr_hash
                while (pt[h].addr) h = (h + 1) & mask;
                pt[h] = GabiPtrEntry{a, ne[i].sig, ne[i].fn};
            }
            // the host runtime roots' native entries (hostrt_fast.cpp): decompiled callers call the host functions
            // directly (WWHD_HOSTRT=0 / _CHECK / WWHD_HOSTRT_FAST=0: none, the guest path to hook_X as before)
            static GabiPtrEntry* s_pt;
            static uint32_t s_mask;
            s_pt = pt, s_mask = mask;
            const unsigned nhr = hostrt_fast_entries([](uint32_t a, uint32_t sig, void* fn) {
                uint32_t h = ((a >> 2) * 0x9E3779B1u >> 7) & s_mask;
                while (s_pt[h].addr && s_pt[h].addr != a) h = (h + 1) & s_mask;
                if (!s_pt[h].addr) s_pt[h] = GabiPtrEntry{a, sig, fn};  // a decompiled body of the same address wins
            });
            LOG("[decomp] fast mode: %u host runtime roots called directly (native entries)", nhr);
            // HLE import stubs: their host functions by slot, for gabi::call's direct path (WWHD_DECOMP_FAST_HLE=0: none)
            const char* fh = getenv("WWHD_DECOMP_FAST_HLE");
            if (!(fh && !strcmp(fh, "0"))) {
                PpcFunc* ht = new PpcFunc[0x40000 / 4]();
                unsigned nh = 0;
                for (uint32_t i = 0; i < 0x40000 / 4; i++)
                    if ((ht[i] = dispatch::lookup(0xC0000000u + 4 * i))) nh++;
                __atomic_store_n(&g_decomp_hle_tab, ht, __ATOMIC_RELEASE);
                LOG("[decomp] fast mode: %u HLE import stubs called directly", nh);
#ifdef WWHD_DECOMP_FAST_MUTEX
                // the OS mutex imports' direct entries, for stubs the dispatch table still routes to the import itself
                // (WWHD_DECOMP_FAST_MUTEX=0: none, the HLE path above)
                const char* fm = getenv("WWHD_DECOMP_FAST_MUTEX");
                if (!(fm && !strcmp(fm, "0"))) {
#ifndef WWHD_WRITE_WATCH
                    static GabiHleDirect* s_hd;
                    static const PpcFunc* s_ht;
                    static unsigned s_nd;
                    s_hd = new GabiHleDirect[0x40000 / 4]();
                    s_ht = ht;
                    s_nd = 0;
                    threads::fast_mutex_entries([](PpcFunc imp, uint32_t (*direct)(Cpu*, uint32_t)) {
                        for (uint32_t i = 0; i < 0x40000 / 4; i++)
                            if (s_ht[i] == imp) { s_hd[i] = direct; s_nd++; }
                    });
                    __atomic_store_n(&g_decomp_hle_direct, s_hd, __ATOMIC_RELEASE);
                    LOG("[decomp] fast mode: %u OS mutex import stubs with direct host entries", s_nd);
#endif
                } else {
                    LOG("[decomp] fast mode: WWHD_DECOMP_FAST_MUTEX=0, OS mutex imports through the HLE path");
                }
#endif
            } else {
                LOG("[decomp] fast mode: WWHD_DECOMP_FAST_HLE=0, HLE imports through gmem_call");
            }
            g_decomp_ptr_mask = mask;
            __atomic_store_n(&g_decomp_ptr_tab, pt, __ATOMIC_RELEASE);
            // gabi::call by address: the same entries; bisecting: WWHD_DECOMP_FAST_CALL=0 (none) / _CALL_ONLY /
            // _CALL_EXCLUDE=ADDR,...|@file
            const char* cf = getenv("WWHD_DECOMP_FAST_CALL");
            std::set<uint32_t> conly, cexcl;
            for (auto& w : split_list(getenv("WWHD_DECOMP_FAST_CALL_ONLY"))) conly.insert((uint32_t)strtoul(w.c_str(), nullptr, 16));
            for (auto& w : split_list(getenv("WWHD_DECOMP_FAST_CALL_EXCLUDE"))) cexcl.insert((uint32_t)strtoul(w.c_str(), nullptr, 16));
            if (cf && !strcmp(cf, "0")) {
                LOG("[decomp] fast mode: WWHD_DECOMP_FAST_CALL=0, gabi::call by address takes no native entries");
            } else if (conly.empty() && cexcl.empty()) {
                g_decomp_call_tab = pt;
            } else {
                GabiPtrEntry* ct = new GabiPtrEntry[size]();
                unsigned nc = 0;
                for (uint32_t h = 0; h < size; h++)
                    if (pt[h].addr && (conly.empty() || conly.count(pt[h].addr)) && !cexcl.count(pt[h].addr)) {
                        uint32_t k = ((pt[h].addr >> 2) * 0x9E3779B1u >> 7) & mask;
                        while (ct[k].addr) k = (k + 1) & mask;
                        ct[k] = pt[h];
                        nc++;
                    }
                g_decomp_call_tab = ct;
                LOG("[decomp] fast mode: gabi::call by address with %u native entries (bisecting)", nc);
            }
            LOG("[decomp] fast mode: %u native entries for calls through pointers (table %u)", nen, size);
        }
#endif
        LOG("[decomp] real native calls: %u register-exact functions, %u native entries on, %u signature mismatches, %u "
            "without an entry (argument types)%s%s%s", nreg, nen, nbad, ntypes, ex && *ex ? " WWHD_DECOMP_NATIVE_EXACT=" : "", ex ? ex : "", (g_decomp_exact & 4) ? " (WWHD_DECOMP_POISON=1: argument/result registers poisoned)" : (g_decomp_exact & 2) ? " (WWHD_DECOMP_POISON=args: argument registers poisoned)" : "");
    } else {
        LOG("[decomp] real native calls: off (%s)", ex && !strcmp(ex, "0") ? "WWHD_DECOMP_NATIVE_EXACT=0" : "WWHD_DECOMP_NATIVE=0");
    }
    LOG("[decomp] direct calls between decompiled functions: %u of %u; %u frame-exact functions keep the dispatch "
        "table%s", n, kNumSlots, nfx, all ? " (WWHD_DECOMP_DIRECT=all: none)" : "");
}
}  // namespace

namespace {
// call_lr's answers, per slot (call_lr.h call_lr_sites over the original's code in guest memory: the function
// from its entry to the next function of the recompiled table; none for the last one or an address that is not a
// table entry, so LR stays unchanged there). Made once at init, after the game's code is loaded and before any
// guest thread runs; read-only afterwards, shared by every thread.
std::vector<uint32_t> g_lr_keys, g_lr_rets;
std::vector<LrSites> g_lr_table;

void lr_init() {
    const RecompEntry* b = g_recomp_funcs;
    const RecompEntry* e = g_recomp_funcs + g_recomp_func_count;
    std::vector<std::pair<uint32_t, uint32_t>> first(kNumSlots);  // {offset, count} in g_lr_keys
    std::vector<uint32_t> words;
    for (unsigned i = 0; i < kNumSlots; i++) {
        const uint32_t caller = g_slots[i].addr;
        first[i] = {(uint32_t)g_lr_keys.size(), 0};
        const RecompEntry* f = std::lower_bound(b, e, caller, [](const RecompEntry& r, uint32_t a) { return r.addr < a; });
        if (f == e || f->addr != caller || f + 1 == e) continue;
        const uint32_t n = (f[1].addr - caller) / 4;
        words.resize(n);
        for (uint32_t k = 0; k < n; k++) words[k] = ld32(caller + 4 * k);
        for (auto& [key, ret] : call_lr_sites(words.data(), caller, n, g_recomp_decomp_import_calls, g_recomp_decomp_import_call_count)) {
            g_lr_keys.push_back(key);
            g_lr_rets.push_back(ret);
        }
        first[i].second = (uint32_t)g_lr_keys.size() - first[i].first;
    }
    g_lr_table.resize(kNumSlots);
    for (unsigned i = 0; i < kNumSlots; i++)
        g_lr_table[i] = LrSites{g_lr_keys.data() + first[i].first, g_lr_rets.data() + first[i].first, first[i].second};
    g_lr_sites = g_lr_table.data();
    // WWHD_DECOMP_CALL_LR_CHECK=1: compare the table with call_lr_scan for every slot and every target of a
    // bl/bla in its code (one call or several, import calls by their slot), bctrl, and an address it never calls
    if (const char* chk = getenv("WWHD_DECOMP_CALL_LR_CHECK"); chk && *chk == '1') {
        uint64_t checked = 0, bad = 0;
        const uint32_t kStale = 0xDEADBEE0u;
        for (unsigned i = 0; i < kNumSlots; i++) {
            const uint32_t caller = g_slots[i].addr;
            const RecompEntry* f = std::lower_bound(b, e, caller, [](const RecompEntry& r, uint32_t a) { return r.addr < a; });
            const bool body = f != e && f->addr == caller && f + 1 != e;
            const uint32_t n = body ? (f[1].addr - caller) / 4 : 0;
            words.resize(n);
            for (uint32_t k = 0; k < n; k++) words[k] = ld32(caller + 4 * k);
            std::vector<uint32_t> targets = {0x02000000u, caller};
            for (uint32_t k = 0; k < n; k++) {
                const uint32_t w = words[k];
                if ((w >> 26) != 18) continue;
                int32_t li = int32_t(w & 0x03FFFFFCu);
                if (li & 0x02000000) li -= 0x04000000;
                targets.push_back((w & 2) ? uint32_t(li) : caller + 4 * k + uint32_t(li));
            }
            for (uint32_t k = 0; k < g_recomp_decomp_import_call_count; k++)
                if (g_recomp_decomp_import_calls[k][0] - caller < 4 * n) targets.push_back(g_recomp_decomp_import_calls[k][1]);
            for (uint32_t t : targets)
                for (int kind = 0; kind < 2; kind++) {
                    const uint32_t want = body ? call_lr_scan(words.data(), caller, n, t, kind, kStale, g_recomp_decomp_import_calls,
                                                             g_recomp_decomp_import_call_count) : kStale;
                    const uint32_t got = call_lr(i, t, kind, kStale);
                    checked++;
                    if (got != want && bad++ < 20) LOG("[decomp] call_lr check: %08X -> %08X kind %d: table %08X, scan %08X", caller, t, kind, got, want);
                }
        }
        LOG("[decomp] call_lr check: %llu answers, %llu differ", (unsigned long long)checked, (unsigned long long)bad);
    }
}

// call sites (decomp.h call_to): per slot, the targets of the original's calls (call_lr.h call_site_targets) with
// call_lr's answer and the direct table's entry, after direct_init (the direct table and frame-exact marks are final
// then; mod hooks are still checked per call through the entry's ordinal, as direct_slot does). Read-only afterwards.
// WWHD_DECOMP_SITES=0: none (call_lr + direct_slot per call). WWHD_DECOMP_SITES_CHECK=1: compare call_to with
// call_lr + direct_slot (call_to_lookup) for every slot, every listed target and others, both kinds (log).
std::vector<CallSite> g_site_pool;
std::vector<CallerSites> g_caller_table;

void sites_init() {
    if (const char* e = getenv("WWHD_DECOMP_SITES"); e && !strcmp(e, "0")) {
        LOG("[decomp] WWHD_DECOMP_SITES=0: guest calls look up call_lr and the direct table per call");
        return;
    }
    const RecompEntry* b = g_recomp_funcs;
    const RecompEntry* e = g_recomp_funcs + g_recomp_func_count;
    std::vector<std::vector<uint32_t>> targets(kNumSlots);
    std::vector<uint32_t> words;
    size_t listed = 0;
    for (unsigned i = 0; i < kNumSlots; i++) {
        const uint32_t caller = g_slots[i].addr;
        const RecompEntry* f = std::lower_bound(b, e, caller, [](const RecompEntry& r, uint32_t a) { return r.addr < a; });
        if (f == e || f->addr != caller || f + 1 == e) continue;  // no body (call_lr: none listed either)
        const uint32_t n = (f[1].addr - caller) / 4;
        words.resize(n);
        for (uint32_t k = 0; k < n; k++) words[k] = ld32(caller + 4 * k);
        targets[i] = call_site_targets(words.data(), caller, n, g_recomp_decomp_import_calls, g_recomp_decomp_import_call_count);
        listed += targets[i].size();
    }
    std::vector<uint32_t> bits(kNumSlots, 0);
    g_site_pool.reserve(4 * listed + 2 * kNumSlots);
    std::vector<size_t> base(kNumSlots, 0);
    for (unsigned i = 0; i < kNumSlots; i++) {
        if (targets[i].empty()) continue;
        base[i] = g_site_pool.size();
        bits[i] = site_table(targets[i], g_site_pool, [&](uint32_t t) {
            CallSite c{};
            c.lr = call_lr(i, t, 0, 1u);
            if (const DirectEntry* d = direct_entry(t)) { c.slot1 = uint32_t(d->slot - g_slots) + 1; c.ordinal = d->ordinal; }
            return c;
        });
    }
    g_caller_table.resize(kNumSlots);
    for (unsigned i = 0; i < kNumSlots; i++) {
        CallerSites& c = g_caller_table[i];
        c.t = targets[i].empty() ? nullptr : g_site_pool.data() + base[i];
        c.bits = (uint8_t)bits[i];
        c.bctrl_lr = call_lr(i, 0, 1, 1u);
        c.frame_exact = g_slots[i].frame_exact;
    }
    LOG("[decomp] call sites: %zu targets in %zu table entries (%zu KB)", listed, g_site_pool.size(),
        g_site_pool.size() * sizeof(CallSite) / 1024);
    if (const char* chk = getenv("WWHD_DECOMP_SITES_CHECK"); chk && *chk == '1') {
        // the same questions to both: listed targets, their neighbours, bctrl, the caller itself, every 97th slot's
        // address, an odd target and 0; kind 0 and 1 (twice); LR in: a stale value
        uint64_t checked = 0, bad = 0, found = 0;
        const uint32_t kStale = 0xDEADBEE0u;
        g_callers = g_caller_table.data();
        for (unsigned i = 0; i < kNumSlots; i++) {
            std::vector<uint32_t> ts = targets[i];
            for (uint32_t t : targets[i]) ts.push_back(t + 4), ts.push_back(t - 4);
            ts.push_back(g_slots[i].addr);
            ts.push_back(0);
            ts.push_back(0x02000001u);
            ts.push_back(0x12345678u);
            for (unsigned k = i % 97; k < kNumSlots; k += 97) ts.push_back(g_slots[k].addr);
            for (uint32_t t : ts)
                for (int kind : {0, 1, 1}) {  // bctrl twice: the caller's last-target word missed, then hit
                    const CallTo want = call_to_lookup(i, t, kind, kStale);
                    const CallTo got = call_to(i, t, kind, kStale);
                    checked++;
                    if (kind == 0 && site_find(g_caller_table[i], t)) {
                        found++;
                        // the entry's ordinal is direct_slot's: a mod hook set later turns both off alike
                        const CallSite* s = site_find(g_caller_table[i], t);
                        const DirectEntry* d = direct_entry(t);
                        if (d ? (s->slot1 != uint32_t(d->slot - g_slots) + 1 || s->ordinal != d->ordinal) : s->slot1 != 0) {
                            if (bad++ < 20) LOG("[decomp] sites check: %08X -> %08X: direct entry differs", g_slots[i].addr, t);
                        }
                    }
                    if (kind == 0 && call_lr_by_site(i, t, kStale) != want.lr && bad++ < 20)
                        LOG("[decomp] sites check: %08X -> %08X: call_lr_by_site differs", g_slots[i].addr, t);
                    if ((got.lr != want.lr || got.s != want.s) && bad++ < 20)
                        LOG("[decomp] sites check: %08X -> %08X kind %d: sites %08X/%d, lookup %08X/%d", g_slots[i].addr, t, kind,
                            got.lr, got.s ? int(got.s - g_slots) : -1, want.lr, want.s ? int(want.s - g_slots) : -1);
                }
        }
        for (uint32_t h = 0; g_direct && h < kDirectSize; h++)  // the slots' ordinals (bctrl's mod-flag check)
            if (g_direct[h].addr && g_direct[h].slot->ordinal != g_direct[h].ordinal && bad++ < 20)
                LOG("[decomp] sites check: %08X: slot ordinal differs", g_direct[h].addr);
        LOG("[decomp] sites check: %llu answers (%llu from a call-site entry), %llu differ", (unsigned long long)checked,
            (unsigned long long)found, (unsigned long long)bad);
    }
    g_callers = g_caller_table.data();
}
}  // namespace

}  // namespace decomp
extern "C" void decomp_mod_hooked(uint32_t ordinal) {
    using namespace decomp;
    uint8_t* ok = g_decomp_native_ok;
    if (!ok || ordinal >= g_recomp_func_count) return;
    const uint32_t addr = g_recomp_funcs[ordinal].addr;
    Slot* s = std::lower_bound(g_slots, g_slots + kNumSlots, addr, [](const Slot& x, uint32_t v) { return x.addr < v; });
    if (s != g_slots + kNumSlots && s->addr == addr) {
        __atomic_store_n(&ok[s - g_slots], 0, __ATOMIC_RELAXED);
        if (GabiNativeEntry* ne = g_decomp_native_entry) __atomic_store_n(&ne[s - g_slots].sig, 0u, __ATOMIC_RELAXED);
    }
}
namespace decomp {

int g_debug = 0;
namespace {
int g_guard = 0;  // 1 log, 2 restore
bool g_stats = false;
// WWHD_DECOMP_ENTRY_STATS=PATH (diagnostics, any decomp build): every entry into a decompiled body through its
// guest entry (f_X -> hook_X -> decomp::call: recompiled callers, the kept recompiled functions, HLE callbacks,
// thread starts, dispatch-table calls), counted by (LR, callee slot); written to PATH every 2 s as
// count, caller function (the recompiled-table entry at or below LR), its kind (kept = recompiler-kept, decomp =
// decompiled, recomp = recompiled, hle = LR outside the code), LR, callee address, callee name
bool g_estats = false;
std::mutex g_estats_mu;
std::map<std::pair<uint32_t, uint32_t>, uint64_t>* g_estats_map = nullptr;
void estats_write(const std::string& path);
std::unique_ptr<std::atomic<uint64_t>[]> g_ncand, g_norig;

std::vector<std::string> split_list(const char* e) {
    std::vector<std::string> v;
    if (!e || !*e) return v;
    std::string s = e;
    if (s[0] == '@') {
        std::ifstream f(s.substr(1));
        std::string line;
        while (std::getline(f, line)) {
            line = line.substr(0, line.find('#'));
            std::istringstream ls(line);
            std::string w;
            if (ls >> w) v.push_back(w);
        }
        return v;
    }
    std::istringstream ss(s);
    std::string w;
    while (std::getline(ss, w, ','))
        if (!w.empty()) v.push_back(w);
    return v;
}

int unit_index(const std::string& n) {
    for (unsigned i = 0; i < kNumUnits; i++)
        if (n == kUnits[i]) return (int)i;
    for (unsigned i = 0; i < kNumAliases; i++)
        if (n == kAliases[i].name) return kAliases[i].unit;
    return -1;
}

int ordinal_of(uint32_t addr) {
    const RecompEntry* b = g_recomp_funcs;
    const RecompEntry* e = g_recomp_funcs + g_recomp_func_count;
    const RecompEntry* it = std::lower_bound(b, e, addr, [](const RecompEntry& r, uint32_t a) { return r.addr < a; });
    return it != e && it->addr == addr ? (int)(it - b) : -1;
}

void write_stats(const std::string& path) {
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if (!f) return;
    for (unsigned i = 0; i < kNumSlots; i++) {
        const Slot& s = g_slots[i];
        uint64_t a = g_ncand[i].load(std::memory_order_relaxed), b = g_norig[i].load(std::memory_order_relaxed);
        if (a || b) fprintf(f, "%08X\t%s\t%s\t%d\t%llu\t%llu\n", s.addr, kUnits[s.unit], s.name, s.on, (unsigned long long)a, (unsigned long long)b);
    }
    fclose(f);
    rename(tmp.c_str(), path.c_str());
}

void estats_write(const std::string& path) {
    std::vector<std::pair<std::pair<uint32_t, uint32_t>, uint64_t>> v;
    {
        std::lock_guard<std::mutex> l(g_estats_mu);
        v.assign(g_estats_map->begin(), g_estats_map->end());
    }
    static const std::set<uint32_t> kept(g_recomp_decomp_kept, g_recomp_decomp_kept + g_recomp_decomp_kept_count);
    // per (caller function, callee): the LRs merged
    std::map<std::pair<uint32_t, uint32_t>, std::pair<uint64_t, uint32_t>> m;
    for (auto& [k, n] : v) {
        const uint32_t lr = k.first;
        uint32_t fn = 0;
        if (lr >= 0x02000000u && lr < 0x10000000u) {
            unsigned lo = 0, hi = g_recomp_func_count;
            while (lo < hi) {
                unsigned mid = (lo + hi) / 2;
                if (g_recomp_funcs[mid].addr <= lr) lo = mid + 1; else hi = mid;
            }
            if (lo) fn = g_recomp_funcs[lo - 1].addr;
        }
        auto& e = m[{fn, k.second}];
        e.first += n;
        e.second = lr;
    }
    std::vector<std::pair<std::pair<uint32_t, uint32_t>, std::pair<uint64_t, uint32_t>>> w(m.begin(), m.end());
    std::sort(w.begin(), w.end(), [](const auto& a, const auto& b) { return a.second.first > b.second.first; });
    FILE* fp = fopen((path + ".tmp").c_str(), "w");
    if (!fp) return;
    for (auto& [k, e] : w) {
        const uint32_t fn = k.first;
        const Slot* cs = fn ? direct_slot(fn) : nullptr;
        const char* kind = !fn ? "hle" : kept.count(fn) ? "kept" : cs ? "decomp" : "recomp";
        const Slot* cs2 = nullptr;
        if (fn && !cs) {  // a decompiled caller that is not in the direct table (frame-exact, off): by slot address
            const Slot* b = std::lower_bound(g_slots, g_slots + kNumSlots, fn, [](const Slot& a, uint32_t x) { return a.addr < x; });
            if (b != g_slots + kNumSlots && b->addr == fn) cs2 = b;
        }
        if (cs2 && !kept.count(fn)) kind = "decomp";
        const Slot& t = g_slots[k.second];
        fprintf(fp, "%llu\t%08X\t%s\t%s\t%08X\t%08X\t%s\n", (unsigned long long)e.first, fn, kind,
                cs ? cs->name : cs2 ? cs2->name : "-", e.second, t.addr, t.name);
    }
    fclose(fp);
    rename((path + ".tmp").c_str(), path.c_str());
}

// WWHD_DECOMP_GUARD: the caller's back chain word at the entry SP must not change across a decompiled
// function (no function writes it); a change is a function writing past its topmost stack object
// (a gabi::Local smaller than what a real callee writes into it). The report lists the other changed
// words of the 0x40 bytes above SP (+8.. may also be legitimate output parameters).
void guard_report(Cpu* c, const Slot& s, const uint8_t* before) {
    static std::mutex m;
    static std::set<uint32_t> seen;
    std::lock_guard lk(m);
    uint8_t* now = ppc_ptr(c->r[1]);
    if (seen.insert(s.addr).second) {
        std::string d;
        for (int o = 0; o < 0x40; o += 4) {
            if (o == 4 || !memcmp(before + o, now + o, 4)) continue;
            char b[48];
            snprintf(b, sizeof b, " +%02X:%08X->%08X", o, __builtin_bswap32(*(const uint32_t*)(before + o)), ld32(c->r[1] + o));
            d += b;
        }
        LOG("[decomp] GUARD %08X %s (%s): wrote into its caller's frame above SP %08X:%s", s.addr, s.name, kUnits[s.unit], c->r[1], d.c_str());
    }
    if (g_guard == 2) {  // restore: keep the run going (the difference is reported above)
        uint32_t lr = ld32(c->r[1] + 4);
        memcpy(now, before, 0x40);
        st32(c->r[1] + 4, lr);
    }
}
}  // namespace

void call_debug(Cpu* c, Slot& s) {
    size_t i = &s - g_slots;
    if (g_estats) {
        {
            std::lock_guard<std::mutex> l(g_estats_mu);
            (*g_estats_map)[{c->lr, (uint32_t)i}]++;
        }
        if (!(g_debug & 1)) {  // entry stats only: the entry as decomp::call makes it
            if (!s.on) return s.orig(c);
            const uint16_t outer = c->decomp_running;
            c->decomp_running = uint16_t(i + 1);
            s.cand(c);
            c->decomp_running = outer;
            return;
        }
    }
    if (!s.on) {
        if (g_stats) g_norig[i].fetch_add(1, std::memory_order_relaxed);
        s.orig(c);
        return;
    }
    if (g_stats) g_ncand[i].fetch_add(1, std::memory_order_relaxed);
    const uint16_t outer = c->decomp_running;
    c->decomp_running = uint16_t(&s - g_slots + 1);
    if (!g_guard) {
        s.run(c, s.cand);
        c->decomp_running = outer;
        return;
    }
    uint8_t before[0x40];
    memcpy(before, ppc_ptr(c->r[1]), sizeof before);
    s.run(c, s.cand);
    c->decomp_running = outer;
    if (memcmp(before, ppc_ptr(c->r[1]), 4)) guard_report(c, s, before);
}

void init() {
    if (kNumSlots >= 0xFFFF) {  // Cpu::decomp_running holds slot + 1 in 16 bits
        fprintf(stderr, "[decomp] %u slots do not fit Cpu::decomp_running\n", kNumSlots);
        abort();
    }
    if (const char* g = getenv("WWHD_DECOMP_GUARD")) g_guard = !strcmp(g, "restore") ? 2 : !strcmp(g, "log") || atoi(g) ? 1 : 0;
    std::vector<char> unit_on(kNumUnits, 1);
    if (const char* e = getenv("WWHD_DECOMP")) {
        std::fill(unit_on.begin(), unit_on.end(), 0);
        for (const std::string& u : split_list(e)) {
            if (u == "all") std::fill(unit_on.begin(), unit_on.end(), 1);
            else if (u == "none") std::fill(unit_on.begin(), unit_on.end(), 0);
            else if (int i = unit_index(u); i >= 0) unit_on[i] = 1;
            else LOG("[decomp] WWHD_DECOMP: unknown unit %s", u.c_str());
        }
    }
    std::set<uint32_t> only, excl;
    std::vector<char> unit_excl(kNumUnits, 0);
    auto only_list = split_list(getenv("WWHD_DECOMP_FUNCS"));
    for (auto& w : only_list) only.insert((uint32_t)strtoul(w.c_str(), nullptr, 16));
    for (auto& w : split_list(getenv("WWHD_DECOMP_EXCLUDE"))) {
        if (int i = unit_index(w); i >= 0) unit_excl[i] = 1;
        else excl.insert((uint32_t)strtoul(w.c_str(), nullptr, 16));
    }
    std::set<uint32_t> kept(g_recomp_decomp_kept, g_recomp_decomp_kept + g_recomp_decomp_kept_count);
    unsigned on = 0, missing = 0, modded = 0, units = 0;
    std::vector<char> unit_used(kNumUnits, 0);
    for (unsigned i = 0; i < kNumSlots; i++) {
        Slot& s = g_slots[i];
        bool want = unit_on[s.unit] && !unit_excl[s.unit] && !excl.count(s.addr) && (only_list.empty() || only.count(s.addr)) &&
                    !kept.count(s.addr);
        if (want && g_mod_hook_flags) {
            // a guest mod hooks or replaces this function: its check is in the recompiled original
            int o = ordinal_of(s.addr);
            if (o >= 0 && g_mod_hook_flags[o]) { want = false; modded++; }
        }
        if (want && !s.cand) { missing++; LOG("[decomp] %08X %s: no decompiled function registered", s.addr, s.name); }
        s.on = want && s.cand;
        on += s.on;
        if (s.on && !unit_used[s.unit]) { unit_used[s.unit] = 1; units++; }
    }
    frames_init();
    lr_init();
    LOG("[decomp] decomp build: %u of %u decompiled functions run (%u game units); %u kept recompiled by the "
        "recompiler, %u hooked by guest mods%s", on, kNumSlots, units, (unsigned)kept.size(), modded,
        missing ? "; some decompiled functions missing" : "");
    if (const char* p = getenv("WWHD_DECOMP_STATS")) {
        g_stats = true;
        g_ncand.reset(new std::atomic<uint64_t>[kNumSlots]());
        g_norig.reset(new std::atomic<uint64_t>[kNumSlots]());
        static std::string path = p;
        std::thread([] {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                write_stats(path);
            }
        }).detach();
        atexit([] { write_stats(path); });
    }
    if (const char* p = getenv("WWHD_DECOMP_ENTRY_STATS")) {
        g_estats_map = new std::map<std::pair<uint32_t, uint32_t>, uint64_t>();
        g_estats = true;
        static std::string path = p;
        std::thread([] {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                estats_write(path);
            }
        }).detach();
        LOG("[decomp] guest-entry stats: %s", p);
    }
    g_debug = (g_stats || g_guard ? 1 : 0) | (g_estats ? 2 : 0);
#ifdef WWHD_DECOMP_FAST_LOCAL
    {  // WWHD_DECOMP_FAST_LOCAL=0: the general Local form; WWHD_NATIVE_LOCALS=1 (in-frame placement) needs it too
        const char* e = getenv("WWHD_DECOMP_FAST_LOCAL");
        const char* nl = getenv("WWHD_NATIVE_LOCALS");
        g_decomp_fast_local = (e && !strcmp(e, "0")) || (nl && *nl == '1') ? 0 : 1;
        LOG("[decomp] fast Local form: %s", g_decomp_fast_local ? "on" : "off");
    }
#endif
#ifdef WWHD_DECOMP_FAST_COUNT
    if (const char* p = getenv("WWHD_DECOMP_FAST_COUNT")) {  // Mac statistics: logic steps and the gabi.h counters
        static std::string path = p;
        std::thread([] {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                if (FILE* fp = fopen(path.c_str(), "a")) {
                    fprintf(fp, "%llu", (unsigned long long)interp::logic_steps());
                    for (auto& n : g_decomp_fast_count) fprintf(fp, " %llu", (unsigned long long)__atomic_load_n(&n, __ATOMIC_RELAXED));
                    fprintf(fp, "\n");
                    fclose(fp);
                }
            }
        }).detach();
    }
#endif
#ifdef WWHD_DECOMP_FAST_STR
    {  // WWHD_DECOMP_FAST_STR=0: guest_str's per-thread cache at every call site
        const char* e = getenv("WWHD_DECOMP_FAST_STR");
        g_decomp_fast_str = e && !strcmp(e, "0") ? 0 : 1;
        LOG("[decomp] per-site guest strings: %s", g_decomp_fast_str ? "on" : "off");
    }
#endif
#ifdef WWHD_DECOMP_FAST
#ifdef WWHD_DECOMP_FAST_LEAF
    if (const char* e = getenv("WWHD_DECOMP_FAST_LEAF"); e && !strcmp(e, "0")) {
        g_decomp_leaf_off = 1;
        LOG("[decomp] fast mode: WWHD_DECOMP_FAST_LEAF=0, leaf bodies take the full native-call scope");
    }
#ifdef WWHD_DECOMP_FAST_LEAF_CHECK
    LOG("[decomp] fast mode: leaf check build (a call or frame use from a leaf body traps)");
#endif
#endif
#ifdef WWHD_DECOMP_FAST_NSTATS
    if (const char* p = getenv("WWHD_DECOMP_FAST_NSTATS")) {
        g_decomp_nstats = new uint64_t[kNumSlots]();
        static std::string path = p;
        std::thread([] {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                FILE* fp = fopen((path + ".tmp").c_str(), "w");
                if (!fp) continue;
                for (unsigned i = 0; i < kNumSlots; i++)
                    if (const uint64_t n = __atomic_load_n(&g_decomp_nstats[i], __ATOMIC_RELAXED))
                        fprintf(fp, "%llu\t%08X\t%s\n", (unsigned long long)n, g_slots[i].addr, g_slots[i].name);
                fclose(fp);
                rename((path + ".tmp").c_str(), path.c_str());
            }
        }).detach();
        LOG("[decomp] fast mode native-entry stats: %s", p);
    }
#endif
#ifdef WWHD_DECOMP_FAST_CHECKCOUNT
    if (const char* p = getenv("WWHD_DECOMP_FAST_CHECKCOUNT")) {
        // diagnostics: native_enter_ok's checks counted per callee, written every WWHD_DECOMP_FAST_CHECKCOUNT_EVERY seconds
        // (default 10) and at exit: totals per check, per callee runs/refusals, every refusal by (callee, check, caller, arg)
        g_cc_fail = new CcFail[kCcFailSize];
        g_decomp_checkcount = new uint64_t[(size_t)kNumSlots * 2 * kCcKinds]();
        static std::string path = p;
        static int every = 10;
        if (const char* e = getenv("WWHD_DECOMP_FAST_CHECKCOUNT_EVERY"); e && atoi(e) > 0) every = atoi(e);
        static auto write = [] {
            static std::mutex m;
            std::lock_guard<std::mutex> lk(m);
            const std::string tmp = path + ".tmp";
            FILE* fp = fopen(tmp.c_str(), "w");
            if (!fp) return;
            uint64_t tot[2 * kCcKinds] = {};
            for (unsigned i = 0; i < kNumSlots; i++)
                for (unsigned k = 0; k < 2 * kCcKinds; k++) tot[k] += __atomic_load_n(&g_decomp_checkcount[(size_t)i * 2 * kCcKinds + k], __ATOMIC_RELAXED);
            fprintf(fp, "# checkcount v1 logic_steps %llu lost_fail_records %llu\n# totals: check runs refusals\n",
                    (unsigned long long)interp::logic_steps(), (unsigned long long)g_cc_fail_lost.load());
            for (unsigned k = 0; k < kCcKinds; k++)
                fprintf(fp, "T\t%s\t%llu\t%llu\n", kCcName[k], (unsigned long long)tot[k], (unsigned long long)tot[kCcKinds + k]);
            fprintf(fp, "# per callee: S addr name then runs/refusals per check (guest nested static rule caller nbyte arg); "
                        "slot state on fx hooked modhook nok\n");
            for (unsigned i = 0; i < kNumSlots; i++) {
                const uint64_t* c = g_decomp_checkcount + (size_t)i * 2 * kCcKinds;
                bool any = false;
                for (unsigned k = 0; k < 2 * kCcKinds; k++) any |= __atomic_load_n(&c[k], __ATOMIC_RELAXED) != 0;
                if (!any) continue;
                const Slot& s = g_slots[i];
                fprintf(fp, "S\t%08X\t%s", s.addr, s.name);
                for (unsigned k = 0; k < kCcKinds; k++)
                    fprintf(fp, "\t%llu/%llu", (unsigned long long)__atomic_load_n(&c[k], __ATOMIC_RELAXED),
                            (unsigned long long)__atomic_load_n(&c[kCcKinds + k], __ATOMIC_RELAXED));
                fprintf(fp, "\t%u %u %u %u %u\n", s.on, s.frame_exact, s.hooked,
                        g_mod_hook_flags && s.ordinal ? (unsigned)g_mod_hook_flags[s.ordinal] : 0u,
                        g_decomp_native_ok ? (unsigned)g_decomp_native_ok[i] : 256u);
            }
            fprintf(fp, "# refusals: F count check callee_addr callee caller_addr caller arg value\n");
            auto name_of = [](uint32_t a) -> const char* {
                const Slot* b = g_slots;
                const Slot* e = b + kNumSlots;
                const Slot* s = std::lower_bound(b, e, a, [](const Slot& x, uint32_t v) { return x.addr < v; });
                return s != e && s->addr == a ? s->name : "-";
            };
            for (uint32_t h = 0; h < kCcFailSize; h++) {
                const uint64_t key = g_cc_fail[h].key.load(std::memory_order_acquire);
                if (!key) continue;
                const uint32_t slot = (uint32_t)(key >> 8 & 0xFFFFFF) - 1, caller = (uint32_t)(key >> 32);
                fprintf(fp, "F\t%llu\t%s\t%08X\t%s\t%08X\t%s\t%u\t%llx\n", (unsigned long long)g_cc_fail[h].n.load(),
                        kCcName[key >> 4 & 15], g_slots[slot].addr, g_slots[slot].name, caller, name_of(caller),
                        (unsigned)(key & 15), (unsigned long long)g_cc_fail[h].value.load());
            }
            fclose(fp);
            rename(tmp.c_str(), path.c_str());
        };
        std::thread([] {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::seconds(every));
                write();
            }
        }).detach();
        atexit([] { write(); });
        LOG("[decomp] fast mode check counts: %s (every %d s)", p, every);
    }
#endif
#ifdef WWHD_DECOMP_FAST_PTR_SUB
    if (const char* e = getenv("WWHD_DECOMP_FAST_PTR_SUB"); e && !strcmp(e, "0")) {
        g_decomp_ptr_sub = 0;
        LOG("[decomp] fast mode: WWHD_DECOMP_FAST_PTR_SUB=0, pointer calls take native entries of their own shape only");
    }
#endif
#ifdef WWHD_DECOMP_FAST_A3_FN
    if (const char* e = getenv("WWHD_DECOMP_FAST_A3_FN"); e && !strcmp(e, "0")) {
        g_decomp_a3_off = 1;
        LOG("[decomp] fast mode: WWHD_DECOMP_FAST_A3_FN=0, A3 by source again (the per-function rule's bodies make guest calls)");
    }
#endif
    if (const char* p = getenv("WWHD_DECOMP_FAST_STATS")) {
        g_fstats_map = new std::map<std::tuple<uint32_t, uint32_t, int>, uint64_t>();
        g_fstats = true;
        static std::string path = p;
        std::thread([] {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                fstats_write(path);
            }
        }).detach();
        LOG("[decomp] fast mode stats: %s", p);
    }
#endif
    direct_init();
    sites_init();
#ifdef WWHD_DECOMP_EDGE_LCALL
    if (const char* e = getenv("WWHD_DECOMP_EDGE_CHECK"); e && *e == '1') {
        g_edge_check = true;
        LOG("[decomp] WWHD_DECOMP_EDGE_CHECK=1: every edge call compared with call_to");
    }
#endif
}
}  // namespace decomp

#ifdef WWHD_DECOMP_FAST
static void fstats_write(const std::string& path) {
    std::vector<std::pair<std::tuple<uint32_t, uint32_t, int>, uint64_t>> v;
    {
        std::lock_guard<std::mutex> l(g_fstats_mu);
        v.assign(g_fstats_map->begin(), g_fstats_map->end());
    }
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    FILE* fp = fopen((path + ".tmp").c_str(), "w");
    if (!fp) return;
    for (auto& [k, n] : v) {
        const auto [f, t, kind] = k;
        const decomp::Slot* ts = decomp::direct_slot(t);
        fprintf(fp, "%llu\t%s\t%08X\t%d\t%s\n", (unsigned long long)n, f ? decomp::g_slots[f - 1].name : "-", t, kind,
                ts ? ts->name : "-");
    }
    fclose(fp);
    rename((path + ".tmp").c_str(), path.c_str());
}
#endif
