// SPDX-License-Identifier: MPL-2.0
// Exhaustive proof for the fast mode's libm ports (runtime/src/hostrt/hostrt_libm.cpp, cmake WWHD_DECOMP_FAST_LIBM):
// each port runs next to its recompiled original on every one of the 2^32 single-precision inputs (f1 = the lfs
// value of the bit pattern, as callers pass a float), and on COUNT random double inputs plus an edge set for a
// double function. Every other register is randomised per input (both lanes of every FPR, f1.ps1 included; GPRs;
// CR; LR); both runs start from that same state and the same guest stack contents. Compared after the call: all 32
// GPRs, both lanes of all 32 FPRs (bits), CR, LR, CTR, XER, FPSCR, the import calls (name and registers) and the
// guest stack from entry SP - 0x100 to entry SP + 0x40. Any difference prints the input and exits 1.
//
// usage: libmport IMAGE FUNC [threads] [count]   FUNC: sqrtf; count 0 (float functions): all 2^32 inputs
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "rt.h"

using namespace difftest;

extern "C" void f_028F4384(Cpu* c);
namespace hostrt { void sqrtf_028F4384(Cpu* c); }

namespace {

struct Fn { const char* name; void (*orig)(Cpu*); void (*port)(Cpu*); bool single; };
const Fn kFns[] = {{"sqrtf", f_028F4384, hostrt::sqrtf_028F4384, true}};

constexpr uint32_t kBelow = 0x100, kAbove = 0x40;

std::atomic<uint64_t> g_done{0}, g_bad{0}, g_errno{0};

bool same_state(const Cpu& a, const Cpu& b, std::string& why) {
    char m[160];
    for (int i = 0; i < 32; i++)
        if (a.r[i] != b.r[i]) { snprintf(m, sizeof m, "r%d %08X vs %08X", i, a.r[i], b.r[i]); why = m; return false; }
    for (int i = 0; i < 32; i++) {
        if (bits(a.f[i].ps0) != bits(b.f[i].ps0)) { snprintf(m, sizeof m, "f%d.ps0 %016" PRIX64 " vs %016" PRIX64, i, bits(a.f[i].ps0), bits(b.f[i].ps0)); why = m; return false; }
        if (bits(a.f[i].ps1) != bits(b.f[i].ps1)) { snprintf(m, sizeof m, "f%d.ps1 %016" PRIX64 " vs %016" PRIX64, i, bits(a.f[i].ps1), bits(b.f[i].ps1)); why = m; return false; }
    }
    if (memcmp(a.cr, b.cr, sizeof a.cr)) { why = "cr"; return false; }
    if (a.lr != b.lr) { snprintf(m, sizeof m, "lr %08X vs %08X", a.lr, b.lr); why = m; return false; }
    if (a.ctr != b.ctr) { why = "ctr"; return false; }
    if (memcmp(&a.xer_so, &b.xer_so, sizeof a.xer_so) || a.xer_ca != b.xer_ca || a.xer_ov != b.xer_ov) { why = "xer"; return false; }
    if (a.fpscr != b.fpscr) { why = "fpscr"; return false; }
    return true;
}

bool same_calls(const CallLog& a, const CallLog& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i].addr != b[i].addr || strcmp(a[i].import ? a[i].import : "", b[i].import ? b[i].import : "") ||
            memcmp(a[i].r, b[i].r, sizeof a[i].r) || memcmp(a[i].f, b[i].f, sizeof a[i].f) || a[i].lr != b[i].lr ||
            memcmp(a[i].stack, b[i].stack, sizeof a[i].stack) || a[i].back != b[i].back)
            return false;
    return true;
}

void worker(const Fn& fn, int slot, uint64_t lo, uint64_t hi, bool exhaustive, uint64_t seed) {
    std::mt19937_64 rng(seed);
    Cpu init, a, b;
    CallLog la, lb;
    std::vector<uint8_t> stk(kBelow + kAbove), sa(kBelow + kAbove), sb(kBelow + kAbove);
    for (uint64_t n = lo; n < hi; n++) {
        reset(&init, slot);
        const uint32_t sp = init.r[1];
        for (int i = 0; i < 32; i++) if (i != 1) init.r[i] = (uint32_t)rng();
        for (int i = 0; i < 32; i++) init.f[i].ps0 = u64_as_f64(rng()), init.f[i].ps1 = u64_as_f64(rng());
        for (int i = 0; i < 32; i++) init.cr[i] = rng() & 1;
        init.lr = (uint32_t)rng() & ~3u;
        double x;
        if (exhaustive) x = ppc_lfs_value((uint32_t)n);
        else x = u64_as_f64(rng());
        init.f[1].ps0 = x;
        if (n & 1) init.f[1].ps1 = x;  // half the inputs as a frsp result (both lanes), half with another ps1
        for (auto& v : stk) v = (uint8_t)rng();
        memcpy(PPC_MEM_BASE + sp - kBelow, stk.data(), stk.size());
        a = init; la.clear(); t_log = &la; t_entry_sp = sp;
        fn.orig(&a);
        memcpy(sa.data(), PPC_MEM_BASE + sp - kBelow, sa.size());
        memcpy(PPC_MEM_BASE + sp - kBelow, stk.data(), stk.size());
        b = init; lb.clear(); t_log = &lb;
        fn.port(&b);
        memcpy(sb.data(), PPC_MEM_BASE + sp - kBelow, sb.size());
        t_log = nullptr;
        std::string why;
        bool ok = same_state(a, b, why);
        if (ok && !same_calls(la, lb)) ok = false, why = "import calls";
        if (ok && sa != sb) ok = false, why = "guest stack";
        if (!la.empty()) g_errno++;
        if (!ok) {
            if (g_bad++ < 10)
                fprintf(stderr, "%s: input %016" PRIX64 " (n %" PRIu64 "): %s\n", fn.name, bits(x), n, why.c_str());
        }
        if (((n - lo) & 0xFFFFF) == 0xFFFFF) g_done += 0x100000;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: libmport IMAGE FUNC [threads] [count]\n"); return 2; }
    load_image(argv[1]);
    const Fn* fn = nullptr;
    for (const Fn& f : kFns) if (!strcmp(f.name, argv[2])) fn = &f;
    if (!fn) { fprintf(stderr, "unknown function %s\n", argv[2]); return 2; }
    const int threads = argc > 3 ? atoi(argv[3]) : 8;
    const uint64_t count = argc > 4 ? strtoull(argv[4], nullptr, 10) : 0;
    const bool exhaustive = fn->single && count == 0;
    const uint64_t total = exhaustive ? (1ull << 32) : count;
    std::vector<std::thread> ts;
    for (int t = 0; t < threads; t++)
        ts.emplace_back(worker, std::cref(*fn), t, total * t / threads, total * (t + 1) / threads, exhaustive, 0x5EED0000ull + t);
    std::thread progress([&] {
        while (g_done < total - (uint64_t)threads * 0x100000 && g_bad == 0) {
            std::this_thread::sleep_for(std::chrono::seconds(60));
            fprintf(stderr, "%s: %.1f%% (%" PRIu64 " differ)\n", fn->name, 100.0 * g_done / total, g_bad.load());
        }
    });
    for (auto& t : ts) t.join();
    progress.detach();
    printf("%s: %" PRIu64 " inputs (%s), %" PRIu64 " with an import call, %" PRIu64 " differ -> %s\n", fn->name, total,
           exhaustive ? "all 2^32 single inputs" : "random doubles + edge set", g_errno.load(), g_bad.load(),
           g_bad ? "FAIL" : "EXACT");
    return g_bad ? 1 : 0;
}
