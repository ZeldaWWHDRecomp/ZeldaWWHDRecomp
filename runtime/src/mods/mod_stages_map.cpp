// SPDX-License-Identifier: MPL-2.0
// Mod stages: the dungeon map (mod_stages.h). The game's GamePad map coordinator is followed through its
// updateMapKind (hdMapDisplayCoordinator_c): which view it shows, so the port can draw a mod stage's map.
#include <cstdlib>
#include <cstring>

#include "guest_addr.h"
#include "runtime.h"

extern "C" void f_026876F4_orig(Cpu* c);

namespace mod_stages_map {
uint32_t g_coordinator = 0;  // the game's map coordinator (its last updateMapKind)
bool trace() {
    static const bool on = [] { const char* e = getenv("WWHD_MOD_STAGE_MAP_TRACE"); return e && *e == '1'; }();
    return on;
}
}  // namespace mod_stages_map

using namespace mod_stages_map;

// hdMapDisplayCoordinator_c::updateMapKind(self)
extern "C" void hook_026876F4(Cpu* c) {
    const uint32_t self = c->r[3];
    f_026876F4_orig(c);
    if (!self) return;
    g_coordinator = self;
    if (!trace()) return;
    static uint32_t last[12] = {};
    const uint32_t now[12] = {ld32(self + 0x50), ld32(self + 0x54), ld8(self + 0x59), ld32(self + 0x5C), ld32(self + 0x60), ld32(self + 0x64),
                              ld8(self + 0x68), ld8(self + 0x69), ld32(self + 0x6C), ld32(self + 0x70), ld8(self + 0x74), ld32(self + 0x10 + 8)};
    if (!memcmp(now, last, sizeof now)) return;
    memcpy(last, now, sizeof now);
    LOG("[mod-stages] map coordinator %08X: kind %d/%d interior %u detected %u mode %u prev %u opened %u visible %u input %u flags %08X link %u state %08X",
        self, int(now[0]), int(now[1]), now[2], now[3], now[4], now[5], now[6], now[7], now[8], now[9], now[10], now[11]);
}
