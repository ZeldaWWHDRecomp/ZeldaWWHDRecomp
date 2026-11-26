// SPDX-License-Identifier: MPL-2.0
// Proof for the fast mode's native math (runtime/src/hostrt/hostrt_native_math.cpp, cmake
// WWHD_DECOMP_FAST_NATIVE_MATH): each native function runs next to its recompiled original on the same input.
//
// The original runs on a Cpu whose every other register is random per input (GPRs, both lanes of every FPR, f1.ps1
// = the argument for half the inputs, CR, LR, CTR, XER) and on a guest stack whose caller area is random. Compared:
//   - f1.ps0 after the original returns vs the native result (bits: NaN payloads and signed zeros included);
//   - the __gh_set_errno calls (count and code, r3) vs the native function's errno code; any other import or game
//     call fails the input;
//   - what the native version does not repeat must be dead for the caller: the original leaves r1, r2, r13..r31,
//     f14..f31, cr2..cr4, LR, FPSCR and the GQRs as they were, and writes nothing at or above the entry SP except the
//     caller's LR save word (SP+4). A saved FPR's ps1 may come back rounded through single format (GHS's save /
//     restore routines store it with stfs and load it with lfs; every decompiled function's original does the same
//     and its native body does not, so the decomp build already leaves such lanes as they are): counted, not failed.
// Any difference prints the input and the run exits 1.
//
// usage: nativemath IMAGE FUNC MODE [threads] [count] [checkpoint]
//   FUNC  sqrtf | sqrt | sin | cos | fmod
//   MODE  all32  every one of the 2^32 single-format words as the argument (lfs value; fmod: as x, y random)
//         rand   COUNT random inputs (raw 64-bit doubles, doubles of the singles' range, near multiples of pi/2,
//                near the function's constants; fmod: pairs of these and pairs with close exponents) + the edge grid
//         grid   the edge grid only (specials, subnormals, extremes, the constants +- 64 ulp; fmod: every pair)
//   CHECKPOINT: a file of finished chunks (2^24 inputs each); a rerun with the same file skips them
#include <mach/mach.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "hostrt_native_math.h"
#include "rt.h"

using namespace difftest;

extern "C" void f_028F4384(Cpu* c);
extern "C" void f_028F37F0(Cpu* c);
extern "C" void f_028F43F8(Cpu* c);
extern "C" void f_028F4BE0(Cpu* c);
extern "C" void f_028F4034(Cpu* c);

namespace {

struct Fn {
    const char* name;
    void (*orig)(Cpu*);
    int nargs;
    std::vector<uint32_t> consts32, consts64;  // guest addresses of the original's constants (edge values around them)
};
const Fn kFns[] = {
    {"sqrtf", f_028F4384, 1, {0x1018A848u, 0x1018A84Cu}, {}},
    {"sqrt", f_028F37F0, 1, {}, {0x1018A5C0u, 0x1018A5C8u, 0x1018A5D0u}},
    {"sin", f_028F43F8, 1, {0x1018A850u, 0x1018A854u, 0x1018A870u, 0x1018A86Cu}, {}},
    {"cos", f_028F4BE0, 1, {0x1018A94Cu, 0x1018A954u, 0x1018A96Cu}, {}},
    {"fmod", f_028F4034, 2, {0x1018A7E4u}, {}},
};

double native(const Fn& fn, double x, double y, uint32_t& err) {
    using namespace hostrt::nmath;
    switch (fn.name[1]) {
    case 'q': return fn.name[4] == 'f' ? sqrtf_n(x, err) : sqrt_n(x, err);
    case 'i': return sin_n(x);
    case 'o': return cos_n(x);
    default: return fmod_n(x, y, err);
    }
}

struct Rng {
    uint64_t s;
    uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
};

constexpr uint32_t kAbove = 0x40;

std::atomic<uint64_t> g_done{0}, g_bad{0}, g_errno{0}, g_lrsave{0}, g_ps1{0}, g_noreturn{0}, g_noreturn_unverified{0};
std::mutex g_mu;

// the edge grid: specials, subnormal / extreme singles and doubles, the constants +- 64 ulp (as doubles and as
// singles), multiples of pi/2 +- 64 ulp (single and double), small integers and halves
std::vector<double> edge_values(const Fn& fn) {
    std::vector<double> v;
    auto both = [&](double d) { v.push_back(d); v.push_back(-d); };
    const uint64_t dsp[] = {0, 1, 2, 0x000FFFFFFFFFFFFFull, 0x0010000000000000ull, 0x7FEFFFFFFFFFFFFFull,
                            0x7FF0000000000000ull, 0x7FF0000000000001ull, 0x7FF4000000000000ull, 0x7FF8000000000000ull,
                            0x7FFFFFFFFFFFFFFFull, 0x7FF8000000000001ull, 0x3FF0000000000000ull, 0x36A0000000000000ull,
                            0x3690000000000000ull, 0x3800000000000000ull, 0x380FFFFFFFFFFFFFull, 0x47EFFFFFE0000000ull,
                            0x47EFFFFFF0000000ull, 0x47F0000000000000ull};
    for (uint64_t b : dsp) both(u64_as_f64(b));
    const uint32_t ssp[] = {0, 1, 2, 0x007FFFFFu, 0x00800000u, 0x00800001u, 0x7F7FFFFFu, 0x7F800000u, 0x7F800001u,
                            0x7FC00000u, 0x7FFFFFFFu, 0x3F800000u, 0x3F000000u, 0x4B000000u, 0x4B800000u, 0x4F000000u,
                            0x4EFFFFFFu, 0x4F800000u, 0x34000000u, 0x33800000u, 0x39800000u};
    for (uint32_t b : ssp) both(ppc_lfs_value(b));
    for (int i = -40; i <= 40; i++) both(i), both(i + 0.5), both(std::ldexp(1.0, i * 3));
    auto around64 = [&](double c) {
        if (!std::isfinite(c)) return;
        uint64_t b = f64_as_u64(std::fabs(c));
        for (int d = -64; d <= 64; d++) both(u64_as_f64(b + (int64_t)d));
        uint32_t s = f32_as_u32((float)std::fabs(c));
        for (int d = -64; d <= 64; d++) both(ppc_lfs_value(s + (int32_t)d));
    };
    for (uint32_t a : fn.consts32) around64(ldf32(a));
    for (uint32_t a : fn.consts64) around64(ldf64(a));
    if (fn.nargs == 1) {  // (fmod pairs every grid value with every other: no pi multiples there)
        for (int k = 1; k <= 200; k++) around64(k * M_PI_2);
        for (int k = 1; k <= 64; k++) around64(std::ldexp(M_PI_2, k * 2));
    } else {
        for (double c : {360.0, 2 * M_PI, 65536.0, 1.0}) around64(c);
    }
    return v;
}

// one random argument for MODE rand: raw bits / a single / a double in the singles' range / near k*pi/2
double rand_arg(Rng& r) {
    const uint64_t u = r.next();
    switch (u & 7) {
    case 0: case 1: return u64_as_f64(r.next());
    case 2: case 3: return ppc_lfs_value((uint32_t)r.next());
    case 4: {  // a double of exponent -40..40, either sign
        const uint64_t m = r.next();
        const uint64_t e = (uint64_t)(1023 - 40 + (int)((m >> 52) % 81));
        return u64_as_f64((m & 0x800FFFFFFFFFFFFFull) | e << 52);
    }
    case 5: {  // a single of exponent -40..40
        const uint32_t m = (uint32_t)r.next();
        const uint32_t e = (uint32_t)(127 - 40 + (int)((m >> 23) % 81));
        return ppc_lfs_value((m & 0x807FFFFFu) | e << 23);
    }
    default: {  // k*pi/2 (k up to 2^20) +- up to 2^20 ulp, as a double or rounded to single
        const uint64_t m = r.next();
        const double k = (double)(int64_t)(m % (2u << 20)) - (double)(1u << 20);
        double d = u64_as_f64(f64_as_u64(k * M_PI_2) + (int64_t)((m >> 32) % (2u << 20)) - (int64_t)(1u << 20));
        return (m >> 63) ? (double)(float)d : d;
    }
    }
}

// one random fmod pair: independent arguments, or y and x with close exponents, or x near a multiple of y
void rand_pair(Rng& r, double& x, double& y) {
    const uint64_t u = r.next();
    switch (u & 3) {
    case 0: x = rand_arg(r), y = rand_arg(r); return;
    case 1: {  // singles, |x| / |y| within 2^-4 .. 2^40
        const uint32_t a = (uint32_t)r.next(), b = (uint32_t)r.next();
        const uint32_t ey = 1 + (uint32_t)((b >> 23) % 200);
        const uint32_t ex = (uint32_t)std::min<int64_t>(254, std::max<int64_t>(1, (int64_t)ey - 4 + (int64_t)((a >> 23) % 45)));
        x = ppc_lfs_value((a & 0x807FFFFFu) | ex << 23), y = ppc_lfs_value((b & 0x807FFFFFu) | ey << 23);
        return;
    }
    case 2: {  // x = k*y +- a few ulp (singles)
        y = ppc_lfs_value((uint32_t)r.next() & 0xBFFFFFFFu);
        const double k = (double)(int64_t)(r.next() % 100000) - 50000.0;
        const float xf = (float)(k * y);
        x = ppc_lfs_value(f32_as_u32(xf) + (int32_t)(r.next() % 9) - 4);
        return;
    }
    default: {  // the game's typical use: an angle (degrees / radians / s16 range) mod 360, 2pi, 65536
        const double ys[] = {360.0, 2 * M_PI, 65536.0, 1.0, 256.0, 2.0, 32768.0, (float)(2 * M_PI)};
        y = (double)(float)ys[r.next() % 8];
        x = (double)(float)((double)(int64_t)(r.next() % 4000000) / 7.0 - 285714.0);
        return;
    }
    }
}

bool check_one(const Fn& fn, double x, double y, Rng& r, int slot, std::string& why, bool& had_errno) {
    bool ps1_round = false;
    Cpu init;
    reset(&init, slot);
    const uint32_t sp = init.r[1];
    for (int i = 0; i < 32; i++) if (i != 1) init.r[i] = (uint32_t)r.next();
    for (int i = 0; i < 32; i++) init.f[i].ps0 = u64_as_f64(r.next()), init.f[i].ps1 = u64_as_f64(r.next());
    const uint64_t crb = r.next();
    for (int i = 0; i < 32; i++) init.cr[i] = (crb >> i) & 1;
    init.lr = (uint32_t)r.next() & ~3u;
    init.ctr = (uint32_t)r.next();
    init.xer_so = crb >> 40 & 1, init.xer_ov = crb >> 41 & 1, init.xer_ca = crb >> 42 & 1;
    init.fpscr = 0;  // round to nearest: the game's mode (the recompiled code reads FPSCR only in fctiw)
    for (int i = 0; i < 8; i++) init.gqr[i] = (uint32_t)r.next();
    init.f[1].ps0 = x;
    init.f[1].ps1 = (crb >> 50 & 1) ? x : u64_as_f64(r.next());
    init.f[2].ps0 = y;
    if (fn.nargs == 2) init.f[2].ps1 = (crb >> 51 & 1) ? y : init.f[2].ps1;
    uint8_t stk[kAbove];
    for (auto& b : stk) b = (uint8_t)r.next();
    memcpy(PPC_MEM_BASE + sp, stk, kAbove);
    uint32_t err = 0;
    hostrt::nmath::g_fmod_capped = false;
    const double n = native(fn, x, y, err);
    Cpu a = init;
    CallLog log;
    t_log = &log;
    t_entry_sp = sp;
    char m[200];
    if (hostrt::nmath::g_fmod_capped) {
        // the native loop did not end: the original must not return either. It makes an errno call per round
        // (ldexpf's overflow); after 10000 of them it is stopped (longjmp out of its loop). Without errno calls the
        // input is not run (counted: unverified).
        if (!err) { t_log = nullptr; g_noreturn_unverified++; return true; }
        jmp_buf jb;
        t_import_jmp = &jb, t_import_limit = 10000;
        if (setjmp(jb)) {
            t_import_jmp = nullptr, t_log = nullptr;
            g_noreturn++;
            return true;
        }
        fn.orig(&a);
        t_import_jmp = nullptr, t_log = nullptr;
        why = "the native loop does not end, the original returned";
        return false;
    }
    fn.orig(&a);
    t_log = nullptr;
    if (f64_as_u64(a.f[1].ps0) != f64_as_u64(n)) {
        snprintf(m, sizeof m, "f1 %016" PRIX64 " (original) vs %016" PRIX64 " (native)", f64_as_u64(a.f[1].ps0), f64_as_u64(n));
        why = m;
        return false;
    }
    uint32_t oerr = 0;
    int ne = 0;
    for (const Call& k : log) {
        if (!k.import || strcmp(k.import, "coreinit___gh_set_errno")) { why = "a call other than __gh_set_errno"; return false; }
        oerr = k.r[3];
        ne++;
    }
    had_errno = ne > 0;
    if (ne > 1 || oerr != err) {
        snprintf(m, sizeof m, "errno: original %d call(s), code %X; native code %X", ne, oerr, err);
        why = m;
        return false;
    }
    for (int i : {1, 2}) if (a.r[i] != init.r[i]) { snprintf(m, sizeof m, "r%d changed", i); why = m; return false; }
    for (int i = 13; i < 32; i++) if (a.r[i] != init.r[i]) { snprintf(m, sizeof m, "r%d changed", i); why = m; return false; }
    for (int i = 14; i < 32; i++) {
        // ps1 of a saved FPR may come back as GHS's save / restore routines leave it: stored with stfs and loaded
        // with lfs (rounded through single format). Every decompiled function's original saves its FPRs that way
        // and its native body does not, so the decomp build already leaves such lanes unrounded (counted only).
        const uint64_t p1 = f64_as_u64(a.f[i].ps1), i1 = f64_as_u64(init.f[i].ps1);
        const bool ps1_ok = p1 == i1 || p1 == f64_as_u64(ppc_lfs_value(ppc_stfs_bits(init.f[i].ps1)));
        if (f64_as_u64(a.f[i].ps0) != f64_as_u64(init.f[i].ps0) || !ps1_ok) {
            snprintf(m, sizeof m, "f%d changed", i);
            why = m;
            return false;
        }
        if (p1 != i1) ps1_round = true;
    }
    for (int i = 8; i < 20; i++) if (a.cr[i] != init.cr[i]) { snprintf(m, sizeof m, "cr bit %d changed", i); why = m; return false; }
    if (a.lr != init.lr) { why = "lr changed"; return false; }
    if (a.fpscr != init.fpscr) { why = "fpscr changed"; return false; }
    if (memcmp(a.gqr, init.gqr, sizeof a.gqr)) { why = "gqr changed"; return false; }
    for (uint32_t o = 0; o < kAbove; o++) {
        if (o >= 4 && o < 8) continue;
        if (PPC_MEM_BASE[sp + o] != stk[o]) { snprintf(m, sizeof m, "caller stack +%X written", o); why = m; return false; }
    }
    if (memcmp(PPC_MEM_BASE + sp + 4, stk + 4, 4)) g_lrsave++;
    if (ps1_round) g_ps1++;
    return true;
}

void report(const Fn& fn, double x, double y, uint64_t n, const std::string& why) {
    std::lock_guard<std::mutex> l(g_mu);
    if (g_bad++ < 20) {
        if (fn.nargs == 2)
            fprintf(stderr, "%s: x %016" PRIX64 " y %016" PRIX64 " (input %" PRIu64 "): %s\n", fn.name, f64_as_u64(x), f64_as_u64(y), n, why.c_str());
        else
            fprintf(stderr, "%s: x %016" PRIX64 " (input %" PRIu64 "): %s\n", fn.name, f64_as_u64(x), n, why.c_str());
    }
}

constexpr uint64_t kChunk = 1ull << 24;

}  // namespace

extern "C" void hostrt_test_import(Cpu* c, const char* name);

// In-process memory cap (Darwin ignores ulimit -v): a thread polls the footprint every 20 ms and stops the process
// at NATIVEMATH_CAP_MB (default 2048; 0 = off, used only to test the external watchdog in capped.sh).
static void start_self_cap() {
    const char* e = getenv("NATIVEMATH_CAP_MB");
    const uint64_t cap_mb = e ? strtoull(e, nullptr, 10) : 2048;
    if (!cap_mb) return;
    std::thread([cap_mb] {
        for (;;) {
            task_vm_info_data_t vi;
            mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
            if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vi, &n) == KERN_SUCCESS &&
                vi.phys_footprint > cap_mb << 20) {
                fprintf(stderr, "nativemath: memory %llu MB over the %llu MB cap, stopping\n",
                        (unsigned long long)(vi.phys_footprint >> 20), (unsigned long long)cap_mb);
                _exit(4);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }).detach();
}

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: nativemath IMAGE FUNC all32|rand|grid [threads] [count] [checkpoint]\n"); return 2; }
    // memory-cap self tests (run before long runs): NATIVEMATH_RUNAWAY=mem grows RSS without end (the external
    // watchdog must kill it), =log makes an endless call log (the in-process hard cap must stop it)
    start_self_cap();
    if (const char* rw = getenv("NATIVEMATH_RUNAWAY")) {
        if (!strcmp(rw, "mem")) {
            for (std::vector<char*> keep;;) { char* p = (char*)malloc(64 << 20); memset(p, 1, 64 << 20); usleep(50000); __asm__ volatile("" ::"r"(p) : "memory"); keep.push_back(p); }
        }
    }
    load_image(argv[1]);
    if (const char* rw = getenv("NATIVEMATH_RUNAWAY"); rw && !strcmp(rw, "log")) {
        CallLog log;
        t_log = &log;
        Cpu c{};
        c.r[1] = 0x10000000u;
        for (;;) hostrt_test_import(&c, "coreinit___gh_set_errno");
    }
    const Fn* fp = nullptr;
    for (const Fn& f : kFns) if (!strcmp(f.name, argv[2])) fp = &f;
    if (!fp) { fprintf(stderr, "unknown function %s\n", argv[2]); return 2; }
    const Fn& fn = *fp;
    const std::string mode = argv[3];
    const int threads = argc > 4 ? atoi(argv[4]) : 4;
    const uint64_t count = argc > 5 ? strtoull(argv[5], nullptr, 10) : 0;
    const char* ckpt = argc > 6 ? argv[6] : nullptr;

    // the edge grid first (every mode but all32): one argument each, or every pair for fmod
    const std::vector<double> edge = edge_values(fn);
    uint64_t grid_n = 0;
    if (mode != "all32") {
        Rng r{0xED6E0000ull};
        uint64_t k = 0;
        for (size_t i = 0; i < edge.size(); i++) {
            for (size_t j = 0; j < (fn.nargs == 2 ? edge.size() : 1); j++, k++) {
                const double x = edge[i], y = fn.nargs == 2 ? edge[j] : 0.0;
                std::string why;
                bool had = false;
                if (!check_one(fn, x, y, r, 0, why, had)) report(fn, x, y, k, why);
                if (had) g_errno++;
            }
        }
        grid_n = k;
        fprintf(stderr, "%s: edge grid %" PRIu64 " inputs, %" PRIu64 " differ\n", fn.name, grid_n, g_bad.load());
    }
    const uint64_t total = mode == "all32" ? (1ull << 32) : mode == "rand" ? count : 0;
    const uint64_t nchunks = (total + kChunk - 1) / kChunk;
    std::set<uint64_t> done;
    uint64_t prev_errno = 0, prev_lr = 0;
    if (ckpt) {
        if (FILE* f = fopen(ckpt, "r")) {
            char line[256];
            while (fgets(line, sizeof line, f)) {
                char fname[32], fmode[16];
                unsigned long long ch, e, lr, bad;
                if (sscanf(line, "%31s %15s chunk %llu errno %llu lrsave %llu bad %llu", fname, fmode, &ch, &e, &lr, &bad) == 6 &&
                    fname == std::string(fn.name) && fmode == mode && bad == 0 && !done.count(ch))
                    done.insert(ch), prev_errno += e, prev_lr += lr;
            }
            fclose(f);
        }
        if (!done.empty()) fprintf(stderr, "%s: %zu of %" PRIu64 " chunks done before (checkpoint)\n", fn.name, done.size(), nchunks);
    }
    std::atomic<uint64_t> next{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < threads; t++)
        ts.emplace_back([&, t] {
            for (;;) {
                const uint64_t ch = next++;
                if (ch >= nchunks || g_bad) return;
                if (done.count(ch)) continue;
                Rng r{0x5EED0000ull + ch * 0x10001ull};
                const uint64_t lo = ch * kChunk, hi = std::min(total, lo + kChunk);
                uint64_t ne = 0;
                const uint64_t lr0 = g_lrsave.load();
                for (uint64_t n = lo; n < hi; n++) {
                    double x, y = 0.0;
                    if (mode == "all32") {
                        x = ppc_lfs_value((uint32_t)n);
                        if (fn.nargs == 2) y = rand_arg(r);
                    } else if (fn.nargs == 2) {
                        rand_pair(r, x, y);
                    } else {
                        x = rand_arg(r);
                    }
                    std::string why;
                    bool had = false;
                    if (!check_one(fn, x, y, r, t, why, had)) report(fn, x, y, n, why);
                    if (had) ne++;
                }
                g_errno += ne;
                g_done += hi - lo;
                if (ckpt && !g_bad) {
                    std::lock_guard<std::mutex> l(g_mu);
                    if (FILE* f = fopen(ckpt, "a")) {
                        // (lrsave per chunk is approximate under threads; the total is printed at the end)
                        fprintf(f, "%s %s chunk %" PRIu64 " errno %" PRIu64 " lrsave %" PRIu64 " bad 0\n", fn.name, mode.c_str(), ch, ne,
                                g_lrsave.load() - lr0);
                        fclose(f);
                    }
                }
            }
        });
    std::atomic<bool> stop{false};
    std::thread progress([&] {
        int s = 0;
        while (!stop) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (++s % 120 == 0 && total)
                fprintf(stderr, "%s %s: %.1f%% of the remaining inputs (%" PRIu64 " differ)\n", fn.name, mode.c_str(),
                        100.0 * g_done / (double)(total - done.size() * kChunk + 1), g_bad.load());
        }
    });
    for (auto& t : ts) t.join();
    stop = true;
    progress.join();
    const uint64_t n_all = grid_n + total;
    printf("%s %s: %" PRIu64 " inputs (%s%s), %" PRIu64 " with an errno call, %" PRIu64 " differ -> %s"
           " (this run: caller's LR save word written %" PRIu64 "x, a saved FPR's ps1 rounded by GHS's save/restore %" PRIu64 "x;"
           " original and native both never return %" PRIu64 "x, unverified %" PRIu64 "x)\n",
           fn.name, mode.c_str(), n_all,
           mode == "all32" ? "all 2^32 single-format arguments" : mode == "rand" ? "random + edge grid" : "edge grid",
           fn.nargs == 2 && mode == "all32" ? ", y random" : "", g_errno.load() + prev_errno, g_bad.load(), g_bad ? "FAIL" : "EXACT",
           g_lrsave.load(), g_ps1.load(), g_noreturn.load(), g_noreturn_unverified.load());
    return g_bad || g_noreturn_unverified ? 1 : 0;
}
