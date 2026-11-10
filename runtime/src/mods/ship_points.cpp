// Decomp build: the port points of the decompiled ship (the decompilation's port_hooks.h PORT_SHIP;
// tools/decomp_build/port_sites.py). Each runs its instruction hook's handler (mods/traversal.cpp: the sail speed and
// the automatic wind) through port_points::run, with the registers the handler reads at the hooked instruction.
#include <cstdint>

#include "port_points.h"
#include "runtime.h"
extern "C" {
void site_0247D4E8(Cpu*);  // setMoveAngle: f10 the speed the turn rate falls off with (r31 the ship)
void site_0247E988(Cpu*);  // procSteerMove: the normal-sail branch (r31 the ship): automatic wind
void site_0247EB88(Cpu*);  // procSteerMove: f29 the target speed (windPower)
void site_0247ED24(Cpu*);  // procSteerMove: before the jump code, f29 back to the stock target
}
extern "C" float wwhd_ship(Cpu* c, unsigned sp, unsigned site, unsigned ship, float value) {
    PpcFunc h;
    int f;
    switch (site) {
    case 0x0247D4E8: h = site_0247D4E8; f = 10; break;
    case 0x0247E988: h = site_0247E988; f = -1; break;
    case 0x0247EB88: h = site_0247EB88; f = 29; break;
    case 0x0247ED24: h = site_0247ED24; f = 29; break;
    default: port_points::unknown("ship", site);
    }
    port_points::run(c, sp, h,
                     [&](Cpu& r) {
                         r.r[31] = ship;
                         if (f >= 0) r.f[f].ps0 = r.f[f].ps1 = value;  // an lfs / fmuls result: both halves
                     },
                     [&](Cpu& r) { if (f >= 0) value = (float)r.f[f].ps0; });
    return value;
}
