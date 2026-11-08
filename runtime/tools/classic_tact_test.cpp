// Classic Wind Waker (mods/speedrun.cpp, issue #405): the two instruction sites against the TWWHD Randomizer's
// Classic-mode patches, on synthetic register state (no game code). Each guest sequence is written out as the
// recompiler translates it (control flow, registers, cr0.eq), in three forms: HD's original words, the randomizer's
// patched words, and the original words with the port's site. For every input:
//  - switch off: the site leaves the original's result (the patched word's behaviour is not there)
//  - switch on: the same result as the randomizer's patched word (0243A28C nop, 023F22B0 b 0x023F22E4)
//  - a Classic-mode seed (patched words) with the switch on as well: unchanged
#include "mods/mods.h"
#include "ppc.h"
#include <cassert>
#include <cstdio>
#include <cstring>

extern "C" void site_0243A294(Cpu*);
extern "C" void site_023F22B4(Cpu*);

namespace {
// daPy_lk_c::procTactWait 0243A26C..0243A294: r30 = 1 (cancel), then r3 = the result of the call at 0243A270
// (given), r11 = mProcVar1 (given), the acch word at +0x834 (given, in r0).
struct Tact { uint32_t r0, r3, r11, r12, r30; };
Tact tact(uint32_t r3, int32_t r11, uint32_t acch, bool nop_patch, bool site) {
    Cpu c{};
    c.r[30] = 1;                                                // 0243A26C li r30, 1
    c.r[3] = r3; c.r[11] = (uint32_t)r11;
    c.cr[2] = c.r[3] == 0;                                      // 0243A274 cmpwi r3, 0
    if (!c.cr[2]) goto L_0243A294;                              // 0243A278 bne
    c.cr[0] = (int32_t)c.r[11] < -1;                            // 0243A27C cmpwi r11, -1
    if (!c.cr[0]) goto L_0243A290;                              // 0243A280 bge
    c.r[0] = acch;                                              // 0243A284 lwz r0, 0x834(r31)
    c.r[12] = c.r[0] & 0x20; c.cr[2] = c.r[12] == 0;            // 0243A288 rlwinm. r12, r0, 0, 26, 26
    if (!nop_patch && c.cr[2]) goto L_0243A294;                 // 0243A28C beq (Classic mode: nop)
L_0243A290:
    c.r[30] = 0;                                                // 0243A290 li r30, 0
L_0243A294:
    if (site) site_0243A294(&c);
    return {c.r[0], c.r[3], c.r[11], c.r[12], c.r[30]};         // (0243A294 cmpwi r11, 0 sets cr0 again)
}
bool same(const Tact& a, const Tact& b) { return !memcmp(&a, &b, sizeof a); }

// daPy_lk_c::endDemoMode 023F22A4..023F22E8: r10 = mProcID, r30 (given); the call at 023F22DC (HD's reset to the
// waiting state) is recorded; 023F22E8 reloads r30 from the stack.
struct Demo { bool reset; uint32_t r10, r30; };
Demo demo(uint32_t proc, uint32_t r30, bool branch_patch, bool site) {
    Cpu c{};
    bool reset = false;
    const uint32_t saved_r30 = 0xDEAD0030u;
    c.r[10] = proc; c.r[30] = r30;                              // 023F22A4 lwz r10, 0x65F0(r31)
    c.cr[2] = c.r[10] == 201;                                   // 023F22A8 cmpwi r10, 201
    if (c.cr[2]) goto L_023F22DC;                               // 023F22AC beq
    if (branch_patch) goto L_023F22E4;                          // 023F22B0 (Classic mode: b 0x023F22E4)
    c.cr[2] = c.r[30] == 0;                                     // 023F22B0 cmpwi r30, 0
    if (site) site_023F22B4(&c);
    if (c.cr[2]) goto L_023F22E4;                               // 023F22B4 beq
    c.r[11] = c.r[10] - 15; if (c.r[11] < 4) goto L_023F22E4;   // 023F22B8..023F22C0
    c.r[12] = c.r[10] - 150; if (c.r[12] < 4) goto L_023F22E4;  // 023F22C4..023F22CC
    c.r[0] = c.r[10] - 118; if (c.r[0] < 10) goto L_023F22E4;   // 023F22D0..023F22D8
L_023F22DC:
    reset = true;                                               // 023F22DC/E0 mr r3, r31; bl
L_023F22E4:
    c.r[30] = saved_r30;                                        // 023F22E8 lwz r30, 8(r1)
    return {reset, c.r[10], c.r[30]};
}
bool same(const Demo& a, const Demo& b) { return a.reset == b.reset && a.r10 == b.r10 && a.r30 == b.r30; }
}  // namespace

int main() {
    assert(!mods::classic_wind_waker());  // off by default
    int cases = 0, differs = 0;
    for (uint32_t r3 : {0u, 1u, 0x10000000u})
        for (int32_t r11 : {-5, -2, -1, 0, 1, 7})
            for (uint32_t acch : {0u, 0x20u, 0x28E0u, 0x2010u, 0xFFFFFFDFu}) {
                mods::set_classic_wind_waker(false);
                const Tact hd = tact(r3, r11, acch, false, false), off = tact(r3, r11, acch, false, true);
                mods::set_classic_wind_waker(true);
                const Tact patched = tact(r3, r11, acch, true, false), on = tact(r3, r11, acch, false, true),
                           seed_on = tact(r3, r11, acch, true, true);
                assert(same(off, hd));          // off: HD's falling check stays
                assert(same(on, patched));      // on: the nop
                assert(same(seed_on, patched)); // Classic-mode seed + switch: unchanged
                differs += !same(hd, patched);
                ++cases;
            }
    // the HD check cancels (r30 = 1) exactly when the nop does not: falling (ground bit clear), r3 = 0, r11 < -1
    mods::set_classic_wind_waker(false);
    assert(tact(0, -2, 0x2010, false, true).r30 == 1);
    mods::set_classic_wind_waker(true);
    assert(tact(0, -2, 0x2010, false, true).r30 == 0);
    assert(tact(1, -2, 0x2010, false, true).r30 == 1);  // the game's own cancel (r3 != 0) stays
    assert(differs > 0);

    int demo_differs = 0;
    for (uint32_t proc : {0u, 4u, 15u, 18u, 19u, 118u, 127u, 128u, 150u, 153u, 154u, 201u, 202u})
        for (uint32_t r30 : {0u, 1u, 5u}) {
            mods::set_classic_wind_waker(false);
            const Demo hd = demo(proc, r30, false, false), off = demo(proc, r30, false, true);
            mods::set_classic_wind_waker(true);
            const Demo patched = demo(proc, r30, true, false), on = demo(proc, r30, false, true),
                       seed_on = demo(proc, r30, true, true);
            assert(same(off, hd));
            assert(same(on, patched));
            assert(same(seed_on, patched));
            demo_differs += !same(hd, patched);
            ++cases;
        }
    mods::set_classic_wind_waker(false);
    assert(demo(4, 1, false, true).reset);   // HD: back to the waiting state
    mods::set_classic_wind_waker(true);
    assert(!demo(4, 1, false, true).reset);  // Classic: no reset (dry storage)
    assert(demo(201, 1, false, true).reset); // procedure 201 resets in both
    assert(demo_differs > 0);
    mods::set_classic_wind_waker(false);
    printf("classic_tact_test: %d cases, %d tact + %d demo inputs where Classic differs from HD; all match\n", cases,
           differs, demo_differs);
}
