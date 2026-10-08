// Gyro aiming in Pro Controller mode (issue #71): the game's own hook (tools/recomp/hooks_gyro.txt).
//
// WWHD's controller manager (singleton 101F5088, decompiled in wwhd_src/d/hd_input_ctrl.cpp) keeps a
// controller mode: 0 when the game plays with a Pro Controller, 1/2 with the GamePad, 3 with both. Every
// frame it copies the GamePad's direction matrix (026173B0) whatever the mode, and the first-person camera
// (dCamera_c::CalcSubjectAngle) asks 02618604 for the calibrated GamePad orientation. 02618604 returns the
// identity, so no motion, when the in-game Gyro option is off or the mode is 0: on the Wii U a Pro
// Controller has no gyro and the GamePad lies on the table.
//
// The port's GamePad is virtual and the gyro belongs to the player's own controller, so in Pro Controller
// mode the mode check is skipped while a gyro source is on: the game then reads the motion exactly as with
// the GamePad, through the same camera code (bow, hookshot, boomerang, telescope, Picto Box, grappling hook,
// R3 look), with its Gyro option, its 0.1 right-stick dead zone (now the Pro Controller's stick) and its
// per-frame calibration. With the gyro source off nothing changes.
#include "input.h"
#include "motion.h"
#include "runtime.h"

// 02618604 (orientation), right after `bl 02617AE4` (is the mode 0?): r3 = its answer
extern "C" void site_0261864C(Cpu* c) {
    if (c->r[3] != 0 && input::pro_controller() && motion::drives_gamepad()) c->r[3] = 0;
}
