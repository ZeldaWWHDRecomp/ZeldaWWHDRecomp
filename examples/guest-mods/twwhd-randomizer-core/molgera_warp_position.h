/* MIT TWWHD Randomizer fix_vanilla_bugs.asm/check_if_inside_warp.
 * Intended ONLY immediately after the full Molgera camera consumer's target-z
 * store. This helper is not registered as an independent/global SDK hook. */
#pragma once
#ifndef R_MOLGERA_POS
#define R_MOLGERA_POS(t,a) (*(volatile t*)(a))
#endif
#ifndef RANDOMIZER_MOLGERA_SQRT
#define RANDOMIZER_MOLGERA_SQRT(value) randomizer_hyrule_sqrt(value)
#endif
static void randomizer_molgera_safe_target(u32 position) {
#pragma clang fp contract(off)
    const f32 x=R_MOLGERA_POS(f32,position),z=R_MOLGERA_POS(f32,position+8);
    const f32 xx=x*x,zz=z*z;
    const f32 square=xx+zz;
    const f64 distance=RANDOMIZER_MOLGERA_SQRT((f64)square);
    /* Native bgt skips replacement; unordered therefore also selects default. */
    if(!(distance>450.0)) {
        R_MOLGERA_POS(f32,position)=0.0f;
        R_MOLGERA_POS(f32,position+8)=777.0f;
    }
}
