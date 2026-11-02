// The LR a guest call from decompiled code gets (runtime/src/decomp/call_lr.h): the return address of the
// original's one call instruction to that target, else LR unchanged.
#include "decomp/call_lr.h"

#include <cstdio>

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
    if (!failures) printf("decomp_call_lr: ok\n");
    return failures ? 1 : 0;
}
