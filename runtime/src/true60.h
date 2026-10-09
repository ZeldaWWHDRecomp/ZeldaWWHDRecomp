// True 60 fps (game logic at 60 steps per second): see true60.cpp.
#pragma once
#include <cstdint>

struct Cpu;

namespace true60 {
bool enabled();
bool selected(); // saved choice, including while fast forward suspends 60 Hz passes
void set_enabled(bool v);
float dt();              // step length of the execute in progress: 1 = one original 30 Hz step
uint32_t exec_proc();    // process whose execute is in progress (0 outside)
bool half_pass();        // this pass only runs 60 Hz processes
int32_t split(int32_t v);  // integer per-step amount for this step (half steps add up to v)
void new_pass();
void pass_begin(bool full);  // after new_pass: a full pass first takes back the last half-pass preview
bool preview();
void camera_draw_preview(bool begin);  // around camera_draw on a half pass (interp.cpp)              // a 60 Hz execute on a half pass (a preview: no lasting effects)
uint64_t pass();
bool runs_60(uint32_t proc);  // the process executed at 60 Hz on this pass (not interpolated)
bool drawing_60();            // the process being drawn runs at 60 Hz on this pass
void force_draw_60(bool on);  // nested scope: drawing_60() is true (drawing built from 60 Hz state)
uint32_t link();
uint64_t link_steps();
bool state_loaded();          // a save state was restored since boot        // Link's full-pass executes since the last save-state load (test scenarios)              // Link's process once seen (0 before)
void ss_reset();              // a save state was loaded: forget processes and histories
// instruction-level hooks for per-step smoothing (tools/true60/gen_sites.py): the ratio r in
// fR becomes 1-(1-r)^dt for one instruction (begin/end) or for a call argument (arg)
void ratio_begin(Cpu* c, int r);
void ratio_end(Cpu* c, int r);
void ratio_arg(Cpu* c, int r);
float set_dt(float dt);  // step length for the code that follows (returns the previous one)
// conversion groups (WWHD_TRUE60_GROUPS): each converted part can be switched off on its own
enum Group { kGrpLoco, kGrpCamera, kGrpSword, kGrpItems, kGrpSwim, kGrpSail, kGrpBk, kGrpMo2, kGrpCc, kGrpKi, kNumGroups };
bool group_on(Group g);
// how Link runs in his current procedure: 0 = 30 Hz (interpolated), 1 = 60 Hz (the procedure
// itself runs on every pass with dt = 0.5), 2 = 60 Hz motion with the procedure's own logic on
// full passes only (dt = 1 there; true60_link.cpp)
int link_proc_mode(uint32_t proc_id);
}  // namespace true60
