// Quick doors (turbo.cpp): which parts of a logic step (fpcM_Management) an extra step runs, and a
// model of the game's process deletion against drawing that runtime/tools/turbo_steps_test.cpp
// checks (ctest turbo_steps).
//
// Why the extra steps leave process deletion out (issue #61): fopAcM_delete / fpcDt_ToDeleteQ puts a
// process into the delete queue with a timer of 1 (fpcDtTg_ToDeleteQ) and takes it off the draw
// list; each fpcDt_Handler counts the timer down and the one after that deletes the process
// (fpcDtTg_Do), so a process is deleted two frames after it was queued. The J3D packets its draw
// entered into a draw buffer (J3DDrawBuffer::entryImm: packet +0x94 = the buffer slot) stay listed
// until that buffer's next frameInit, in the next frame's draw, and the GPU may still read its
// display lists; the timer gives every packet a draw (and a frameInit) before the process heap
// is destroyed. When quick doors ran fpcDt_Handler in each extra step too, a process queued in a
// door event's step was deleted two extra steps later in the same frame, with its packets from the
// last draw still listed: its heap destroy reached ~J3DPacket with mEntryPtr set,
// "PROGRAM HALT J3DPacket.cpp:157 mEntryPtr == (0)" (Tingle's jail on Windfall, both door ways).
// The extra steps now skip fpcDt_Handler: deletion keeps counting frames, as in the game.
#pragma once

namespace mods {

// parts of fpcM_Management one logic step runs (in the game's order)
struct StepParts {
    bool counter;   // cCt_Counter (end of the previous step)
    bool deletion;  // fpcDt_Handler: the delete queue (timers, deletes)
    bool priority;  // fpcPi_Handler
    bool creation;  // fpcCt_Handler
    bool execute;   // fpcEx_Handler: every process's execute
};

// the game's own step
constexpr StepParts kNormalStep{true, true, true, true, true};
// a quick-door extra step (turbo.cpp): everything but deletion, which stays once per frame
constexpr StepParts kDoorExtraStep{true, false, true, true, true};

}  // namespace mods
