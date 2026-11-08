// Speedrun options (Settings > Mods > More > Speedrun).
//
// Classic Wind Waker (issue #405): HD added two checks to the Wind Waker that the GameCube game does not have.
//  - daPy_lk_c::procTactWait cancels the Wind Waker as soon as Link starts to fall (the "falling check": beq at
//    0243A28C keeps r30 = 1, "cancel", while Link's ground bit is clear). That stops the ledge-climb dive.
//  - daPy_lk_c::endDemoMode puts Link back into his waiting state when the Wind Waker ends (023F22B0..023F22E0, for
//    every procedure but 201). That stops "dry storage" (cancelling the Wind Waker as Link lands).
// The TWWHD Randomizer's Classic mode (classic_features.asm, by SuperDude88, MIT) removes both: a nop at 0243A28C and
// `b 0x023F22E4` at 023F22B0. The two instruction sites below (tools/recomp/hooks_speedrun.txt) do exactly that while
// the switch is on. They sit next to the patched words, not on them, so a Classic-mode randomizer seed (whose words
// are already patched) keeps canonical hook words, and with the switch on as well nothing changes twice:
//  - @0243A294 is reached from the falling check's beq (r3 = 0: the cancel case HD added), from 0243A278 (r3 != 0:
//    the game's own cancel, kept) and by falling through 0243A290 (r30 = 0 already). On: r30 = 0 when r3 = 0, which is
//    what the nop gives; r0, r12 and cr0 are as the nop leaves them (cr0 is set again by the next instruction).
//  - @023F22B4 is the beq after `cmpwi r30, 0` (023F22B0), reached only for procedures other than 201. On: taken, so
//    the code jumps to 023F22E4 as the randomizer's branch does (r30 is reloaded from the stack there; cr0 is volatile).
// Off, both sites return at once: the game's own code runs, and they run only while Link conducts or a demo ends.
// Start-up / test switch: WWHD_MOD_CLASSIC_TACT=1. Saved as mod.classic-wind-waker.enabled (mods/manager.cpp).
#include <atomic>
#include <cstdlib>

#include "mods.h"
#include "ppc.h"

namespace mods {
namespace {
bool env_on(const char* n) {
    const char* e = std::getenv(n);
    return e && std::atoi(e) != 0;
}
std::atomic<bool> g_classic_tact{env_on("WWHD_MOD_CLASSIC_TACT")};
}  // namespace

bool classic_wind_waker() { return g_classic_tact.load(std::memory_order_relaxed); }
void set_classic_wind_waker(bool on) { g_classic_tact.store(on, std::memory_order_relaxed); }
}  // namespace mods

// daPy_lk_c::procTactWait, just after HD's falling check (see above)
extern "C" void site_0243A294(Cpu* c) {
    if (!mods::classic_wind_waker()) return;
    if (c->r[3] == 0) c->r[30] = 0;  // the falling check's cancel; the game's own (r3 != 0) stays
}

// daPy_lk_c::endDemoMode: the beq of `cmpwi r30, 0` (HD's wait-state reset after the Wind Waker, see above)
extern "C" void site_023F22B4(Cpu* c) {
    if (!mods::classic_wind_waker()) return;
    c->cr[2] = 1;  // cr0.eq: branch to 023F22E4
}
