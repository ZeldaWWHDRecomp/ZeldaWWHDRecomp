// SPDX-License-Identifier: MPL-2.0
// Bit-exactness survey of the GHS compiler helpers the game calls: float -> 64-bit integer conversions,
// u64 -> float, and 64-bit division / remainder. Each candidate is plain C/C++ arithmetic with the
// out-of-range and divide-by-zero results stated explicitly (measured); it may replace the original only
// if every input gives the same r3/r4 (or f1, both lanes) bit for bit.
//
// usage: abisurvey IMAGE FUNC [threads] [count]   (float inputs: all 2^32; integer inputs: count random
//        plus edge values)
#include <atomic>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <random>
#include <thread>
#include <vector>

#include "hostrt_impl.h"
#include "rt.h"

using namespace difftest;

extern "C" {
void f_028F5680(Cpu* c);  // float -> s64
void f_028F5678(Cpu* c);  // float -> u64
void f_028F543C(Cpu* c);  // u64 -> float
void f_028F597C(Cpu* c);  // u64 % u64
void f_028F5A1C(Cpu* c);  // u64 / u64
void f_028F5B8C(Cpu* c);  // s64 / s64
}

namespace {

constexpr uint64_t kMarker = 0x7FF7A5A5A5A5A5A5ull;

// candidates (the replacement's arithmetic, runtime/src/hostrt/hostrt_abi.cpp): the magnitude is
// converted (a NaN counting as infinite) and the sign bit applied afterwards (measured)
uint64_t cvt_f_s64(float x) { return hostrt::cvt_f_s64_value(x); }
uint64_t cvt_f_u64(float x) { return hostrt::cvt_f_u64_value(x); }

struct Stat {
    std::atomic<uint64_t> n{0}, differ{0};
    std::atomic<int> shown{0};
};

void report(const char* name, Stat& s) {
    printf("%-14s inputs %12" PRIu64 "  differ %12" PRIu64 "%s\n", name, s.n.load(), s.differ.load(), s.differ ? "" : "  EXACT");
    fflush(stdout);
}

void miss(Stat& s, const char* what, uint64_t a, uint64_t b, uint64_t got, uint64_t want) {
    s.differ++;
    if (s.shown++ < 6)
        printf("    %s a=%016" PRIX64 " b=%016" PRIX64 " original=%016" PRIX64 " candidate=%016" PRIX64 "\n", what, a, b, got, want);
}

template <class F>
void parallel(int nth, uint64_t total, F f) {
    std::vector<std::thread> th;
    for (int k = 0; k < nth; k++)
        th.emplace_back([&, k] {
            Cpu c;
            std::mt19937_64 rng(0xAB1000u + k);
            for (uint64_t i = k; i < total; i += nth) f(c, k, i, rng);
        });
    for (auto& t : th) t.join();
}

std::vector<uint64_t> int_edges() {
    std::vector<uint64_t> v;
    for (int s = 0; s < 64; s++)
        for (int64_t d = -2; d <= 2; d++) v.push_back((1ull << s) + d), v.push_back(~((1ull << s) + d));
    for (uint64_t x : {0ull, 1ull, 2ull, 3ull, 7ull, 10ull, 0xFFFFFFFFull, 0x100000000ull, 0x7FFFFFFFFFFFFFFFull,
                       0x8000000000000000ull, 0xFFFFFFFFFFFFFFFFull})
        v.push_back(x);
    return v;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: abisurvey IMAGE FUNC [threads] [count]\n");
        return 2;
    }
    load_image(argv[1]);
    const std::string f = argv[2];
    const int nth = argc > 3 ? atoi(argv[3]) : 4;
    const uint64_t count = argc > 4 ? strtoull(argv[4], nullptr, 0) : 100000000ull;
    const bool all = f == "all";

    for (auto [name, orig, cand] : {std::tuple{"cvt_f_s64", &f_028F5680, &cvt_f_s64}, std::tuple{"cvt_f_u64", &f_028F5678, &cvt_f_u64}}) {
        if (!all && f != name) continue;
        Stat s;
        parallel(nth, 1ull << 32, [&](Cpu& c, int k, uint64_t i, std::mt19937_64&) {
            reset(&c, k);
            c.f[1].ps0 = ppc_lfs_value((uint32_t)i);
            orig(&c);
            const uint64_t got = (uint64_t)c.r[3] << 32 | c.r[4];
            const uint64_t want = cand(u32_as_f32((uint32_t)i));
            s.n++;
            if (got != want) miss(s, "x", i, 0, got, want);
        });
        report(name, s);
    }
    if (all || f == "cvt_u64_f") {
        // u64 -> float: the FPR gets the float (both lanes); candidates: direct, and through double
        Stat direct, via_double;
        auto e = int_edges();
        parallel(nth, count + e.size(), [&](Cpu& c, int k, uint64_t i, std::mt19937_64& rng) {
            uint64_t v = i < e.size() ? e[i] : rng() >> (rng() & 63);
            reset(&c, k);
            c.f[1].ps1 = u64_as_f64(kMarker);
            c.r[3] = (uint32_t)(v >> 32), c.r[4] = (uint32_t)v;
            f_028F543C(&c);
            const double w1 = (double)(float)v, w2 = (double)(float)(double)v;
            direct.n++, via_double.n++;
            if (bits(c.f[1].ps0) != bits(w1) || bits(c.f[1].ps1) != bits(w1)) miss(direct, "v", v, 0, bits(c.f[1].ps0), bits(w1));
            if (bits(c.f[1].ps0) != bits(w2) || bits(c.f[1].ps1) != bits(w2)) miss(via_double, "v", v, 0, bits(c.f[1].ps0), bits(w2));
        });
        report("cvt_u64_f", direct);
        report("cvt_u64_f(dbl)", via_double);
    }
    for (auto [name, orig] : {std::pair<const char*, void (*)(Cpu*)>{"umod64", &f_028F597C}, {"udiv64", &f_028F5A1C}, {"sdiv64", &f_028F5B8C}}) {
        if (!all && f != name) continue;
        Stat s;
        auto e = int_edges();
        const uint64_t ne = e.size() * e.size();
        parallel(nth, count + ne, [&](Cpu& c, int k, uint64_t i, std::mt19937_64& rng) {
            uint64_t a, b;
            if (i < ne) a = e[i / e.size()], b = e[i % e.size()];
            else a = rng() >> (rng() & 63), b = rng() >> (rng() & 63);
            reset(&c, k);
            c.r[3] = (uint32_t)(a >> 32), c.r[4] = (uint32_t)a, c.r[5] = (uint32_t)(b >> 32), c.r[6] = (uint32_t)b;
            orig(&c);
            const uint64_t got = (uint64_t)c.r[3] << 32 | c.r[4];
            uint64_t want;
            if (!strcmp(name, "umod64")) want = b ? a % b : a;
            else if (!strcmp(name, "udiv64")) want = b ? a / b : 0xFFFFFFFFFFFFFFFFull;
            else {
                const int64_t sa = (int64_t)a, sb = (int64_t)b;
                want = !sb ? (sa < 0 ? 1ull : 0xFFFFFFFFFFFFFFFFull)
                       : (sa == INT64_MIN && sb == -1) ? (uint64_t)INT64_MIN : (uint64_t)(sa / sb);
            }
            s.n++;
            if (got != want) miss(s, "", a, b, got, want);
        });
        report(name, s);
    }
    return 0;
}
