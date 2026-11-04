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
}  // namespace decomp
