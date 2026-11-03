// SPDX-License-Identifier: MPL-2.0
// Contract test of the replacements that write guest memory and call game code (runtime/src/hostrt):
// the recompiled original and the replacement run from the same CPU state and the same memory, with the
// game functions they call (constructors, destructors, operator new/delete) modelled by deterministic
// stand-ins that write memory and clobber volatile registers. They must agree on
// - every call made: target, r3..r10, the stack (SP offset, back chain, stack-argument words), LR;
// - the memory they and their callees wrote (a scratch region, and the caller's frame above SP);
// - the result registers callers read; the replacement must keep every other register.
// A replacement may hand an input class back to the original (it then runs the original itself).
//
// usage: calltest IMAGE [ROOT|all] [count]
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "hostrt_impl.h"
#include "rt.h"

using namespace difftest;

// threads per run: HOSTRT_TEST_THREADS (default 4; the machine-wide budget is 4 across all runs)
static int test_threads() {
    const char* e = getenv("HOSTRT_TEST_THREADS");
    int n = e ? atoi(e) : 4;
    return n < 1 ? 1 : n > 4 ? 4 : n;
}

extern "C" {
void f_028EFFD0(Cpu*);
void f_028F026C(Cpu*);
void f_028F521C(Cpu*);
void f_028F5914(Cpu*);
void f_028F040C(Cpu*);
}

namespace {

constexpr uint32_t kScratch = 0xE0000000u, kScratchSize = 0x40000;  // per slot: + slot * 0x100000
constexpr uint32_t kFakeBase = 0x02100000u;                          // fake constructor/destructor addresses

uint32_t mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    return x ^ (x >> 16);
}

thread_local uint32_t t_heap;  // the bump pointer of the operator new stand-in (reset before each run)

// stand-ins for the game functions the runtime calls: operator new / new[] (bump allocation in the
// scratch region, every 16th request fails), operator delete / delete[], and fake constructors and
// destructors (they mark the object and clobber the volatile registers)
bool model(Cpu* c, uint32_t addr) {
    if (addr == 0x0273AD10u || addr == 0x0273ADACu) {
        const uint32_t n = c->r[3];
        c->r[3] = (mix(n ^ t_heap) & 15) ? t_heap : 0;
        if (c->r[3]) t_heap += (n + 15) & ~15u;
        return true;
    }
    if (addr == 0x0273AF40u || addr == 0x0273AFC8u) return true;
    if ((addr & 0xFFF00000u) != kFakeBase) return false;
    const uint32_t h = mix(c->r[3] ^ addr);
    if (c->r[3] >= 0xE0000000u) st32(c->r[3], h);
    c->r[0] = mix(h + 1);
    for (int i = 4; i <= 12; i++) c->r[i] = mix(h + i);
    for (int i = 0; i <= 13; i++) c->f[i].ps0 = u64_as_f64((uint64_t)mix(h + 100 + i) << 32 | mix(h + 200 + i));
    c->ctr = mix(h + 300);
    for (int i = 0; i < 8; i++) c->cr[i] = mix(h + 400 + i) & 1;
    c->r[3] = mix(h + 3);
    return true;
}

enum Out { R3 = 1, R4 = 2 };

struct Root {
    const char* name;
    void (*orig)(Cpu*);
    bool (*repl)(Cpu*);
    std::function<void(Cpu&, std::mt19937_64&, uint32_t scratch)> gen;
    int outs;
    std::vector<uint32_t> globals = {};  // guest words outside the scratch region the function may write
};

template <void (*F)(Cpu*)>
bool always(Cpu* c) {
    F(c);
    return true;
}

std::vector<Root> roots() {
    return {
        {"construct_array", f_028EFFD0, hostrt::construct_array_028EFFD0,
         [](Cpu& c, std::mt19937_64& r, uint32_t s) {
             static const uint32_t sizes[] = {1, 4, 8, 12, 0x2C, 0x12C};
             c.r[3] = (r() & 7) ? s + 0x100 + ((uint32_t)r() & 0xFFC) : 0;
             c.r[4] = (r() & 15) ? (uint32_t)(r() % 7) : (uint32_t)(r() % 60);
             c.r[5] = sizes[r() % 6];
             c.r[6] = (r() & 7) ? kFakeBase + ((uint32_t)r() & 0xFFFC) : 0;
         },
         R3 | R4},
        {"register_global", f_028F026C, always<hostrt::register_global_object_028F026C>,
         [](Cpu& c, std::mt19937_64& r, uint32_t s) {
             const uint32_t rec = s + 0x100 + ((uint32_t)r() & 0xFF0);
             c.r[3] = rec;
             if (r() & 1) st32(rec, 0);
             const uint32_t m = r() % 4;
             st32(0x104DA130u, m == 0 ? rec : m == 1 ? 0 : s + ((uint32_t)r() & 0xFFF0));
         },
         R3 | R4, {0x104DA130u}},
        {"bzero", f_028F521C, always<hostrt::bzero_028F521C>,
         [](Cpu& c, std::mt19937_64& r, uint32_t s) {
             c.r[3] = s + ((uint32_t)r() & 0x3FFF);
             c.r[4] = (r() & 3) ? (uint32_t)(r() % 0x200) : (uint32_t)(r() % 0x8000);
         },
         R3 | R4},
        {"zero_words", f_028F5914, always<hostrt::zero_words_028F5914>,
         [](Cpu& c, std::mt19937_64& r, uint32_t s) {
             c.r[3] = s + ((uint32_t)r() & 0x3FFC);
             c.r[4] = (r() & 3) ? (uint32_t)(r() % 40) : (uint32_t)(r() % 0x2000);
         },
         R3 | R4},
        {"strcpy", f_028F040C, always<hostrt::strcpy_028F040C>,
         [](Cpu& c, std::mt19937_64& r, uint32_t s) {
             const uint32_t src = s + 0x8000 + ((uint32_t)r() & 0xFFF), len = (uint32_t)(r() % ((r() & 3) ? 40 : 0x400));
             for (uint32_t i = 0; i < len; i++) st8(src + i, (uint8_t)(1 + r() % 255));
             st8(src + len, 0);
             // destination: disjoint, or overlapping at or below the source (a forward copy ahead of it never ends)
             c.r[3] = (r() & 3) ? s + ((uint32_t)r() & 0x3FFF) : src - (uint32_t)(r() % 64);
             c.r[4] = src;
         },
         R3 | R4},
    };
}

void randomise(Cpu& c, std::mt19937_64& r, int slot) {
    reset(&c, slot);
    for (int i = 0; i < 32; i++)
        if (i != 1) c.r[i] = (uint32_t)r();
    for (int i = 0; i < 32; i++) c.f[i].ps0 = u64_as_f64(r()), c.f[i].ps1 = u64_as_f64(r());
    for (int i = 0; i < 32; i++) c.cr[i] = r() & 1;
    c.ctr = (uint32_t)r(), c.lr = 0x02000000u | ((uint32_t)r() & 0xFFFFFC);
    c.xer_ca = r() & 1, c.xer_so = r() & 1, c.xer_ov = r() & 1;
}

bool kept(const Cpu& e, const Cpu& b, int outs) {
    for (int i = 0; i < 32; i++) {
        if ((i == 3 && (outs & R3)) || (i == 4 && (outs & R4))) continue;
        if (e.r[i] != b.r[i]) return false;
    }
    return !memcmp(e.f, b.f, sizeof e.f) && !memcmp(e.cr, b.cr, sizeof e.cr) && e.ctr == b.ctr && e.lr == b.lr &&
           e.xer_ca == b.xer_ca && e.xer_so == b.xer_so && e.xer_ov == b.xer_ov;
}

bool same_call(const Call& a, const Call& b) {
    if (a.addr != b.addr || (a.import == nullptr) != (b.import == nullptr)) return false;
    if (a.import && strcmp(a.import, b.import)) return false;
    for (int i = 1; i <= 10; i++)
        if (i != 2 && a.r[i] != b.r[i]) return false;
    return !memcmp(a.stack, b.stack, sizeof a.stack) && a.lr == b.lr && a.back == b.back;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: calltest IMAGE [ROOT|all] [count]\n");
        return 2;
    }
    load_image(argv[1]);
    outside_hook = model;
    const std::string which = argc > 2 ? argv[2] : "all";
    const uint64_t count = argc > 3 ? strtoull(argv[3], nullptr, 0) : 1000000ull;
    int bad = 0;
    for (const Root& rt : roots()) {
        if (which != "all" && which != rt.name) continue;
        const int nth = rt.globals.empty() ? test_threads() : 1;  // globals are shared guest memory: one thread
        std::vector<uint64_t> fails(nth), handed(nth), calls(nth);
        std::vector<std::thread> th;
        for (int k = 0; k < nth; k++)
            th.emplace_back([&, k] {
                std::mt19937_64 r(0xCA11000u + k);
                const uint32_t s = kScratch + 0x100000u * k;
                std::vector<uint8_t> init(kScratchSize), after_a(kScratchSize);
                Cpu e, a, b;
                CallLog la, lb;
                for (uint64_t i = k; i < count; i += nth) {
                    for (uint32_t j = 0; j < kScratchSize; j += 4) {
                        uint32_t w = (uint32_t)r();
                        memcpy(&init[j], &w, 4);
                    }
                    randomise(e, r, k);
                    memcpy(PPC_MEM_BASE + s, init.data(), kScratchSize);  // the generator may plant data in it
                    std::vector<uint32_t> g_init;
                    for (uint32_t g : rt.globals) st32(g, (uint32_t)r());
                    rt.gen(e, r, s);
                    memcpy(init.data(), PPC_MEM_BASE + s, kScratchSize);
                    for (uint32_t g : rt.globals) g_init.push_back(ld32(g));
                    const uint32_t sp = e.r[1];
                    uint8_t frame_a[0x20], frame_init[0x20];
                    for (int j = 0; j < 0x20; j++) frame_init[j] = (uint8_t)r();
                    // original
                    memcpy(PPC_MEM_BASE + s, init.data(), kScratchSize);
                    memcpy(PPC_MEM_BASE + sp, frame_init, 0x20);
                    for (size_t g = 0; g < rt.globals.size(); g++) st32(rt.globals[g], g_init[g]);
                    a = e, t_entry_sp = sp, la.clear(), t_log = &la, t_heap = s + 0x20000;
                    rt.orig(&a);
                    memcpy(after_a.data(), PPC_MEM_BASE + s, kScratchSize);
                    memcpy(frame_a, PPC_MEM_BASE + sp, 0x20);
                    std::vector<uint32_t> g_a;
                    for (uint32_t g : rt.globals) g_a.push_back(ld32(g));
                    // replacement
                    memcpy(PPC_MEM_BASE + s, init.data(), kScratchSize);
                    memcpy(PPC_MEM_BASE + sp, frame_init, 0x20);
                    for (size_t g = 0; g < rt.globals.size(); g++) st32(rt.globals[g], g_init[g]);
                    b = e, t_entry_sp = sp, lb.clear(), t_log = &lb, t_heap = s + 0x20000;
                    if (!rt.repl(&b)) {
                        handed[k]++;
                        continue;
                    }
                    calls[k] += lb.size();
                    std::string why;
                    if (la.size() != lb.size()) why = "call count";
                    else
                        for (size_t j = 0; j < la.size() && why.empty(); j++)
                            if (!same_call(la[j], lb[j])) why = "call " + std::to_string(j);
                    if (why.empty() && memcmp(after_a.data(), PPC_MEM_BASE + s, kScratchSize)) why = "memory";
                    if (why.empty() && memcmp(frame_a, PPC_MEM_BASE + sp, 0x20)) why = "caller frame";
                    for (size_t g = 0; g < rt.globals.size() && why.empty(); g++)
                        if (ld32(rt.globals[g]) != g_a[g]) why = "global";
                    if (why.empty() && (((rt.outs & R3) && a.r[3] != b.r[3]) || ((rt.outs & R4) && a.r[4] != b.r[4]))) why = "result";
                    if (why.empty() && !kept(e, b, rt.outs)) why = "registers kept";
                    if (!why.empty() && fails[k]++ < 3)
                        printf("  %s FAIL (%s): r3 %08X r4 %08X r5 %08X r6 %08X r7 %08X r8 %08X; calls %zu/%zu; result %08X %08X / %08X %08X\n",
                               rt.name, why.c_str(), e.r[3], e.r[4], e.r[5], e.r[6], e.r[7], e.r[8], la.size(), lb.size(), a.r[3], a.r[4],
                               b.r[3], b.r[4]);
                }
            });
        for (auto& t : th) t.join();
        uint64_t f = 0, h = 0, n = 0;
        for (int k = 0; k < nth; k++) f += fails[k], h += handed[k], n += calls[k];
        printf("%-16s inputs %10" PRIu64 "  handed to the original %8" PRIu64 "  guest calls compared %10" PRIu64 "  failures %" PRIu64 "%s\n",
               rt.name, count, h, n, f, f ? "" : "  OK");
        fflush(stdout);
        bad += f != 0;
    }
    return bad ? 1 : 0;
}
