// Hooks on the game's process loop (game_hooks.cpp): pass counter and Link's step clock.
#pragma once
#include <cstdint>

namespace game_hooks {
void new_pass();        // interp.cpp: a per-frame pass begins
uint64_t pass();        // per-frame passes so far
uint32_t link();        // Link's process once seen (0 before)
uint64_t link_steps();  // Link's executes since the last save-state load (test scenarios)
bool state_loaded();    // a save state was restored since boot
void ss_reset();        // a save state was loaded: forget Link and the step clock
void portable_arrived(); // a portable state's first frame in the new stage: the step clock starts (test scenarios)
// the record of the execute in progress (traces, actor dumps): its step (the 30 Hz logic step; true 60, decomp build:
// the 30 Hz frame the step ends), full (1; true 60: the tick30 step) and the execute's step length
uint64_t rec_step();
int rec_full();
float rec_dt();
}  // namespace game_hooks
