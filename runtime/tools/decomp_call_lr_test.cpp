// The LR a guest call from decompiled code gets (runtime/src/decomp/call_lr.h): the return address of the
// original's one call instruction to that target, else LR unchanged.
#include "decomp/call_lr.h"

#include <algorithm>
#include <cstdio>
#include <vector>

static int failures = 0;
static void expect(const char* what, uint32_t got, uint32_t want) {
    if (got != want) {
        printf("FAIL %s: %08X, expected %08X\n", what, got, want);
        failures++;
    }
}
// bl/b from `at` to `to`
static uint32_t branch(uint32_t at, uint32_t to, bool link) { return 0x48000000u | ((to - at) & 0x03FFFFFCu) | (link ? 1u : 0u); }

int main() {
    using decomp::call_lr_scan;
    const uint32_t lo = 0x0203E0F8;  // as __sinit_olvOperation: two import calls after other code
    uint32_t w[16] = {};
    for (auto& x : w) x = 0x60000000u;  // nop
    w[0] = 0x7C0802A6u;                 // mflr r0
    w[3] = branch(lo + 12, 0x02000000, true);   // bl backwards
    w[7] = branch(lo + 28, 0x02050000, true);   // bl forwards
    w[9] = branch(lo + 36, 0x02100000, true);
    w[11] = branch(lo + 44, 0x02100000, true);  // the same target twice
    w[13] = branch(lo + 52, 0x02200000, false); // b: a tail call
    w[14] = 0x4E800421u;                        // bctrl
    const uint32_t stale = 0x028EA0F4;
    expect("bl backwards", call_lr_scan(w, lo, 16, 0x02000000, 0, stale), lo + 16);
    expect("bl forwards", call_lr_scan(w, lo, 16, 0x02050000, 0, stale), lo + 32);
    expect("two calls: unchanged", call_lr_scan(w, lo, 16, 0x02100000, 0, stale), stale);
    expect("tail call: unchanged", call_lr_scan(w, lo, 16, 0x02200000, 0, stale), stale);
    expect("no call: unchanged", call_lr_scan(w, lo, 16, 0x02300000, 0, stale), stale);
    expect("one bctrl", call_lr_scan(w, lo, 16, 0x12345678, 1, stale), lo + 60);
    w[15] = 0x4E800420u;  // bctr: a tail branch through CTR (Delegate_invoke 0203E284), never a call
    expect("bctr does not count", call_lr_scan(w, lo, 16, 0x12345678, 1, stale), lo + 60);
    w[14] = 0x60000000u;
    expect("bctr only: unchanged (the callee sees the entry LR)", call_lr_scan(w, lo, 16, 0x12345678, 1, stale), stale);
    w[14] = 0x4E800421u;
    w[2] = 0x4E800421u;
    expect("two bctrl: unchanged", call_lr_scan(w, lo, 16, 0x12345678, 1, stale), stale);
    w[5] = 0x48000003u | 0x00001000u;  // bla 0x1000 (absolute)
    expect("bla", call_lr_scan(w, lo, 16, 0x00001000, 0, stale), lo + 24);
    // calls of imported functions: the bl words are not relocated (as at 0203E1B0: 488BB181); the
    // import-call table names their slots
    w[8] = 0x488BB181u, w[10] = 0x488BB1A5u;
    const uint32_t imports[][2] = {{0x02000000, 0xC0000004}, {lo + 32, 0xC0004F20}, {lo + 40, 0xC0004F50},
                                   {0x02100000, 0xC0004F20}};
    expect("import", call_lr_scan(w, lo, 16, 0xC0004F20, 0, stale, imports, 4), lo + 36);
    expect("other import", call_lr_scan(w, lo, 16, 0xC0004F50, 0, stale, imports, 4), lo + 44);
    expect("import call of another function", call_lr_scan(w, lo, 16, 0xC0000004, 0, stale, imports, 4), stale);
    expect("raw target of an import call is no call", call_lr_scan(w, lo, 16, lo + 32 + 0x8BB180, 0, stale, imports, 4), stale);
    // call_lr_sites / call_lr_find (the start-up table) answer as call_lr_scan for every target and kind: here w holds
    // two calls of 02100000, a tail call only (b 02200000), two bctrl, imports, one import called twice
    w[12] = 0x488BB181u;  // a second call of import C0004F20 (site lo + 48)
    const uint32_t imports2[][2] = {{0x02000000, 0xC0000004}, {lo + 32, 0xC0004F20}, {lo + 40, 0xC0004F50},
                                    {lo + 48, 0xC0004F20}, {0x02100000, 0xC0004F20}};
    for (int variant = 0; variant < 4; variant++) {
        if (variant == 2) w[2] = 0x60000000u;   // one bctrl left
        if (variant == 3) w[14] = 0x4E800420u;  // no bctrl, only bctr
        for (int with_imports = 0; with_imports < 2; with_imports++) {
            const uint32_t (*imp)[2] = with_imports ? imports2 : nullptr;
            const uint32_t nimp = with_imports ? 5 : 0;
            auto sites = decomp::call_lr_sites(w, lo, 16, imp, nimp);
            std::vector<uint32_t> keys, rets;
            for (auto& [k, r] : sites) keys.push_back(k), rets.push_back(r);
            const uint32_t targets[] = {0x02000000, 0x02050000, 0x02100000, 0x02200000, 0x02300000, 0x00001000, 0xC0004F20,
                                        0xC0004F50, 0xC0000004, lo + 32 + 0x8BB180, lo + 48 + 0x8BB180, 1, 0x12345678};
            for (uint32_t t : targets)
                for (int kind = 0; kind < 2; kind++) {
                    char what[96];
                    snprintf(what, sizeof what, "table = scan: variant %d imports %d target %08X kind %d", variant, with_imports, t, kind);
                    expect(what, decomp::call_lr_find(keys.data(), rets.data(), (uint32_t)keys.size(), t, kind, stale),
                           call_lr_scan(w, lo, 16, t, kind, stale, imp, nimp));
                }
        }
    }
    {
        auto sites = decomp::call_lr_sites(w, lo, 16, imports2, 5);
        std::vector<uint32_t> keys, rets;
        for (auto& [k, r] : sites) keys.push_back(k), rets.push_back(r);
        const uint32_t n = (uint32_t)keys.size();
        expect("table: two calls unchanged", decomp::call_lr_find(keys.data(), rets.data(), n, 0x02100000, 0, stale), stale);
        expect("table: tail call unchanged", decomp::call_lr_find(keys.data(), rets.data(), n, 0x02200000, 0, stale), stale);
        expect("table: import called twice unchanged", decomp::call_lr_find(keys.data(), rets.data(), n, 0xC0004F20, 0, stale), stale);
        expect("table: bctr only unchanged", decomp::call_lr_find(keys.data(), rets.data(), n, 0x12345678, 1, stale), stale);
        expect("table: one import call", decomp::call_lr_find(keys.data(), rets.data(), n, 0xC0004F50, 0, stale), lo + 44);
    }
    {
        // call sites (decomp.cpp sites_init): every bl/bla/b/ba target outside the function's own body, import calls by
        // their slot, each once; a branch inside the body is none
        w[1] = branch(lo + 4, lo + 40, false);   // b forward inside the body
        w[6] = branch(lo + 24, lo, false);       // b back to the entry (a loop to the start): listed
        auto ts = decomp::call_site_targets(w, lo, 16, imports2, 5);
        const std::vector<uint32_t> want = {0x00001000, 0x02000000, lo, 0x02050000, 0x02100000, 0x02200000, 0xC0004F20, 0xC0004F50};
        expect("site targets: count", (uint32_t)ts.size(), (uint32_t)want.size());
        for (size_t i = 0; i < want.size() && i < ts.size(); i++) expect("site targets", ts[i], want[i]);
        // a table answers each listed target with its own entry, nothing else (0 included)
        std::vector<decomp::CallSite> pool(3);  // a table after others in the pool
        for (uint32_t n : {1u, 2u, 3u, 7u, 64u, 1000u}) {
            std::vector<uint32_t> keys;
            for (uint32_t k = 0; k < n; k++) keys.push_back(0x02000000u + 4u * ((k * 7919u) % 100003u) * 8u);
            std::sort(keys.begin(), keys.end());
            keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
            const size_t base = pool.size();
            decomp::CallerSites c;
            c.bits = (uint8_t)decomp::site_table(keys, pool, [](uint32_t t) { return decomp::CallSite{0, t + 4, t ^ 1u, t >> 2}; });
            c.t = pool.data() + base;
            expect("site table at most half full", (uint32_t)(2 * keys.size() <= (size_t(1) << c.bits)), 1);
            for (uint32_t k : keys) {
                const decomp::CallSite* e = decomp::site_find(c, k);
                expect("site found", e ? e->target : 0, k);
                expect("site answer", e ? e->lr : 0, k + 4);
                expect("site ordinal", e ? e->ordinal : 0, k ^ 1u);
                expect("site not found", decomp::site_find(c, k + 4) ? 1u : 0u, std::binary_search(keys.begin(), keys.end(), k + 4) ? 1u : 0u);
            }
            expect("site 0 not found", decomp::site_find(c, 0) ? 1u : 0u, 0);
        }
        decomp::CallerSites none;
        expect("no table", decomp::site_find(none, 0x02000000) ? 1u : 0u, 0);
    }
    if (!failures) printf("decomp_call_lr: ok\n");
    return failures ? 1 : 0;
}
