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
#include <thread>
#include <unordered_map>
#include <vector>

#include "../runtime.h"
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
void gmem_call(Cpu* c, uint32_t target, int kind, int, int) {
    // kind 1: through a function pointer (bctrl: CTR holds the target)
    if (kind == 1) c->ctr = target;
    // LR as the original's bl/bctrl leaves it: a runtime hook, an HLE import or a mod that tells its
    // callers apart by LR (story_mail's text-length check, true60's call-site checks) sees the same
    // caller as in the normal build (before, LR still held the return address into the decompiled
    // function's own caller)
    if (const uint32_t f = decomp::t_running) c->lr = decomp::call_lr(f, target, kind, c->lr);
    c->pc = target;
    ppc_dispatch(c);
}

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

namespace decomp {
void frames_init();  // frames.cpp

thread_local uint32_t t_running = 0;

namespace {
struct LrKey {
    uint32_t caller, target;
    bool operator==(const LrKey& o) const { return caller == o.caller && target == o.target; }
};
struct LrHash {
    size_t operator()(const LrKey& k) const { return (size_t(k.caller) << 32) ^ k.target; }
};
}  // namespace

uint32_t call_lr(uint32_t caller, uint32_t target, int kind, uint32_t lr) {
    // per thread: no lock on the hot path; a result is a fact of the game's code, the same on every thread
    // (0: no unique call, LR stays as it is)
    static thread_local std::unordered_map<LrKey, uint32_t, LrHash> cache;
    const LrKey key{caller, kind == 1 ? 1u : target};
    auto it = cache.find(key);
    if (it == cache.end()) {
        uint32_t ret = 0;
        const RecompEntry* b = g_recomp_funcs;
        const RecompEntry* e = g_recomp_funcs + g_recomp_func_count;
        const RecompEntry* f = std::lower_bound(b, e, caller, [](const RecompEntry& r, uint32_t a) { return r.addr < a; });
        if (f != e && f->addr == caller && f + 1 != e) {
            const uint32_t n = (f[1].addr - caller) / 4;
            std::vector<uint32_t> words(n);
            for (uint32_t i = 0; i < n; i++) words[i] = ld32(caller + 4 * i);
            ret = call_lr_scan(words.data(), caller, n, target, kind, 0, g_recomp_decomp_import_calls,
                               g_recomp_decomp_import_call_count);
        }
        it = cache.emplace(key, ret).first;
    }
    return it->second ? it->second : lr;
}

int g_debug = 0;
namespace {
int g_guard = 0;  // 1 log, 2 restore
bool g_stats = false;
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
    if (!s.on) {
        if (g_stats) g_norig[i].fetch_add(1, std::memory_order_relaxed);
        s.orig(c);
        return;
    }
    if (g_stats) g_ncand[i].fetch_add(1, std::memory_order_relaxed);
    const uint32_t outer = t_running;
    t_running = s.addr;
    if (!g_guard) {
        s.run(c, s.cand);
        t_running = outer;
        return;
    }
    uint8_t before[0x40];
    memcpy(before, ppc_ptr(c->r[1]), sizeof before);
    s.run(c, s.cand);
    t_running = outer;
    if (memcmp(before, ppc_ptr(c->r[1]), 4)) guard_report(c, s, before);
}

void init() {
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
    g_debug = g_stats || g_guard;
}
}  // namespace decomp
