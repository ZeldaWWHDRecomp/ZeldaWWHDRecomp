// Decomp build: the decompiled source's port points (the decompilation's wwhd_src/include/port_hooks.h, game build
// only; tools/decomp_build/port_sites.py lists them). A function with one of the port's instruction hooks inside used to
// stay recompiled in the decomp build so that the hook ran; at a port point the decompiled function runs that hook's
// own handler instead, at the same place. The normal (recompiled) build never calls these: its hooks are unchanged.
//
// So the points are compiled only where something calls them: the decomp build (WWHD_DECOMP) and the unit tests that
// drive them (WWHD_PORT_POINT_TESTS). Each point sits inside `#if WWHD_PORT_POINTS`. The decomp build runs the USA game
// only, so its sites are the USA addresses the decompiled source passes in; the region audit
// (tools/recomp/test_coverage.py) skips these blocks. Nothing in them is in the normal build, where every game address
// still goes through GC/GD.
#pragma once
#if defined(WWHD_DECOMP) || defined(WWHD_PORT_POINT_TESTS)
#define WWHD_PORT_POINTS 1
#else
#define WWHD_PORT_POINTS 0
#endif
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "ppc.h"

namespace port_points {
// Runs HANDLER (an instruction hook's site_XXXXXXXX) on the decompiled function's Cpu as it would run at the hooked
// instruction for what the handler uses: r1 = SP (where the function's guest calls go: its frame is above), the
// registers SET writes (the values the original keeps there at that instruction). GET reads the handler's result from
// the Cpu; then every register is put back, so the decompiled code's own state (r1, its call state) is as before.
template <class Set, class Get>
inline void run(Cpu* c, uint32_t sp, PpcFunc handler, Set set, Get get) {
    const Cpu saved = *c;
    c->r[1] = sp;
    set(*c);
    ppc_host_call(c, handler);
    get(*c);
    *c = saved;
}

// a point names a site the port has no handler row for: the decompilation and the port disagree
[[noreturn]] inline void unknown(const char* feature, uint32_t site) {
    std::fprintf(stderr, "port point: %s has no point for site %08X (port_hooks.h vs tools/decomp_build/port_sites.py)\n",
                 feature, site);
    std::abort();
}
}  // namespace port_points
