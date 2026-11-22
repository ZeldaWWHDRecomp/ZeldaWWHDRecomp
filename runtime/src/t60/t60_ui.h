// The in-game UI's draw-path logic on a general step length (true 60 Hz logic, decomp build only).
//
// Two UI updates run from the draw path, not from the process executor, so the per-process rate gate does not reach
// them; frame interpolation holds them on its in-between passes, which with true 60 are the frame's earlier steps:
//   HUD  the HUD meter's per-frame update dMeter_Execute 02593B10 (from the play scene's draw; game_hooks.cpp
//        hook_02593B10, issue #262)
//   HDUI the HD UI manager's screen updates after fpcM_Management 02715310 (interp.cpp hook_02715310, issues #64, #74)
// The decompilation names the UI systems it converted (d/d_meter_t60.h kT60UiConverted, a bit each, exported as
// wwhd_t60_ui_converted). On every step of an N-step 30 Hz frame (the earlier steps and the tick30 step) a converted
// system runs, with g_dt = 1/N set around it (it is not inside a converted process's execute); plain interpolation
// passes (no step) keep the hold. An unconverted system keeps the hold on the earlier steps and runs on the tick30
// step with dt 1, as before. With no converted system (the mask 0: today) nothing changes.
//
// debug: WWHD_T60_UI=0 treats every UI system as unconverted (the switch; default on).
#pragma once
#include <cstdint>

struct Cpu;

namespace t60::ui {
enum : uint32_t { kHud = 1u << 0, kHdUi = 1u << 1, kHdLayout = 1u << 2 };
uint32_t converted_mask();  // the decompilation's converted UI systems (0: none)
// this pass is a step of an N > 1 frame and the system steps on it (converted): run it with step_enter / step_leave
bool steps(uint32_t sys);
float step_enter();          // sets g_dt to 1/N; returns the previous g_dt
void step_leave(float prev);  // puts it back

// The HD layouts' picture between 30 Hz frames (decomp nw/lyt/LYT_t60.h). The HD
// in-game UI's logic, the layout animation advance included, runs whole once a frame on its first step at dt 1 (play
// scene execute); the decomp records each such update of an N > 1 frame (wwhd_t60_lyt_record). The
// panes A shows during frame f carry the frame each animator applied in that calc (its frame BEFORE the update); during
// frame f's steps the panes show the animators at prev + (before - prev) k/N (prev = the frame applied in frame f - 1,
// only for an animator that ran on continuously: its last update ended where this one began), the frames are put back
// right after re-applying, and the panes are re-applied at exactly `before` (= A's pane state) wherever logic or the
// next frame could read them (layout_exact). Decomp kT60Ui_HdLayout; WWHD_T60_UI=0 turns it off (the control).
void layout_present(Cpu* c);  // a step of an N > 1 frame: show the recorded animators at this step's fraction
void layout_exact(Cpu* c);    // re-apply every recorded animator at its frame end picture (A's panes); no-op if exact
}  // namespace t60::ui
