/* Example guest mod: replaces cLib_addCalc2 (step a value towards a target by a fraction, at most
 * max_step) with an equivalent implementation, and every other call uses the game's own code through
 * WWHD_GAME_ORIGINAL. Behaviour is unchanged; the log shows the call counts.
 * Semantics as documented by the public GameCube decompilation (zeldaret/tww, CC0,
 * src/SSystem/SComponent/c_lib.cpp); this file is written for the example. */
#include "wwhd_guest.h"

#include "game/functions.h"

#define ADDCALC2 WWHD_ADDR_cLib_addCalc2_hd

WWHD_GAME_ORIGINAL(ADDCALC2, void, addcalc2_original, (f32* value, f32 target, f32 scale, f32 max_step));

static u32 calls, own;

WWHD_REPLACE(ADDCALC2, void, addcalc2, (f32* value, f32 target, f32 scale, f32 max_step)) {
    ++calls;
    if ((calls & 0xFFFF) == 1) {
        wwhd_log_int("addcalc-replace: calls", (int)calls);
        wwhd_log_int("addcalc-replace: handled by the mod", (int)own);
    }
    if (calls & 1) {
        addcalc2_original(value, target, scale, max_step);
        return;
    }
    ++own;
    f32 v = *value;
    if (v == target) return;
    f32 step = (target - v) * scale;
    if (step > max_step) step = max_step;
    else if (step < -max_step) step = -max_step;
    *value = v + step;
}
