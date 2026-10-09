/* Match a generated game body: C linkage, Cpu* restrict, and no C++ containers or
 * exception cleanups in the translation unit containing the musttail entry.
 * Keep this separate from the C++ hook runner, as real generated game code is. */
#include "ppc.h"

void guest_hooks_record_body(void);

void guest_hooks_body(Cpu* __restrict c) {
    PPC_ENTER(0x02000000u);
    PPC_MOD_HOOK(0, 0x02000000u);
    guest_hooks_record_body();
    c->r[3] += 1;
}
