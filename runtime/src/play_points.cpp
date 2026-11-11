// Decomp build: the port points of the decompiled play scene's draw and of the layout panes' draw (the decompilation's
// port_hooks.h PORT_PLAY_W, PORT_PANE_AT; tools/decomp_build/port_sites.py): game_hooks.cpp's world-system hold on
// in-between interpolation passes and aspect.cpp's clipping scope around Pane::Draw's DrawSelf, each through its
// instruction hook's handler.
#include <cstdint>

#include "port_points.h"
#include "runtime.h"

#if WWHD_PORT_POINTS  // (port_points.h: the decomp build and the tests that drive the points)
extern "C" void site_025B00B0(Cpu*);  // dScnPly_Draw: r3 the pause check's result
extern "C" unsigned wwhd_play_word(Cpu* c, unsigned sp, unsigned site, unsigned value) {
    if (site != 0x025B00B0) port_points::unknown("play", site);
    port_points::run(c, sp, site_025B00B0, [&](Cpu& r) { r.r[3] = value; }, [&](Cpu& r) { value = r.r[3]; });
    return value;
}

extern "C" void site_0287714C(Cpu*); extern "C" void site_02877150(Cpu*);  // Pane::Draw: before / after DrawSelf (r3)
extern "C" void wwhd_pane(Cpu* c, unsigned sp, unsigned site, unsigned pane) {
    if (site != 0x0287714C && site != 0x02877150) port_points::unknown("pane", site);
    port_points::run(c, sp, site == 0x0287714C ? site_0287714C : site_02877150, [&](Cpu& r) { r.r[3] = pane; },
                     [](Cpu&) {});
}
#endif  // WWHD_PORT_POINTS
