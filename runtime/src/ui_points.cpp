// Decomp build: the port points of the decompiled title, file select, Options and input code (the decompilation's
// port_hooks.h PORT_UI_W, PORT_UI_AT, PORT_UI_EVENT, PORT_UI_STATE; tools/decomp_build/port_sites.py). Each runs its
// instruction hook's handler (mods/one_screen.cpp: One screen; motion/game_hooks.cpp: gyro aiming with a Pro
// Controller; controller_selection.cpp: the automatic controller choice) through port_points::run, with the registers
// the handler reads at the hooked instruction.
#include <cstdint>

#include "port_points.h"
#include "runtime.h"
extern "C" {
void site_02617874(Cpu*);  // hdInputManager::init: r0 the mode stored to +0x1D0
void site_0261864C(Cpu*);  // orientation: r3 isTvMode()'s result
void site_026D5D84(Cpu*);  // hdSequenceOption_c::loadSettings: r31 the window, its values loaded
void site_026D8514(Cpu*);  // hdSequenceOption_c::open: r7 the item count stored to +0x120
void site_0270AC04(Cpu*);  // hdSaveDataScene_c::updateControllerChoice: changeState(r3 machine, r4 state)
void site_0270ADD0(Cpu*);  // hdSaveDataScene_c::updateSaveSelection: the same
}
void controller_select_event(Cpu* c, uint32_t screen, uint32_t event);  // site_026D44A8 with r28, sp+0x30 passed
namespace {
struct UiPoint { uint32_t site; void (*handler)(Cpu*); int8_t obj, value; };  // registers (-1: none)
constexpr UiPoint kUiPoints[] = {
    {0x02617874, site_02617874, -1, 0}, {0x0261864C, site_0261864C, -1, 3}, {0x026D5D84, site_026D5D84, 31, -1},
    {0x026D8514, site_026D8514, -1, 7}, {0x0270AC04, site_0270AC04, 3, 4},  {0x0270ADD0, site_0270ADD0, 3, 4},
};
}  // namespace
extern "C" unsigned wwhd_ui_word(Cpu* c, unsigned sp, unsigned site, unsigned obj, unsigned value) {
    if (site == 0x026D44A8) {  // hdTitleControllerSelection_c::updateSelection: OBJ the screen, VALUE the event
        const Cpu saved = *c;
        c->r[1] = sp;
        c->r[28] = obj;
        controller_select_event(c, obj, value);
        *c = saved;
        return value;
    }
    for (const UiPoint& p : kUiPoints) {
        if (p.site != site) continue;
        port_points::run(c, sp, p.handler,
                         [&](Cpu& r) {
                             if (p.obj >= 0) r.r[p.obj] = obj;
                             if (p.value >= 0) r.r[p.value] = value;
                         },
                         [&](Cpu& r) { if (p.value >= 0) value = r.r[p.value]; });
        return value;
    }
    port_points::unknown("ui", site);
}
