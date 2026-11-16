// The environment's draw-time state on passes without logic (frame interpolation hold passes).
//
// The KANKYO process's draw (dKy_Draw 0255B2D8 -> dScnKy_env_light_c::drawKankyo 0255A924) does not only draw: it
// advances state that the next step reads, once per call. setLight (02558C40) -> setLight_palno_get (025580FC) steps the
// colour-pattern blend (mColPatBlend +0xFC8: a weather or palette change, at sea 1/300 per call), the start-up timer
// (mInitAnimTimer +0x109C) and, at the blend's end, the palette selection (mEnvrIdxPrev / mColpatPrev); setLightPosHD
// eases the day / night light fade (mDayNightFade) and the dungeon room light blend; SetBaseLight eases the sea light
// blend (mSeaLightBlend +0x1040); dKy_Itemgetcol_chg_move and drawKankyo itself count the item-get and arrow colour
// flashes (mColChgFlag +0x109B, mColChgRatio +0x101C, the flash light's colour and its point-light slot). At 30 fps the
// draw runs once per logic step; with frame interpolation it ran on every pass, so all of these went 2x / 4x / 8x as
// fast at 60 / 120 / 240 fps (a weather colour change, the evening light fade, an arrow's colour flash).
//
// On the step's logic pass the environment light (g_env_light, 10475A68) is saved before and after the draw. On a pass
// without logic, each word that draw changed and nothing has changed since is put back to its value from before, so the
// hold pass's draw starts where the logic pass's did and ends where it ended (the same inputs: logic does not run
// between them; values taken from the drawn camera, like the sun position, are recomputed from the pass's camera as
// before). The state then advances once per logic step, as at 30 fps; every pass draws the step's colours.
// True 60 (decomp build): the steps before the tick30 step are such passes, so the environment's draw-time state
// advances once per 30 Hz frame, on the tick30 step's draw, after the frame's KANKYO execute as in the original.
// debug: WWHD_INTERP_KANKYO_KEEP=0 turns this off (the draw advances on every pass, as before);
// WWHD_INTERP_KANKYO_TRACE=n logs n hold passes that put words back
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "guest_addr.h"
#include "runtime.h"

namespace interp {
bool enabled();
bool hold_pass();
uint64_t logic_steps();
}  // namespace interp

namespace interp {
namespace {
const uint32_t kEnvLight = GD(0x10475A68);  // g_env_light (dKy_getEnvlight 02555D0C)
constexpr uint32_t kEnvWords = 0x1208 / 4;  // dScnKy_env_light_c up to its vtable pointer (+0x1208)
constexpr uint32_t kKankyoSubMethod = 0xDC;  // kankyo_class::sub_method (kankyo_method_class*: draw at +0x10)
struct KankyoKeep {
    uint64_t step = ~0ull;
    bool valid = false;
    std::vector<uint32_t> before = std::vector<uint32_t>(kEnvWords), after = std::vector<uint32_t>(kEnvWords);
} g_ky;
bool keep_on() {
    static const bool off = getenv("WWHD_INTERP_KANKYO_KEEP") && !atoi(getenv("WWHD_INTERP_KANKYO_KEEP"));
    return !off;
}
// the process is the KANKYO process (its draw is dKy_Draw)
bool is_kankyo(uint32_t proc) {
    static const uint32_t draw = GC(0x0255B2D8);
    const uint32_t sub = ld32(proc + kKankyoSubMethod);
    return sub >= 0x10000000 && sub < 0x50000000 && !(sub & 3) && ld32(sub + 0x10) == draw;
}
void snap(std::vector<uint32_t>& v) {
    const uint32_t* m = (const uint32_t*)ppc_ptr(kEnvLight);
    for (uint32_t i = 0; i < kEnvWords; i++) v[i] = m[i];
}
}  // namespace

// fpcM_Draw (game_hooks.cpp hook_025DF904): around every process draw
void kankyo_draw(uint32_t proc, bool begin) {
    if (!enabled() || !keep_on() || !is_kankyo(proc)) return;
    KankyoKeep& k = g_ky;
    const uint64_t step = logic_steps();
    if (!hold_pass()) {  // the step's logic pass: where its draw starts and ends
        if (begin) {
            snap(k.before);
            k.step = step;
            k.valid = false;
        } else if (k.step == step) {
            snap(k.after);
            k.valid = true;
        }
        return;
    }
    if (!begin || !k.valid || k.step != step) return;
    uint32_t* m = (uint32_t*)ppc_ptr(kEnvLight);
    int n = 0;
    for (uint32_t i = 0; i < kEnvWords; i++)
        if (k.before[i] != k.after[i] && m[i] == k.after[i]) {
            m[i] = k.before[i];
            n++;
        }
    static int trace = getenv("WWHD_INTERP_KANKYO_TRACE") ? atoi(getenv("WWHD_INTERP_KANKYO_TRACE")) : 0;
    if (trace > 0 && n) {
        trace--;
        LOG("[interp] step %llu: environment draw state put back (%d words) before a pass without logic", (unsigned long long)step, n);
    }
}
}  // namespace interp
