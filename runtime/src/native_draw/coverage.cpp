// Native renderer coverage counter (gx2/native_coverage.h). Decomp build, fast mode, WWHD_NATIVE_DRAW=ON only; debug,
// off unless WWHD_NATIVE_COVERAGE is set.
#include "gx2/native_coverage.h"
#include "runtime.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#ifdef __APPLE__
#include <execinfo.h>
#include <mach-o/dyld.h>
#endif

namespace native_draw::coverage {
namespace {
struct Ent {
    uint64_t ops = 0, count = 0, runs = 0, distinct = 0;
    uint32_t op = 0, lr = 0;
    std::vector<uintptr_t> chain;
};
struct FrameMeshes {
    uint64_t chain = 0;
    std::vector<uint64_t> v;
};
std::unordered_map<uint64_t, Ent> g_tab;
std::unordered_map<uint64_t, FrameMeshes> g_frameMeshes;  // (chain, render-target set) -> meshes this frame
uint64_t g_prevKey = 0, g_targetEpoch = 0, g_from = 3600, g_to = 3900, g_swap = 0;
std::mutex g_mu;  // (operations come from more than one guest thread)
FILE* g_out = nullptr;
bool g_done = false;
const char* const kOpNames[kOps] = {"draw", "list", "packet", "copy", "clear", "expand", "target", "calllist"};

bool init() {
    const char* e = std::getenv("WWHD_NATIVE_COVERAGE");
    if (!e || !*e) return false;
#ifdef __APPLE__
    std::string s(e);
    unsigned long long a = 3600, b = 3900;
    const size_t k = s.find(':');
    if (k != std::string::npos) {
        std::sscanf(s.c_str() + k + 1, "%llu:%llu", &a, &b);
        s.resize(k);
    }
    g_out = std::fopen(s.c_str(), "w");
    g_from = a;
    g_to = b;
    LOG("[native-coverage] %s, swaps %llu-%llu", g_out ? "counting" : "cannot open the file", a, b);
    return g_out != nullptr;
#else
    LOG("[native-coverage] not supported on this platform");
    return false;
#endif
}
}  // namespace

bool on() {
    static const bool v = init();
    return v;
}

void op(Op o, uint32_t lr, uint32_t count, uint32_t mesh) {
#ifdef __APPLE__
    std::lock_guard<std::mutex> lk(g_mu);
    if (o == kTarget) ++g_targetEpoch;
    if (g_done || g_swap < g_from || g_swap >= g_to) return;
    void* fr[26];
    const int n = backtrace(fr, 26);
    uint64_t h = (1469598103934665603ull ^ lr ^ uint64_t(o) << 40) * 1099511628211ull;
    for (int i = 2; i < n; ++i) h = (h ^ uint64_t(uintptr_t(fr[i]))) * 1099511628211ull;
    Ent& en = g_tab[h];
    if (!en.ops) {
        for (int i = 2; i < n; ++i) en.chain.push_back(uintptr_t(fr[i]));
        en.op = o;
        en.lr = lr;
    }
    ++en.ops;
    en.count += count;
    if (o != kDraw && o != kList && o != kPacket) return;
    const uint64_t m = uint64_t(mesh) << 32 | count, key = h * 31 + m;
    en.runs += key != g_prevKey;
    g_prevKey = key;
    FrameMeshes& fm = g_frameMeshes[h * 1099511628211ull ^ g_targetEpoch];
    fm.chain = h;
    if (std::find(fm.v.begin(), fm.v.end(), m) == fm.v.end()) fm.v.push_back(m);
#else
    (void)o, (void)lr, (void)count, (void)mesh;
#endif
}

void swap() {
#ifdef __APPLE__
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_done) return;
    if (g_swap >= g_from)
        for (auto& [k, fm] : g_frameMeshes) g_tab[fm.chain].distinct += fm.v.size();
    g_frameMeshes.clear();
    g_prevKey = 0;
    if (++g_swap < g_to) return;
    g_done = true;
    std::fprintf(g_out, "S slide %llx swaps %llu\n", (unsigned long long)_dyld_get_image_vmaddr_slide(0),
                 (unsigned long long)(g_to - g_from));
    for (auto& [h, en] : g_tab) {
        std::fprintf(g_out, "O %s %llu %llu %llu %llu %08X", kOpNames[en.op], (unsigned long long)en.ops,
                     (unsigned long long)en.count, (unsigned long long)en.runs, (unsigned long long)en.distinct, en.lr);
        for (uintptr_t a : en.chain) std::fprintf(g_out, " %llx", (unsigned long long)a);
        std::fputc('\n', g_out);
    }
    std::fclose(g_out);
    g_out = nullptr;
    LOG("[native-coverage] %zu chains written (swaps %llu-%llu)", g_tab.size(), (unsigned long long)g_from,
        (unsigned long long)g_to);
#endif
}

}  // namespace native_draw::coverage
