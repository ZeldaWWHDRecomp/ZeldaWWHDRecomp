// Debug aid of the true 60 pairs (decomp build only): WWHD_RND_LOG's per-call line gets the t60 step state and, with
// WWHD_RND_HBT=STEP, the host call chain from that logic step on. The decompiled game code calls cM_rnd natively, so the guest lr the log prints
// is stale there (the last guest call site, e.g. checkNextMode's call of setFrontWallType); the host chain names the
// native callers. (takeover 11: pham3's extra draw at frame 141.)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(__has_include)
#if __has_include(<execinfo.h>) && __has_include(<dlfcn.h>)
#include <dlfcn.h>
#include <execinfo.h>
#define WWHD_T60_RND_HBT 1  // (the host chain; no execinfo / dlfcn on Windows)
#endif
#endif
#include "ppc.h"
#include "t60.h"

void t60_rnd_log_extra(FILE* f, unsigned long long logic_step) {
    // (the keyed test RNG's inputs: the executing process's id and the record step)
    fprintf(f, " t%u s%u p%08X r%u", wwhd_t60_tick30, wwhd_t60_step_counter, wwhd_t60_exec_proc ? ld32(wwhd_t60_exec_proc + 4) : 0xFFFFFFFFu,
            wwhd_t60_frame);
    // WWHD_RND_HBT=STEP: from that logic step on (a chain per call slows the boot past the scenario's start timeout)
    static const unsigned long long hbt = getenv("WWHD_RND_HBT") ? strtoull(getenv("WWHD_RND_HBT"), nullptr, 0) : 0;
    if (!hbt || logic_step < hbt) return;
#ifdef WWHD_T60_RND_HBT
    void* bt[14];
    int n = backtrace(bt, 14);
    fprintf(f, " |");
    for (int i = 3; i < n; i++) {  // (0 this, 1 rnd_call, 2 the hook)
        Dl_info di;
        const char* s = dladdr(bt[i], &di) && di.dli_sname ? di.dli_sname : "?";
        // skip the gabi / host-call glue; print the game frames
        if (!strncmp(s, "_ZN4gabi", 8) || !strcmp(s, "ppc_host_call") || !strncmp(s, "hook", 4)) continue;
        fprintf(f, " %.60s+%lx", s, di.dli_saddr ? (unsigned long)((char*)bt[i] - (char*)di.dli_saddr) : 0ul);
    }
#else
    fprintf(f, " | (no host call chain on this platform)");
#endif
}
