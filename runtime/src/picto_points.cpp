// Decomp build: the port points of the decompiled Picto Box (the decompilation's port_hooks.h PORT_PICTO;
// tools/decomp_build/port_sites.py). Each runs its instruction hook's handler (aspect.cpp: the photo is the 16:9 part
// of a wider picture, issue #303; the WWHD_PICTO_LOG trace) through port_points::run, with the registers the handler
// reads at the hooked instruction.
#include <cstdint>

#include "port_points.h"
#include "runtime.h"

#if WWHD_PORT_POINTS  // (port_points.h: the decomp build and the tests that drive the points)
extern "C" {
void site_025BE898(Cpu*);                            // dSnap_packet::Execute after Judge: r31 the packet
void site_025BF5D0(Cpu*); void site_025BF7C0(Cpu*);  // draw: drawColorTexture / drawDepthTexture, r6 scale, r7 position
void site_025BFDF8(Cpu*);                            // DebugDraw: draw(r4 context)
void site_025BFFB0(Cpu*); void site_025C0004(Cpu*);  // DebugDraw: the subjects' projection, r4 the matrix
}
extern "C" void wwhd_picto(Cpu* c, unsigned sp, unsigned site, unsigned a, unsigned b) {
    PpcFunc h = nullptr;
    int ra = 4, rb = -1;
    switch (site) {
    case 0x025BE898: h = site_025BE898; ra = 31; break;
    case 0x025BF5D0: h = site_025BF5D0; ra = 6; rb = 7; break;
    case 0x025BF7C0: h = site_025BF7C0; ra = 6; rb = 7; break;
    case 0x025BFDF8: h = site_025BFDF8; break;
    case 0x025BFFB0: h = site_025BFFB0; break;
    case 0x025C0004: h = site_025C0004; break;
    default: port_points::unknown("picto", site);
    }
    port_points::run(c, sp, h, [&](Cpu& r) { r.r[ra] = a; if (rb >= 0) r.r[rb] = b; }, [](Cpu&) {});
}
#endif  // WWHD_PORT_POINTS
