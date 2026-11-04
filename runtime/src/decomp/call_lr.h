// Decomp build: the LR of a guest call from decompiled code (decomp.cpp gmem_call / call_lr). Free of
// guest memory access, so runtime/tools/decomp_call_lr_test.cpp tests it alone.
#pragma once
#include <cstdint>

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

}  // namespace decomp
