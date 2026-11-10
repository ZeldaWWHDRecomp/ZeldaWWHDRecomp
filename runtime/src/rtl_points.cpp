// Decomp build: the port points of the decompiled text measuring and printing (CalcLineRectImpl, PrintImpl: the
// decompilation's port_hooks.h PORT_RTL, PORT_RTL_AT; tools/decomp_build/port_sites.py). Each runs its instruction
// hook's handler (rtl_text_hooks.cpp: right-to-left text, line breaks after a space) through port_points::run. The
// handlers key on the function's frame (r1 = the point's sp) and read its frame locals, which the decompiled functions
// keep at the original's offsets (gabi::FrameLocal: the reader at +0xD4 / +0xC4, the scan context at +0x20).
#include <cstdint>

#include "port_points.h"
#include "runtime.h"
extern "C" {
void site_02870028(Cpu*); void site_028704CC(Cpu*);  // CalcLineRectImpl: a new line; r20 code, r22 wraps, r24 break
void site_02870A50(Cpu*); void site_02870CC8(Cpu*);  // PrintImpl: after the prologue; r22 the control code (tags)
void site_02870CCC(Cpu*); void site_02870FC8(Cpu*);  // after the tag; r22 code, r24 wraps, r25 break, r26 line start
}
extern "C" unsigned wwhd_rtl(Cpu* c, unsigned sp, unsigned site, unsigned limited, unsigned code, unsigned pos,
                             unsigned line) {
    switch (site) {
    case 0x02870028: case 0x02870A50: case 0x02870CCC:
        port_points::run(c, sp, site == 0x02870028 ? site_02870028 : site == 0x02870A50 ? site_02870A50 : site_02870CCC,
                         [](Cpu&) {}, [](Cpu&) {});
        return pos;
    case 0x02870CC8:
        port_points::run(c, sp, site_02870CC8, [&](Cpu& r) { r.r[22] = code; }, [](Cpu&) {});
        return pos;
    case 0x028704CC:
        port_points::run(c, sp, site_028704CC, [&](Cpu& r) { r.r[20] = code; r.r[22] = limited; r.r[24] = pos; },
                         [&](Cpu& r) { pos = r.r[24]; });
        return pos;
    case 0x02870FC8:
        port_points::run(c, sp, site_02870FC8,
                         [&](Cpu& r) { r.r[22] = code; r.r[24] = limited; r.r[25] = pos; r.r[26] = line; },
                         [&](Cpu& r) { pos = r.r[25]; });
        return pos;
    }
    port_points::unknown("rtl", site);
}
