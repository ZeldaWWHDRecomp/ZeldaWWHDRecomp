// SPDX-License-Identifier: MPL-2.0
// Bit-exactness survey of the game's math library (task: host-runtime phase 2). Each recompiled original
// is run on a full or large input set next to candidate host functions; a candidate may replace the
// original only if every result is identical bit for bit (NaN payloads included).
//
// usage: mathsurvey IMAGE FUNC [threads] [count]
//   FUNC: a name from the table below (or all); `count` random inputs (default 1e8) plus the structured
//   edge set; count 0 runs every one of the 2^32 inputs of a one-argument float function.
#include <atomic>
#include <cfenv>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "rt.h"

using namespace difftest;

extern "C" {
void f_028F4384(Cpu* c);  // sqrtf
void f_028F37F0(Cpu* c);  // sqrt
void f_028F43F8(Cpu* c);  // sinf
void f_028F4BE0(Cpu* c);  // cosf
void f_028F423C(Cpu* c);  // tanf~
void f_028F4114(Cpu* c);  // floorf~
void f_028F41B0(Cpu* c);  // ceilf~
void f_028F3F90(Cpu* c);  // modff~/truncf~
void f_028F4DC4(Cpu* c);  // atanf kernel~
void f_028F4F20(Cpu* c);  // asinf~
void f_028F4FAC(Cpu* c);  // acosf
void f_028F4034(Cpu* c);  // fmodf~
void f_028F4560(Cpu* c);  // powf~
void f_028F4D28(Cpu* c);  // atan2f
void f_028F38F8(Cpu* c);  // sin~
void f_028F3C78(Cpu* c);  // cos~
void f_028F3CA0(Cpu* c);  // atan~
void f_028F3EC0(Cpu* c);  // asin~/acos~
void f_028F3B98(Cpu* c);  // floor~/ceil~
void f_028F3AB8(Cpu* c);  // fmod~
}

namespace {

enum Kind { F1, F2, D1, D2 };

struct Cand {
    const char* name;
    std::function<double(double, double)> fn;  // arguments and result as the FPR doubles
    // optional: the candidate's f1.ps1 after the call (x, result, f1.ps1 on entry); then ps1 is compared too
    std::function<uint64_t(double, double, uint64_t)> lane = nullptr;
};

constexpr uint64_t kLaneMarker = 0x7FF7A5A5A5A5A5A5ull;
// floorf/ceilf (measured): ps1 is left alone for |x| >= 2^23, infinities, NaNs and zero (floorf: both
// zeros, ceilf: +0 only); otherwise it gets the result
template <bool kNegZeroToo>
uint64_t lane_round_f(double x, double h, uint64_t ps1) {
    uint32_t b = f32_as_u32((float)x);
    if (std::isnan(x)) b = 0x7F800001u;  // any NaN: exponent field all ones
    const uint32_t e = (b >> 23) & 0xFF;
    const bool zero = kNegZeroToo ? (x == 0) : bits(x) == 0;
    return (e >= 150 || zero) ? ps1 : bits(h);
}

struct Test {
    const char* name;
    void (*orig)(Cpu*);
    Kind kind;
    std::vector<Cand> cands;
};

// a float result as the FPR holds it after frsp / a single-precision instruction
double fr(float v) { return (double)v; }
float fa(double d) { return (float)d; }

#define F1C(n, e) Cand{n, [](double x, double) { return fr(e(fa(x))); }}
#define D1C(n, e) Cand{n, [](double x, double) { return e(x); }}
// the same, with a NaN argument returned unchanged (a signalling NaN is not quieted)
#define F1N(n, e) Cand{n "+nan-unchanged", [](double x, double) { return std::isnan(x) ? x : fr(e(fa(x))); }}
#define F1L(n, e, z) Cand{n "+nan-unchanged+lane", [](double x, double) { return std::isnan(x) ? x : fr(e(fa(x))); }, lane_round_f<z>}
#define D1N(n, e) Cand{n "+nan-unchanged", [](double x, double) { return std::isnan(x) ? x : e(x); }}
#define D1L(n, e) Cand{n "+nan-unchanged+lane", [](double x, double) { return std::isnan(x) ? x : e(x); }, [](double, double, uint64_t p) { return p; }}

std::vector<Test> tests() {
    return {
        {"sqrtf", f_028F4384, F1, {F1C("sqrtf", std::sqrt)}},
        {"sinf", f_028F43F8, F1, {F1C("sinf", std::sin)}},
        {"cosf", f_028F4BE0, F1, {F1C("cosf", std::cos)}},
        {"tanf", f_028F423C, F1, {F1C("tanf", std::tan)}},
        {"floorf", f_028F4114, F1, {F1C("floorf", std::floor), F1N("floorf", std::floor), F1L("floorf", std::floor, true), F1C("ceilf", std::ceil), F1C("truncf", std::trunc)}},
        {"ceilf", f_028F41B0, F1, {F1C("ceilf", std::ceil), F1N("ceilf", std::ceil), F1L("ceilf", std::ceil, false), F1C("floorf", std::floor), F1C("truncf", std::trunc)}},
        {"truncf", f_028F3F90, F1, {F1C("truncf", std::trunc), F1C("roundf", std::round), F1C("floorf", std::floor)}},
        {"atanf", f_028F4DC4, F1, {F1C("atanf", std::atan)}},
        {"asinf", f_028F4F20, F1, {F1C("asinf", std::asin), F1C("acosf", std::acos)}},
        {"acosf", f_028F4FAC, F1, {F1C("acosf", std::acos), F1C("asinf", std::asin)}},
        {"fmodf", f_028F4034, F2, {Cand{"fmodf", [](double x, double y) { return fr(std::fmod(fa(x), fa(y))); }}}},
        {"powf", f_028F4560, F2, {Cand{"powf", [](double x, double y) { return fr(std::pow(fa(x), fa(y))); }}}},
        {"atan2f", f_028F4D28, F2, {Cand{"atan2f", [](double x, double y) { return fr(std::atan2(fa(x), fa(y))); }}}},
        {"sqrt", f_028F37F0, D1, {D1C("sqrt", std::sqrt)}},
        {"sin", f_028F38F8, D1, {D1C("sin", std::sin)}},
        {"cos", f_028F3C78, D1, {D1C("cos", std::cos)}},
        {"atan", f_028F3CA0, D1, {D1C("atan", std::atan)}},
        {"asin", f_028F3EC0, D1, {D1C("asin", std::asin), D1C("acos", std::acos)}},
        {"floor", f_028F3B98, D1, {D1C("floor", std::floor), D1N("floor", std::floor), D1L("floor", std::floor), D1C("ceil", std::ceil), D1C("trunc", std::trunc)}},
        {"fmod", f_028F3AB8, D2, {Cand{"fmod", [](double x, double y) { return std::fmod(x, y); }}}},
    };
}

// input classes: 0 zero, 1 denormal, 2 normal, 3 infinity, 4 NaN
int cls(double d, bool single) {
    if (single) {
        float f = (float)d;
        switch (std::fpclassify(f)) {
            case FP_ZERO: return 0;
            case FP_SUBNORMAL: return 1;
            case FP_NORMAL: return 2;
            case FP_INFINITE: return 3;
            default: return 4;
        }
    }
    switch (std::fpclassify(d)) {
        case FP_ZERO: return 0;
        case FP_SUBNORMAL: return 1;
        case FP_NORMAL: return 2;
        case FP_INFINITE: return 3;
        default: return 4;
    }
}
const char* kClass[5] = {"zero", "denormal", "normal", "inf", "nan"};

std::atomic<uint64_t> lane_kind[3];
std::atomic<uint64_t> lane_rule_broken;  // original's f1.ps1 after the call: = ps0, = argument, other

struct Stats {
    uint64_t n = 0, differ = 0, nan_payload = 0, max_ulp = 0, lane_differ = 0;
    uint64_t by_class[5][5] = {};  // differences by (x class, y class)
    uint64_t ulp_normal = 0;       // largest difference among normal inputs with a non-NaN result on both sides
    uint64_t ex[4][3] = {};  // first differing inputs: x bits, y bits, original result bits
    int nex = 0;
};

uint64_t ulp_dist(double a, double b, bool single) {
    if (single) {
        int32_t ia, ib;
        float fa_ = (float)a, fb_ = (float)b;
        memcpy(&ia, &fa_, 4);
        memcpy(&ib, &fb_, 4);
        if (ia < 0) ia = INT32_MIN - ia;
        if (ib < 0) ib = INT32_MIN - ib;
        return (uint64_t)std::llabs((long long)ia - (long long)ib);
    }
    int64_t ia = (int64_t)bits(a), ib = (int64_t)bits(b);
    if (ia < 0) ia = INT64_MIN - ia;
    if (ib < 0) ib = INT64_MIN - ib;
    __int128 d = (__int128)ia - ib;
    return (uint64_t)(d < 0 ? -d : d);
}

double arg_of(Kind k, uint64_t raw) {
    if (k == F1 || k == F2) return ppc_lfs_value((uint32_t)raw);  // what lfs puts in the FPR
    return u64_as_f64(raw);
}

// structured edge values: zeros, denormals, powers of two and their neighbours, multiples of pi/2,
// integers and half-integers, the largest values, infinities, NaNs
std::vector<uint64_t> edges(bool single) {
    std::vector<uint64_t> v;
    auto add = [&](double d) {
        if (single) {
            float f = (float)d;
            uint32_t b;
            memcpy(&b, &f, 4);
            for (int dd = -2; dd <= 2; dd++) v.push_back((uint32_t)(b + dd));
            v.push_back(b ^ 0x80000000u);
        } else {
            uint64_t b = bits(d);
            for (int dd = -2; dd <= 2; dd++) v.push_back(b + dd);
            v.push_back(b ^ 0x8000000000000000ull);
        }
    };
    for (int e = -1100; e <= 1030; e++) add(std::ldexp(1.0, e));
    for (int k = -64; k <= 64; k++) add(k * M_PI_2), add(k * M_PI_4), add(k * 0.5), add(k);
    add(0.0), add(INFINITY), add(NAN), add(1e38), add(3.4028235e38), add(1.7976931348623157e308);
    if (single) {
        for (uint32_t b : {0x7FC00000u, 0x7F800001u, 0x7FA00000u, 0xFFC00001u, 0x00000001u, 0x007FFFFFu}) v.push_back(b);
    } else {
        for (uint64_t b : {0x7FF8000000000000ull, 0x7FF0000000000001ull, 0xFFF8000000000001ull, 1ull, 0x000FFFFFFFFFFFFFull}) v.push_back(b);
    }
    return v;
}

void run_one(const Test& t, Cpu* c, int slot, uint64_t xr, uint64_t yr, std::vector<Stats>& st, CallLog& log) {
    const bool single = t.kind == F1 || t.kind == F2;
    double x = arg_of(t.kind, xr), y = arg_of(t.kind, yr);
    reset(c, slot);
    c->f[1].ps0 = x;
    c->f[1].ps1 = u64_as_f64(kLaneMarker);  // a marker, to see whether the original writes ps1
    c->f[2].ps0 = c->f[2].ps1 = y;
    log.clear();
    t.orig(c);
    double o = c->f[1].ps0;
    // the result's second lane: the caller sees the whole FPR (paired singles); a candidate leaves ps1 as
    // the original does only if the original sets it to ps0 (frsp / single-precision results) or to the
    // argument's ps1 (untouched); anything else counts as a difference
    const uint64_t o1 = bits(c->f[1].ps1);
    const int lane = o1 == bits(o) ? 0 : o1 == kLaneMarker ? 1 : 2;
    lane_kind[lane].fetch_add(1, std::memory_order_relaxed);
    // hypothesis for floor/ceil: ps1 keeps the argument exactly when the result is the argument itself
    if ((lane == 1) != (bits(o) == bits(x))) lane_rule_broken.fetch_add(1, std::memory_order_relaxed);
    for (size_t i = 0; i < t.cands.size(); i++) {
        double h = t.cands[i].fn(x, y);
        Stats& s = st[i];
        s.n++;
        if (t.cands[i].lane && t.cands[i].lane(x, h, kLaneMarker) != o1) {
            s.differ++;
            s.lane_differ++;
            if (s.nex < 4) s.ex[s.nex][0] = xr, s.ex[s.nex][1] = yr, s.ex[s.nex][2] = o1, s.nex++;
            continue;
        }
        if (bits(o) == bits(h)) continue;
        if (std::isnan(o) && std::isnan(h)) {
            s.nan_payload++;
            continue;
        }
        s.differ++;
        uint64_t u = std::isnan(o) || std::isnan(h) ? UINT64_MAX : ulp_dist(o, h, single);
        if (u > s.max_ulp) s.max_ulp = u;
        int cx = cls(x, single), cy = (t.kind == F2 || t.kind == D2) ? cls(y, single) : 2;
        s.by_class[cx][cy]++;
        if (cx == 2 && cy == 2 && u != UINT64_MAX && u > s.ulp_normal) s.ulp_normal = u;
        if (s.nex < 4) {
            s.ex[s.nex][0] = xr, s.ex[s.nex][1] = yr, s.ex[s.nex][2] = bits(o);
            s.nex++;
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: mathsurvey IMAGE FUNC [threads] [count]\n");
        return 2;
    }
    load_image(argv[1]);
    int nth = argc > 3 ? atoi(argv[3]) : 4;
    uint64_t count = argc > 4 ? strtoull(argv[4], nullptr, 0) : 100000000ull;
    for (const Test& t : tests()) {
        if (strcmp(t.name, argv[2]) && strcmp(argv[2], "all")) continue;
        const bool single = t.kind == F1 || t.kind == F2;
        const bool two = t.kind == F2 || t.kind == D2;
        std::vector<uint64_t> ed = edges(single);
        // count 0: every input of a one-argument float function (2^32); otherwise `count` random inputs
        const bool full = t.kind == F1 && count == 0;
        uint64_t total = full ? (1ull << 32) : count;
        std::vector<std::vector<Stats>> per(nth, std::vector<Stats>(t.cands.size()));
        std::vector<std::thread> th;
        for (int k = 0; k < nth; k++) {
            th.emplace_back([&, k] {
                Cpu c;
                CallLog log;
                t_log = &log;
                std::mt19937_64 rng(0x5EED0000u + k);
                if (k == 0) {  // edges, and for two-argument functions every pair of edges
                    for (uint64_t a : ed) {
                        if (!two) run_one(t, &c, k, a, 0, per[k], log);
                        else
                            for (size_t j = 0; j < ed.size(); j += 13) run_one(t, &c, k, a, ed[j], per[k], log);
                    }
                }
                for (uint64_t i = k; i < total; i += nth) {
                    uint64_t x = full ? i : (single ? (uint32_t)rng() : rng());
                    uint64_t y = two ? (single ? (uint32_t)rng() : rng()) : 0;
                    run_one(t, &c, k, x, y, per[k], log);
                }
            });
        }
        for (auto& x : th) x.join();
        printf("%-7s original f1.ps1 after the call: = ps0 %" PRIu64 ", = unchanged %" PRIu64 ", other %" PRIu64 "\n", t.name,
               lane_kind[0].load(), lane_kind[1].load(), lane_kind[2].load());
        printf("%-7s ps1 rule (argument kept exactly when the result is the argument) broken %" PRIu64 " times\n", t.name, lane_rule_broken.load());
        for (auto& l : lane_kind) l = 0;
        lane_rule_broken = 0;
        for (size_t i = 0; i < t.cands.size(); i++) {
            Stats s;
            for (auto& p : per) {
                s.n += p[i].n, s.differ += p[i].differ, s.nan_payload += p[i].nan_payload, s.lane_differ += p[i].lane_differ;
                if (p[i].max_ulp > s.max_ulp) s.max_ulp = p[i].max_ulp;
                if (p[i].ulp_normal > s.ulp_normal) s.ulp_normal = p[i].ulp_normal;
                for (int a = 0; a < 5; a++)
                    for (int b = 0; b < 5; b++) s.by_class[a][b] += p[i].by_class[a][b];
                for (int e = 0; e < p[i].nex && s.nex < 4; e++) memcpy(s.ex[s.nex++], p[i].ex[e], sizeof s.ex[0]);
            }
            printf("%-7s vs %-7s inputs %12" PRIu64 "  differ %12" PRIu64 "  nan-payload-only %10" PRIu64 "  max-ulp %s%" PRIu64 "%s\n",
                   t.name, t.cands[i].name, s.n, s.differ, s.nan_payload, s.max_ulp == UINT64_MAX ? "(nan vs number) " : "",
                   s.max_ulp == UINT64_MAX ? 0 : s.max_ulp, s.differ ? "" : "  EXACT");
            for (int a = 0; a < 5; a++)
                for (int b = 0; b < 5; b++)
                    if (s.by_class[a][b])
                        printf("    differ with x %s%s%s: %" PRIu64 "\n", kClass[a], two ? ", y " : "", two ? kClass[b] : "", s.by_class[a][b]);
            if (s.lane_differ) printf("    of these, f1.ps1 differs: %" PRIu64 "\n", s.lane_differ);
            if (s.differ) printf("    largest difference on normal inputs (non-NaN results): %" PRIu64 " ulp\n", s.ulp_normal);
            for (int e = 0; e < s.nex; e++)
                printf("    e.g. x=%016" PRIX64 " y=%016" PRIX64 " original=%016" PRIX64 "\n", s.ex[e][0], s.ex[e][1], s.ex[e][2]);
            fflush(stdout);
        }
    }
    return 0;
}
