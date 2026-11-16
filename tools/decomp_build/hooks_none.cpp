// Decomp build, packaging (cmake/Decomp.cmake WWHD_DECOMP_RUNTIME_ONLY): the runtime of the decomp build
// with no decompiled function. The release links it only to check the runtime; setup links its own hooks
// (tools/decomp_build/gen.py hooks.cpp) and game units in its place, with game code translated by
// recomp.py --decomp-hooks (which also defines g_recomp_decomp_kept).
#include "decomp/decomp.h"

// Weak: a placeholder translation (tools/recomp/stubgen.py --decomp-hooks) may define the same tables.
extern "C" {
__attribute__((weak)) extern const uint32_t g_recomp_decomp_kept[] = {0u};
__attribute__((weak)) extern const unsigned g_recomp_decomp_kept_count = 0;
__attribute__((weak)) extern const uint32_t g_recomp_decomp_import_calls[][2] = {{0u, 0u}};
__attribute__((weak)) extern const unsigned g_recomp_decomp_import_call_count = 0;
}

namespace decomp {
const char* const kUnits[] = {""};
const unsigned kNumUnits = 0;
const UnitAlias kAliases[] = {{"", 0}};
const unsigned kNumAliases = 0;
Slot g_slots[] = {{0u, 0, "", nullptr}};
const unsigned kNumSlots = 0;
const uint32_t kFrameExact[] = {0u};
const unsigned kNumFrameExact = 0;
const uint32_t kGuestHooked[] = {0u};
const unsigned kNumGuestHooked = 0;
}  // namespace decomp

#ifdef WWHD_DECOMP_FAST
// fast mode: the runtime's direct entries (hostrt_fast.cpp, hostrt_cxx.cpp) call these, which the game units' glue
// defines (tools/decomp_build/glue.cpp); stand-ins so the release's runtime-only build links. Setup links its own
// units in place of this file.
struct Cpu;
extern "C" {
int gabi_fast_native_ctx(Cpu*) { return 0; }
Cpu* gabi_fast_cpu() { return nullptr; }
}
#endif
