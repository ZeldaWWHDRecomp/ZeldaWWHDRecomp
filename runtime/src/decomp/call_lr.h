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


// Call sites per caller (decomp.cpp sites_init, gmem_call): the targets a decompiled function's original code reaches
// with a bl/bla (an import call by its import slot, as call_lr_sites keys it) or a b/ba outside its own body (a tail
// call), each with the answers gmem_call needs for a guest call to it, found with one hash probe in a small table of
// the caller's own instead of call_lr's binary search plus the direct table's probe. A target that is not listed
// (and 0) is not found: gmem_call asks call_lr and direct_slot then, as before.
struct CallSite {
    uint32_t target;  // 0: empty
    uint32_t lr;      // call_lr's answer (kind 0) for this caller and target; 1: LR unchanged (a return address is never odd)
    uint32_t ordinal; // direct calls: the target's g_direct entry (its index in g_recomp_funcs, for g_mod_hook_flags) ...
    uint32_t slot1;   // ... and its slot index + 1; 0: no direct entry (direct_slot answers null)
};
struct CallerSites {
    const CallSite* t = nullptr;  // 1 << bits entries; null: none listed
    uint32_t bctrl_lr = 1;        // call_lr's answer for kind 1 (bctrl: the same for every target); 1: LR unchanged
    uint8_t bits = 0;
    uint8_t frame_exact = 0;      // Slot::frame_exact of the caller (no direct calls from it)
    uint64_t ind = 0;             // decomp.h call_to: the last bctrl target and its slot + 1 (0: none); 0: empty
};
inline uint32_t site_hash(uint32_t target, uint32_t bits) { return ((target >> 2) * 0x9E3779B1u) >> (32 - bits); }
inline const CallSite* site_find(const CallerSites& c, uint32_t target) {
    const CallSite* t = c.t;
    if (!t) return nullptr;
    const uint32_t mask = (1u << c.bits) - 1;
    for (uint32_t h = site_hash(target, c.bits);; h = (h + 1) & mask) {
        if (!t[h].target) return nullptr;
        if (t[h].target == target) return &t[h];
    }
}
// the targets listed for code words[0..n) at guest address lo (imports as for call_lr_sites), distinct, not 0
inline std::vector<uint32_t> call_site_targets(const uint32_t* words, uint32_t lo, uint32_t n, const uint32_t (*imports)[2] = nullptr,
                                               uint32_t nimports = 0) {
    uint32_t k = 0, hi = nimports;
    while (k < hi) {
        const uint32_t m = (k + hi) / 2;
        if (imports[m][0] < lo) k = m + 1;
        else hi = m;
    }
    std::vector<uint32_t> out;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t w = words[i], a = lo + 4 * i;
        while (k < nimports && imports[k][0] < a) k++;
        if ((w >> 26) != 18) continue;  // b / ba / bl / bla
        uint32_t t;
        if ((w & 1) && k < nimports && imports[k][0] == a) {
            t = imports[k][1];
        } else {
            int32_t li = int32_t(w & 0x03FFFFFCu);
            if (li & 0x02000000) li -= 0x04000000;
            t = (w & 2) ? uint32_t(li) : a + uint32_t(li);
        }
        if (!(w & 1) && t > lo && t - lo < 4 * n) continue;  // a branch inside the function
        if (t) out.push_back(t);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
// a caller's table for `targets` (distinct, not 0): at most half full, entries as fill(target) gives them
template <class Fill> inline uint32_t site_table(const std::vector<uint32_t>& targets, std::vector<CallSite>& pool, Fill fill) {
    uint32_t bits = 1;
    while ((1u << bits) < 2 * targets.size()) bits++;
    const size_t base = pool.size();
    pool.resize(base + (size_t(1) << bits));
    const uint32_t mask = (1u << bits) - 1;
    for (uint32_t t : targets) {
        uint32_t h = site_hash(t, bits);
        while (pool[base + h].target) h = (h + 1) & mask;
        pool[base + h] = fill(t);
        pool[base + h].target = t;
    }
    return bits;
}

}  // namespace decomp
