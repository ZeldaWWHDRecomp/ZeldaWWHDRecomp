// Test aid of the true 60 softlock sweep (decomp build only): WWHD_T60_EVLOG=path writes one line per logic step,
// `frame tick30 sub mode`: the 30 Hz frame the step ends, the tick30 flag, the step's index in its frame (-1 at N 1), and
// dEvt_control_c::mMode (play + 0x5292, nonzero while an event runs) as the step begins, i.e. after the previous step.
// WWHD_EVENT_TIMELINE samples once per 30 Hz frame; this gives the event's start and end to the step (the sweep's
// "no wait longer than the 30 Hz duration + 1 step" gate). Reads guest memory only.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "guest_addr.h"
#include "ppc.h"

extern void (*t60_evlog_step)(unsigned long long frame, int tick, int sub);
static FILE* g_evlog = nullptr;
static void evlog_step(unsigned long long frame, int tick, int sub) {
    FILE* f = g_evlog;
    fprintf(f, "%llu %d %d %u\n", frame, tick, sub, (unsigned)ld8(GD(0x1046F0B0) + 0x5292));
    fflush(f);
}
// armed before main (t60.cpp's pointer is constant-initialised): only with the env var, so the hook is null otherwise
static const bool g_evlog_armed = [] {
    if (const char* p = getenv("WWHD_T60_EVLOG")) g_evlog = fopen(p, "w");
    if (g_evlog) t60_evlog_step = evlog_step;
    return true;
}();
