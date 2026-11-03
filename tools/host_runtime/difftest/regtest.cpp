// SPDX-License-Identifier: MPL-2.0
// Contract test of the register-only replacements (runtime/src/hostrt): for each input, the recompiled
// original and the replacement start from the same randomised CPU state and the same stack contents.
// They must agree on the result registers r3, r4, f1, f2 (both FPR lanes) and on the caller's side of
// the stack (SP .. SP+0x100: the LR save word), and the replacement must leave every other register as
// it was on entry (tools/host_runtime/callsite_regs.py shows no caller reads more).
//
// usage: regtest IMAGE [ROOT|all] [count]
#include <cinttypes>
#include <cmath>
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
void f_028F4114(Cpu*);
void f_028F41B0(Cpu*);
void f_028F3B98(Cpu*);
void f_028F543C(Cpu*);
void f_028F597C(Cpu*);
void f_028F5680(Cpu*);
void f_028F5678(Cpu*);
void f_0286CBC4(Cpu*);
void f_0286CD54(Cpu*);
void f_028F35A8(Cpu*);
void f_0286D5F0(Cpu*);
}

namespace {

using Gen = std::function<void(Cpu&, std::mt19937_64&, uint64_t)>;

// result registers a caller may read (tools/host_runtime/callsite_regs.tsv, clobbered_and_read_strict)
enum Out { R3 = 1, R4 = 2, F1 = 4, F2 = 8 };

struct Root {
    const char* name;
    void (*orig)(Cpu*);
    void (*repl)(Cpu*);
    Gen gen;  // sets the arguments; i < 2^32 may be used as an exhaustive index
    int outs;
    bool exhaustive32 = false;
};

uint64_t rnd64(std::mt19937_64& r) { return r() >> (r() & 63); }

void f1_single(Cpu& c, std::mt19937_64& r, uint64_t i, bool ex) {
    c.f[1].ps0 = ppc_lfs_value(ex ? (uint32_t)i : (uint32_t)r());
}

std::vector<Root> roots() {
    return {
        {"floorf", f_028F4114, hostrt::floorf_028F4114, [](Cpu& c, std::mt19937_64& r, uint64_t i) { f1_single(c, r, i, true); }, F1, true},
        {"ceilf", f_028F41B0, hostrt::ceilf_028F41B0, [](Cpu& c, std::mt19937_64& r, uint64_t i) { f1_single(c, r, i, true); }, F1, true},
        {"floor", f_028F3B98, hostrt::floor_028F3B98, [](Cpu& c, std::mt19937_64& r, uint64_t) { c.f[1].ps0 = u64_as_f64(r()); }, F1},
        {"cvt_u64_f", f_028F543C, hostrt::cvt_u64_f_028F543C, [](Cpu& c, std::mt19937_64& r, uint64_t) {
             uint64_t v = rnd64(r);
             c.r[3] = (uint32_t)(v >> 32), c.r[4] = (uint32_t)v;
         }, F1},
        {"umod64", f_028F597C, hostrt::umod64_028F597C, [](Cpu& c, std::mt19937_64& r, uint64_t) {
             uint64_t a = rnd64(r), b = rnd64(r);
             if ((r() & 15) == 0) b = 0;
             c.r[3] = (uint32_t)(a >> 32), c.r[4] = (uint32_t)a, c.r[5] = (uint32_t)(b >> 32), c.r[6] = (uint32_t)b;
         }, R3 | R4},
        {"cvt_f_s64", f_028F5680, hostrt::cvt_f_s64_028F5680, [](Cpu& c, std::mt19937_64& r, uint64_t i) { f1_single(c, r, i, true); }, R3 | R4, true},
        {"div", f_028F35A8, hostrt::div_028F35A8, [](Cpu& c, std::mt19937_64& r, uint64_t) {
             const uint32_t m = (uint32_t)(r() % 4);
             c.r[3] = m == 0 ? 0x80000000u : (uint32_t)r() >> (r() % 32);
             if (r() & 1) c.r[3] = 0 - c.r[3];
             c.r[4] = m == 1 ? 0 : m == 2 ? 0xFFFFFFFFu : (uint32_t)r() >> (r() % 32);
             if (r() & 1) c.r[4] = 0 - c.r[4];
         }, R3 | R4},
        {"swkbd_work_size", f_0286CD54, hostrt::work_size_0286CD54, [](Cpu& c, std::mt19937_64& r, uint64_t) { c.r[3] = (uint32_t)r(); }, R3},
        {"work_size", f_0286CBC4, hostrt::work_size_0286CBC4, [](Cpu&, std::mt19937_64&, uint64_t) {}, R3},
        {"empty", f_0286D5F0, hostrt::empty_0286D5F0, [](Cpu&, std::mt19937_64&, uint64_t) {}, 0},
        {"cvt_f_u64", f_028F5678, hostrt::cvt_f_u64_028F5678, [](Cpu& c, std::mt19937_64& r, uint64_t i) { f1_single(c, r, i, true); }, R3 | R4, true},
    };
}

// every register randomised, so a replacement that writes one it should keep is caught
void randomise(Cpu& c, std::mt19937_64& r, int slot) {
    reset(&c, slot);
    for (int i = 0; i < 32; i++)
        if (i != 1) c.r[i] = (uint32_t)r();
    for (int i = 0; i < 32; i++) c.f[i].ps0 = u64_as_f64(r()), c.f[i].ps1 = u64_as_f64(r());
    for (int i = 0; i < 32; i++) c.cr[i] = r() & 1;
    c.ctr = (uint32_t)r(), c.lr = 0x02000000u | ((uint32_t)r() & 0xFFFFFC);
    c.xer_ca = r() & 1, c.xer_so = r() & 1, c.xer_ov = r() & 1;
}

bool same_except_outputs(const Cpu& a, const Cpu& b) {
    for (int i = 0; i < 32; i++)
        if (i != 3 && i != 4 && a.r[i] != b.r[i]) return false;
    for (int i = 0; i < 32; i++)
        if (i != 1 && i != 2 && (bits(a.f[i].ps0) != bits(b.f[i].ps0) || bits(a.f[i].ps1) != bits(b.f[i].ps1))) return false;
    return !memcmp(a.cr, b.cr, sizeof a.cr) && a.ctr == b.ctr && a.lr == b.lr && a.xer_ca == b.xer_ca &&
           a.xer_so == b.xer_so && a.xer_ov == b.xer_ov && a.fpscr == b.fpscr;
}

bool same_outputs(const Cpu& a, const Cpu& b, int outs) {
    if ((outs & R3) && a.r[3] != b.r[3]) return false;
    if ((outs & R4) && a.r[4] != b.r[4]) return false;
    if ((outs & F1) && (bits(a.f[1].ps0) != bits(b.f[1].ps0) || bits(a.f[1].ps1) != bits(b.f[1].ps1))) return false;
    if ((outs & F2) && (bits(a.f[2].ps0) != bits(b.f[2].ps0) || bits(a.f[2].ps1) != bits(b.f[2].ps1))) return false;
    return true;
}

// a replacement keeps every register except the outputs a caller reads (those it sets exactly)
bool kept_except(const Cpu& e, const Cpu& b, int outs) {
    Cpu x = b;
    if (outs & R3) x.r[3] = e.r[3];
    if (outs & R4) x.r[4] = e.r[4];
    if (outs & F1) x.f[1] = e.f[1];
    if (outs & F2) x.f[2] = e.f[2];
    return same_except_outputs(e, x) && x.r[3] == e.r[3] && x.r[4] == e.r[4] && bits(x.f[1].ps0) == bits(e.f[1].ps0) &&
           bits(x.f[1].ps1) == bits(e.f[1].ps1) && bits(x.f[2].ps0) == bits(e.f[2].ps0) && bits(x.f[2].ps1) == bits(e.f[2].ps1);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: regtest IMAGE [ROOT|all] [count]\n");
        return 2;
    }
    load_image(argv[1]);
    const std::string which = argc > 2 ? argv[2] : "all";
    const uint64_t count = argc > 3 ? strtoull(argv[3], nullptr, 0) : 10000000ull;
    const int nth = test_threads();
    int bad_roots = 0;
    for (const Root& rt : roots()) {
        if (which != "all" && which != rt.name) continue;
        const uint64_t total = rt.exhaustive32 && count == 0 ? (1ull << 32) : (count ? count : 10000000ull);
        std::vector<uint64_t> fails(nth), lr_slot(nth), preserved(nth), outs(nth), stackw(nth);
        std::vector<std::thread> th;
        for (int k = 0; k < nth; k++)
            th.emplace_back([&, k] {
                std::mt19937_64 r(0x7E57000u + k);
                Cpu entry, a, b;
                CallLog log;
                t_log = &log;
                const uint32_t sp = stack_top(k);
                uint8_t window_a[0x100];
                for (uint64_t i = k; i < total; i += nth) {
                    randomise(entry, r, k);
                    rt.gen(entry, r, i);
                    memset(PPC_MEM_BASE + sp, 0xA5, 0x100);
                    a = entry;
                    rt.orig(&a);
                    memcpy(window_a, PPC_MEM_BASE + sp, 0x100);
                    memset(PPC_MEM_BASE + sp, 0xA5, 0x100);
                    b = entry;
                    rt.repl(&b);
                    bool ok = true;
                    if (!same_outputs(a, b, rt.outs)) outs[k]++, ok = false;
                    if (!kept_except(entry, b, rt.outs)) preserved[k]++, ok = false;
                    if (memcmp(window_a, PPC_MEM_BASE + sp, 0x100)) {
                        stackw[k]++, ok = false;
                        // the usual cause: the original saved LR in the caller's frame (SP+4)
                        uint32_t w;
                        memcpy(&w, window_a + 4, 4);
                        if (__builtin_bswap32(w) == entry.lr) lr_slot[k]++;
                    }
                    if (!ok && fails[k]++ < 3)
                        printf("  %s FAIL input r3=%08X r4=%08X r5=%08X r6=%08X f1=%016" PRIX64 ": orig r3 %08X r4 %08X f1 %016" PRIX64 "/%016" PRIX64
                               "  repl r3 %08X r4 %08X f1 %016" PRIX64 "/%016" PRIX64 "\n",
                               rt.name, entry.r[3], entry.r[4], entry.r[5], entry.r[6], bits(entry.f[1].ps0), a.r[3], a.r[4], bits(a.f[1].ps0),
                               bits(a.f[1].ps1), b.r[3], b.r[4], bits(b.f[1].ps0), bits(b.f[1].ps1));
                }
            });
        for (auto& t : th) t.join();
        uint64_t f = 0, o = 0, p = 0, s = 0, l = 0;
        for (int k = 0; k < nth; k++) f += fails[k], o += outs[k], p += preserved[k], s += stackw[k], l += lr_slot[k];
        printf("%-10s inputs %12" PRIu64 "  failures %" PRIu64 " (outputs %" PRIu64 ", registers kept %" PRIu64 ", caller stack %" PRIu64
               " of which LR save word %" PRIu64 ")%s\n",
               rt.name, total, f, o, p, s, l, f ? "" : "  OK");
        fflush(stdout);
        bad_roots += f != 0;
    }
    return bad_roots ? 1 : 0;
}
