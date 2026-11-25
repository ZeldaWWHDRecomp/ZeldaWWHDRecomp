// True 60 Hz game logic, decomp build only (WWHD_DECOMP): the timebase the decompiled game code reads (the
// decompilation's tools/verify/include/gabi_t60.h and wwhd_src/include/true60.h: g_dt, g_tick30, N) and the step driver
// that runs N logic steps per 30 Hz frame. The normal (recompiled) build has
// none of this: its logic stays at 30 Hz.
//
// Steps ride on frame interpolation's passes (interp.cpp). With interpolation at P passes per 30 Hz frame (P a
// multiple of N), a 30 Hz frame's N steps are the in-between passes P/N, 2P/N, ... of the frame before and then the
// next logic pass, which is the tick30 step that ends the frame: there the 30 Hz world runs (every process, counters,
// audio, the scene's world block, particles). On the earlier steps only the converted processes execute (the
// decompilation's fpcEx_Execute rate gate); everything else on such a pass stays an in-between pass as before (all of
// interpolation's holds of logic in the draw path apply), the 30 Hz world drawn interpolated. N = 1 (the default) is
// the 30 Hz game unchanged.
#pragma once
#include <cstdint>

// the state the decompiled code reads (gabi_t60.h, true60.h; default visibility: the game units bind to these)
extern "C" {
extern float wwhd_t60_dt;          // g_dt: the running code's step length (1 outside converted code)
extern uint32_t wwhd_t60_tick30;   // g_tick30: this step ends a 30 Hz frame
extern uint32_t wwhd_t60_steps;    // N of this 30 Hz frame (1: 30 Hz, also during the fallback)
extern uint32_t wwhd_t60_first;    // the first step of a 30 Hz frame: the game's one input read of the frame (2026-11-15)
extern uint32_t wwhd_t60_stepk;    // the step within the 30 Hz frame, 1..N (N on the tick30 step)
extern int wwhd_t60_world60;       // set by the play scene's execute (tick30): the next frame may run at N
extern int wwhd_t60_scene_first;   // frame-level work on the frame's first step (default 1)
extern int wwhd_t60_test_convert;  // tests (WWHD_T60_TEST_CONVERT): -1 all processes count as converted, else a profile
// tests (WWHD_T60_TEST_CONVERT_FROM): the test window has not opened yet: no process counts as converted, the
// game's default list (decomp true60.h kT60Converted) included, so both runs of a pair execute alike at 30 Hz before it
// (the keyed test RNG starts at the scenario origin: a default-converted actor's pre-origin draw took the game RNG in
// another cross-process order, cckA4_4: a ChuChu's idle timer 4 frames short). Never set without the variable.
constexpr int kT60TestWindowClosed = -2;
// the test conversion of one profile (WWHD_T60_TEST_CONVERT=all or a comma list; true60.h t60_converted asks it)
int wwhd_t60_test_converted(int profName);
// tests (WWHD_T60_TEST_UNCONVERT=<list>, no-conversion controls): the first profile of the list (0: unset / empty), and
// whether a profile is excluded (true60.h t60_converted returns false for it, the default list included)
extern int wwhd_t60_test_unconvert;
int wwhd_t60_test_unconverted(int profName);
int t60_test_unconvert_set(const char* e);  // tests: parse a list as the variable would (nullptr / "": none)
// the list parser (tests): "168,0x1DC" -> out {168, 0x1DC, 0}; returns the first profile (0: none)
int t60_parse_convert_list(const char* e, int* out, int max);
// the frame-level work's switch from WWHD_T60_SCENE_FIRST (tests): unset -> 1 (default on), "0" -> 0
int t60_scene_first_from_env(const char* e);
extern float wwhd_t60_ran;         // the rate gate's last verdict: the execute's dt, 0 held back (-1: not asked)
struct T60Pad {
    uint32_t hold, trig;  // this step's raw buttons, pressed since the previous poll
    float stick[4];       // left x, y, right x, y
};
extern T60Pad wwhd_t60_pad;
// The step counter for converted code (true60.h t60_step_counter), advanced by fapGm_Execute once per logic step
extern uint32_t wwhd_t60_step_counter;
// the 30 Hz frame the running step ends (record_step); the per-frame holds below latch by it
extern uint32_t wwhd_t60_frame;
// the process whose execute runs (its guest address) (0 outside an execute; test aid WWHD_TEST_RNG_KEYED)
extern uint32_t wwhd_t60_exec_proc;
// 1 inside an execute (decomp fpcEx_Execute, T60_EXEC_LOGIC), 0 in a draw / create method outside one (test aid
// WWHD_TEST_RNG_KEYED: only draws outside an execute count on a hold pass's own counters)
extern uint32_t wwhd_t60_exec_logic;
// test aid WWHD_TEST_PROC_ID_BASE (savestate.cpp): the next process id after a portable state load (decomp f_pc_base.cpp
// fpcBs_MakeOfId takes it once and clears it; 0: no change)
extern uint32_t wwhd_test_proc_id_base;
// Per-frame holds: a converted process runs tick30-only from the next 30 Hz frame on (true60.h t60_hold30_request / t60_held)
void wwhd_t60_hold30_request(unsigned procId);
int wwhd_t60_held(unsigned procId);
// the decompiled sead::ControllerManager::calc: the pass about to run is its 30 Hz frame's input pass (2026-11-15)
int wwhd_t60_input_pass();
// True 60 fps input: the pass about to run is a logic step (any step of its 30 Hz frame; every pass at N = 1):
// the game's controller reads the GamePad there
int wwhd_t60_input_step();
// set by the decompiled sead::ControllerManager::calc at the start of each pass: bit 0 the controllers read the GamePad on
// this pass, bit 1 that read is a later step's (not its frame's first: the frame form of sead::InputState::update)
extern uint32_t wwhd_t60_in_read;
// the edges of the last read per controller state (sead::InputState, by guest address): press (+0), release (+4) and
// repeat (+8) of that read alone; the guest words hold the frame's union (decomp sead/InputState.cpp)
void wwhd_t60_in_edges(uint32_t state, uint32_t press, uint32_t release, uint32_t repeat);
// one of them (offset 0 / 4 / 8) into *word; 0 when the state was never read
int wwhd_t60_in_step_word(uint32_t state, uint32_t offset, uint32_t* word);
}

namespace t60 {
int selected_n();   // the chosen steps per 30 Hz frame: 1 (default), 2, 4 (WWHD_T60_STEPS=N)
void set_n(int n);  // (tests: at once)
// the settings' Frame rate (true logic): request_n takes effect at the next 30 Hz frame boundary (the logic pass that
// ends the running frame, before the next frame's N is decided), so a change never splits a frame; any thread.
// requested_n: the N chosen, a pending change included (menus show it). WWHD_T60_STEPS wins over the saved setting
// (n_from_env: it is not loaded and not saved then; tests); load_saved_n: the saved setting at start-up.
void request_n(int n);
int requested_n();
bool n_from_env();
void load_saved_n(int n);
// logic steps run so far, every true-60 step counted (the overlay's "true logic" steps/s; the 30 Hz frames are
// interp::executed_steps); any thread
uint64_t steps_run();
// interp.cpp, at the start of each pass of the per-frame function: whether frame interpolation runs its pass structure,
// whether this is an in-between (hold) pass, its phase (0: the logic pass), the passes of this 30 Hz frame (P) and the
// 30 Hz step clock (interp::logic_steps, already counting this frame's logic pass)
void pass_begin(bool interp_on, bool hold, int phase, int passes, uint64_t logic_steps);
// tests: the scripted scenario's time (input_sdl apply_scenario), for WWHD_T60_TEST_CONVERT_FROM
void test_scenario_time(double t);
double test_scenario_time();  // the last one set (-1: none yet); WWHD_TEST_WARP_AT=tSECONDS:... (savestate.cpp)
bool active();      // the steps of the running 30 Hz frame are N > 1 (selected, the passes fit, no fallback)
bool sub_step();    // this pass is a step before the tick30 step (an in-between pass whose converted processes execute)
bool tick30();      // this pass is a tick30 step (the logic pass; every pass without true 60)
bool first_step();  // this pass is the first step of its 30 Hz frame (the game reads the GamePad here; live, not repeated)
// Frame-level work (WWHD_T60_SCENE_FIRST): this pass is an in-between pass that runs its frame's frame-level work (the
// frame's first step at N > 1): frame interpolation's hold-pass skips of fpcM_Management's process handlers let it run
bool frame_work_hold_pass();
// the pass about to begin (frame interpolation's look-ahead: whether it is a hold pass, its phase and the passes per
// frame) is the first step of its 30 Hz frame; the game's controller read (sead::ControllerManager::calc) runs at the
// start of a pass, before pass_begin, so it asks this (wwhd_t60_input_pass, interp.cpp)
bool next_pass_first(bool interp_on, bool hold_next, int next_phase, int passes);
// True 60 fps input: the pass about to begin is a logic step (the tick30 logic pass, or an in-between pass that runs a step)
bool next_pass_step(bool interp_on, bool hold_next, int next_phase, int passes);
// test aid WWHD_T60_TEST_INPUT_STEP: where in its 30 Hz frame the step of the pass about to begin lies, in
// frames (step k of N: k / N; 0 for a frame of 1 step and for a pass that runs no step)
double next_pass_step_offset(bool interp_on, bool hold_next, int next_phase, int passes);
bool wants_unpaced();  // N > 1 is selected: in-between passes must not be skipped (paced interpolation off)
int wants_fps();       // the frame interpolation rate N > 1 needs (30 N; 0: none)
// the 30 Hz scenario step a trace record of this pass belongs to (the frame its step ends): logic_steps on the tick30
// step, logic_steps + 1 on the steps before it (WWHD_T60_STEPLOG and the traces' step column)
uint64_t record_step(uint64_t logic_steps);
// the time (in 30 Hz frames, on the step clock) the state of a process that executes on this pass's step is at: the
// clock S on the tick30 step, S + j/N on the frame's earlier step j (t60_draw.h)
double step_time();
void at_origin(uint64_t origin_step);  // test aid WWHD_T60_AT_ORIGIN=d (t60.cpp): N from the run's origin + d
// Input (hle/system_stubs.cpp VPADRead). Frame interpolation reads the pad live on the logic pass and repeats that
// sample on in-between passes, so the 30 Hz world sees each change once. While true 60 runs, the in-between reads poll
// the device too (poll_every_read), for converted code (wwhd_t60_pad: every step's raw input); the game's own controller
// keeps the repeated sample there. A button pressed and released again between two live reads is added to the next
// live read (game_buttons), so the 30 Hz world sees every press exactly once.
bool poll_every_read();
void pad_poll(uint32_t raw_buttons, const float sticks[4], bool live_read);  // every read: raw device state
uint32_t game_buttons(uint32_t hold);  // the live read's buttons for the game (with the latched presses)
}  // namespace t60
