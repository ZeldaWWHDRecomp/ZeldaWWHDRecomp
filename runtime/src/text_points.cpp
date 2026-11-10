// Decomp build: the port points of the decompiled text code (the decompilation's port_hooks.h PORT_TEXT_W,
// PORT_TEXT_NAME; tools/decomp_build/port_sites.py): countdown.cpp's interpolated timer digits and language_region.cpp's
// German genitive of the player's name, each through its instruction hook's handler.
#include <cstdint>

#include "port_points.h"
#include "runtime.h"
extern "C" {
void site_026277A8(Cpu*); void site_026E9C24(Cpu*);  // the boat race's / the HUD's timer: r31 the layout, r7 the ms
void site_025F8618(Cpu*);                            // dMesg_state_c::playerName's entry: r5 the message id
}
void german_genitive_name(uint32_t buf, int32_t cap);                     // site_025F8720
void german_genitive_wide_name(uint32_t self, uint32_t buf, int32_t cap);  // site_025FC668
extern "C" unsigned wwhd_text(Cpu* c, unsigned sp, unsigned site, unsigned a, unsigned buf, int cap) {
    switch (site) {
    case 0x026277A8: case 0x026E9C24: {
        unsigned ms = buf;
        port_points::run(c, sp, site == 0x026277A8 ? site_026277A8 : site_026E9C24,
                         [&](Cpu& r) { r.r[31] = a; r.r[7] = ms; }, [&](Cpu& r) { ms = r.r[7]; });
        return ms;
    }
    case 0x025F8618:
        port_points::run(c, sp, site_025F8618, [&](Cpu& r) { r.r[5] = a; }, [](Cpu&) {});
        return 0;
    case 0x025F8720: case 0x025FC668: {
        const Cpu saved = *c;
        c->r[1] = sp;
        if (site == 0x025F8720) german_genitive_name(buf, cap);
        else german_genitive_wide_name(a, buf, cap);
        *c = saved;
        return 0;
    }
    }
    port_points::unknown("text", site);
}
