// Decomp build: the port points of the decompiled attention code (the decompilation's port_hooks.h PORT_LOCK_ON,
// PORT_LOCK_ON_DISTANCE; tools/decomp_build/port_sites.py). Each runs its instruction hook's handler (mods/lock_on.cpp:
// the enemy lock-on range) through port_points::run with the registers it reads there; the values it may scale are
// passed and returned in double precision, as they were in the hooked FPRs.
#include <cstdint>

#include "port_points.h"
#include "runtime.h"
extern "C" {
void site_024EB4A8(Cpu*); void site_024ECC70(Cpu*);  // calcWeight, chaseAttention: check_distace's f1, f2
void site_024EB868(Cpu*);                            // EnemyDistance: the cutoff f13
void site_024EE574(Cpu*);                            // LockonReleaseDistanse: the release radius f1
}
// OBJ: calcWeight the type-table entry (r31), chaseAttention the attention-list entry (r28), EnemyDistance the actor
// (r31), LockonReleaseDistanse the attention (r31); ACTOR (the distance points): the actor checked (calcWeight r25,
// chaseAttention its attention_info r30 = actor + 0x388)
extern "C" double wwhd_lock_on(Cpu* c, unsigned sp, unsigned site, unsigned obj, unsigned actor, double* v) {
    auto set = [](Cpu& r, int i, double x) { r.f[i].ps0 = r.f[i].ps1 = x; };
    switch (site) {
    case 0x024EB4A8: case 0x024ECC70: {
        const bool weight = site == 0x024EB4A8;
        port_points::run(c, sp, weight ? site_024EB4A8 : site_024ECC70,
                         [&](Cpu& r) {
                             if (weight) { r.r[31] = obj; r.r[25] = actor; }
                             else { r.r[28] = obj; r.r[30] = actor + 0x388; }
                             set(r, 1, v[0]); set(r, 2, v[1]);
                         },
                         [&](Cpu& r) { v[0] = r.f[1].ps0; v[1] = r.f[2].ps0; });
        return v[0];
    }
    case 0x024EB868: case 0x024EE574: {
        const int f = site == 0x024EB868 ? 13 : 1;
        port_points::run(c, sp, site == 0x024EB868 ? site_024EB868 : site_024EE574,
                         [&](Cpu& r) { r.r[31] = obj; set(r, f, v[0]); }, [&](Cpu& r) { v[0] = r.f[f].ps0; });
        return v[0];
    }
    }
    port_points::unknown("lock-on", site);
}
