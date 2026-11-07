// Decomp build: the LR of a guest call from decompiled code (decomp.cpp gmem_call / call_lr). Free of
// guest memory access, so runtime/tools/decomp_call_lr_test.cpp tests it alone.
#pragma once
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace decomp {
// the original's call to `target` in its code words[0..n) (guest address lo): `bl target` (kind 0) or bctrl
// (kind 1). A bl that calls an imported function is not relocated in guest memory: `imports` (sorted
// {call site, import slot} pairs, recomp.py g_recomp_decomp_import_calls) names its target. Exactly one
// call -> its return address. `b target` only (a tail call: the original leaves LR as it is), none or
// several -> `lr`, unchanged. gabi::call_site sets LR itself; for its frame-exact chain calls the unique
// call found here is that same return address.
inline uint32_t call_lr_scan(const uint32_t* words, uint32_t lo, uint32_t n, uint32_t target, int kind, uint32_t lr,
                             const uint32_t (*imports)[2] = nullptr, uint32_t nimports = 0) {
    // the first import call at or after lo
    uint32_t k = 0, hi = nimports;
    while (k < hi) {
        const uint32_t m = (k + hi) / 2;
        if (imports[m][0] < lo) k = m + 1;
        else hi = m;
    }
    uint32_t found = 0, hits = 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t w = words[i], a = lo + 4 * i;
        if (kind == 1) {
            if (w == 0x4E800421u) { found = a + 4; hits++; }  // bctrl
            continue;
        }
        while (k < nimports && imports[k][0] < a) k++;
        if ((w >> 26) != 18 || !(w & 1)) continue;  // bl / bla
        uint32_t t;
        if (k < nimports && imports[k][0] == a) {
            t = imports[k][1];
        } else {
            int32_t li = int32_t((w & 0x03FFFFFCu) << 6) >> 6;  // sign-extended 26-bit branch offset
            t = (w & 2) ? uint32_t(li) : a + uint32_t(li);
        }
        if (t == target) { found = a + 4; hits++; }
    }
    return hits == 1 ? found : lr;
}

// The same answers for every target at once (decomp.cpp precomputes them per decompiled function at start-up):
// the calls of words[0..n) by call_lr_scan's rules, as {key, return address} sorted by key, for the keys with
// exactly one call. Key: the target of a bl/bla (its import slot for an import call), or kBctrlKey for bctrl
// (call_lr_scan kind 1; a bl/bla target is word-aligned, never 1). A target that is not listed (no call, a tail
// branch only, or several calls) leaves LR unchanged, as call_lr_scan does.
constexpr uint32_t kBctrlKey = 1;
inline std::vector<std::pair<uint32_t, uint32_t>> call_lr_sites(const uint32_t* words, uint32_t lo, uint32_t n,
                                                                const uint32_t (*imports)[2] = nullptr, uint32_t nimports = 0) {
    uint32_t k = 0, hi = nimports;
    while (k < hi) {
        const uint32_t m = (k + hi) / 2;
        if (imports[m][0] < lo) k = m + 1;
        else hi = m;
    }
    std::vector<std::pair<uint32_t, uint32_t>> all;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t w = words[i], a = lo + 4 * i;
        if (w == 0x4E800421u) { all.push_back({kBctrlKey, a + 4}); continue; }  // bctrl
        while (k < nimports && imports[k][0] < a) k++;
        if ((w >> 26) != 18 || !(w & 1)) continue;  // bl / bla
        uint32_t t;
        if (k < nimports && imports[k][0] == a) {
            t = imports[k][1];
        } else {
            int32_t li = int32_t(w & 0x03FFFFFCu);
            if (li & 0x02000000) li -= 0x04000000;
            t = (w & 2) ? uint32_t(li) : a + uint32_t(li);
        }
        all.push_back({t, a + 4});
    }
    std::stable_sort(all.begin(), all.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    std::vector<std::pair<uint32_t, uint32_t>> out;
    for (size_t i = 0; i < all.size();) {
        size_t j = i;
        while (j < all.size() && all[j].first == all[i].first) j++;
        if (j - i == 1) out.push_back(all[i]);
        i = j;
    }
    return out;
}

// call_lr_scan's answer from a call_lr_sites list (keys[0..n) ascending, rets alongside)
inline uint32_t call_lr_find(const uint32_t* keys, const uint32_t* rets, uint32_t n, uint32_t target, int kind, uint32_t lr) {
    if (kind != 1 && (target & 3)) return lr;  // no bl/bla reaches it (and kBctrlKey stays bctrl's alone)
    const uint32_t key = kind == 1 ? kBctrlKey : target;
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        const uint32_t m = (lo + hi) / 2;
        if (keys[m] < key) lo = m + 1;
        else hi = m;
    }
    return lo < n && keys[lo] == key ? rets[lo] : lr;
}

}  // namespace decomp
