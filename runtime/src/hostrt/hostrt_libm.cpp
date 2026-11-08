// SPDX-License-Identifier: MPL-2.0
// Decomp build, fast mode only (cmake WWHD_DECOMP_FAST_LIBM; OFF: nothing here is compiled): native ports of GHS libm
// functions, called from gmem_call through hostrt_fast_direct (hostrt_fast.cpp) instead of their recompiled originals.
//
// A port is the original's instruction sequence with its out-of-line helpers inlined (GHS's f31/LR save and restore
// routines 028F605C / 028F60F8), on locals instead of the Cpu where the original keeps them in registers. It uses the
// same ppc.h operations as the recompiled code (to_single, round25, ldf32/stf32, cr_set_f), so the arithmetic is the
// recompiled original's, and it makes the same guest memory writes (back chain, f31 and LR save words, the bit-cast
// temporary) and the same import call (__gh_set_errno). Proof: tools/host_runtime/difftest/libmport.cpp runs the
// port next to the original on every one of the 2^32 single-precision inputs (with randomised other registers and
// f1.ps1) and compares every GPR, both lanes of every FPR, CR, LR, CTR, XER, FPSCR, the import calls and the guest
// stack around the call. A function without such a clean proof gets no port (its original stays, hostrt_fast.cpp).
#ifdef WWHD_DECOMP_FAST_LIBM
#include "hostrt_impl.h"

extern "C" void imp_coreinit___gh_set_errno(Cpu* c);

namespace hostrt {

// sqrtf (028F4384): x > 0 (the constant at 1018A848, +0): a bit-trick first guess and two Newton steps in single
// precision; x == 0: the constant; otherwise (x < 0, NaN): __gh_set_errno(33) and the constant. Not IEEE sqrtf
// (tools/host_runtime/difftest/results/math_survey.txt: up to 3 ulp apart).
void sqrtf_028F4384(Cpu* __restrict c) {
    PPC_ENTER(0x028F4384u);
    uint32_t r0 = c->r[0], r1 = c->r[1], r3 = c->r[3], r11 = c->r[11], r12 = c->r[12];
    double f0_0 = c->f[0].ps0, f0_1 = c->f[0].ps1, f1_0 = c->f[1].ps0, f1_1 = c->f[1].ps1, f13_0 = c->f[13].ps0,
           f13_1 = c->f[13].ps1, f31_0 = c->f[31].ps0, f31_1 = c->f[31].ps1;
    r0 = c->lr;                                                              // mflr r0
    r11 = r1;                                                                // mr r11, r1
    { uint32_t ea = r1 + 0xFFFFFFE0u; st32(ea, r1); r1 = ea; }               // stwu r1, -0x20(r1)
    // bl 028F605C (f31 and LR save): its entry check, then the saves; it changes no register but f31's lanes
    c->r[0] = r0; c->r[1] = r1; c->r[11] = r11;
    PPC_ENTER(0x028F605Cu);
    stf64(r11 + 0xFFFFFFF0u, f31_0);                                         // stfd f31, -0x10(r11)
    { double v0 = f31_1, v1 = f31_0; f31_0 = v0; f31_1 = v1; }               // ps_merge10 f31, f31, f31
    stf32(r11 + 0xFFFFFFF8u, f31_0);                                         // stfs f31, -8(r11)
    st32(r11 + 0x00000004u, r0);                                             // stw r0, 4(r11)
    r12 = 0x10190000u;
    { double v = ldf32(r12 + 0xFFFFA848u); f31_0 = v; f31_1 = v; }
    cr_set_f(c, 0, f1_0, f31_0);
    if (c->cr[1]) {
        stf32(r1 + 0x00000008u, f1_0);
        r12 = ld32(r1 + 0x00000008u);
        r12 = rotl32(r12, 31) & 0x7FFFFFFFu;
        r12 = r12 + 0x1FBB0000u;
        r12 = r12 + 0x00004EAFu;
        st32(r1 + 0x00000008u, r12);
        { double v = ldf32(r1 + 0x00000008u); f13_0 = v; f13_1 = v; }
        { double v = to_single(f1_0 / f13_0); f0_0 = v; f0_1 = v; }
        { double v = to_single(f13_0 + f0_0); f13_0 = v; f13_1 = v; }
        r12 = 0x10190000u;
        { double v = to_single(f1_0 / f13_0); f1_0 = v; f1_1 = v; }
        { double v = ldf32(r12 + 0xFFFFA84Cu); f0_0 = v; f0_1 = v; }
        { double v = to_single(f13_0 * round25(f0_0) + f1_0); f31_0 = v; f31_1 = v; }
    } else {
        cr_set_f(c, 0, f1_0, f31_0);
        if (!c->cr[2]) {
            r3 = 0x00000021u;
            c->lr = 0x028F43ECu;
            c->r[3] = r3; c->r[12] = r12;
            c->f[0].ps0 = f0_0; c->f[0].ps1 = f0_1; c->f[1].ps0 = f1_0; c->f[1].ps1 = f1_1;
            c->f[13].ps0 = f13_0; c->f[13].ps1 = f13_1; c->f[31].ps0 = f31_0; c->f[31].ps1 = f31_1;
            imp_coreinit___gh_set_errno(c);
            r0 = c->r[0]; r1 = c->r[1]; r3 = c->r[3]; r11 = c->r[11]; r12 = c->r[12];
            f0_0 = c->f[0].ps0; f0_1 = c->f[0].ps1; f1_0 = c->f[1].ps0; f1_1 = c->f[1].ps1;
            f13_0 = c->f[13].ps0; f13_1 = c->f[13].ps1; f31_0 = c->f[31].ps0; f31_1 = c->f[31].ps1;
        }
    }
    r11 = r1 + 0x00000020u;
    f1_0 = f31_0;
    // b 028F60F8 (f31 and LR restore)
    c->r[11] = r11; c->r[12] = r12;
    c->f[0].ps0 = f0_0; c->f[0].ps1 = f0_1; c->f[1].ps0 = f1_0; c->f[1].ps1 = f1_1;
    c->f[13].ps0 = f13_0; c->f[13].ps1 = f13_1; c->f[31].ps0 = f31_0; c->f[31].ps1 = f31_1;
    PPC_ENTER(0x028F60F8u);
    r0 = ld32(r11 + 0x00000004u);                                            // lwz r0, 4(r11)
    { double v = ldf32(r11 + 0xFFFFFFF8u); f31_0 = v; f31_1 = v; }           // lfs f31, -8(r11)
    f31_0 = ldf64(r11 + 0xFFFFFFF0u);                                        // lfd f31, -0x10(r11)
    __atomic_thread_fence(__ATOMIC_ACQUIRE);                                 // isync
    c->lr = r0;                                                              // mtlr r0
    r1 = r11;                                                                // mr r1, r11
    c->r[1] = r1; c->f[31].ps0 = f31_0; c->f[31].ps1 = f31_1;                // blr
}

}  // namespace hostrt
#endif
