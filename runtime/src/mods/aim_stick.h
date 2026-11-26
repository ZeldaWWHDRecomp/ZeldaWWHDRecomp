// SPDX-License-Identifier: MPL-2.0
// First-person aiming: Right stick (HD) / Left stick (classic) (issue #666).
//
// How the game reads the sticks in first person (decomp d_camera_subject.cpp, d_a_player_main_02 / 04b):
//   - dCamera_c::CalcSubjectAngle (02506964) turns the view with the right stick (CPadGetStickX/Y of the C stick,
//     its magnitude from the GamePad's right stick; the gyro while it is at rest);
//   - HD lets Link walk in first person with the left stick (procSubjectivity: setSpeedAndAngleAtnNoShapeHD;
//     the aim procedures likewise), and the telescope's and the Picto Box's zoom reads the left stick's Y
//     (subjectCamera, mStickMainPosYLast).
// On the GameCube the control stick aimed and the C stick zoomed, and Link stood still. Left stick (classic)
// therefore swaps the two controller sticks while Link looks or aims in first person: the left stick aims (the
// game's own speed, limits, response and gyro rules, Invert camera up/down and the game's aiming inversion apply
// unchanged, since the camera still reads "its" right stick), the right stick zooms the telescope and the Picto
// Box as on the GameCube and walks where HD walks. Nothing else changes, and it is done on the pad, before the
// keyboard is merged in (gfx/input.mm, platform/input_sdl.cpp): no instruction hooks, the keyboard's movement
// keys still move, the mouse and the gyro aim as before.
//
// When: the subject camera ran in the last steps (dCamera_c::subjectCamera, the existing hook_025071FC) AND
// Link's live status says first person (R3 look), an aim (bow, hookshot, boomerang, grappling hook, also from
// the boat), the telescope or the Picto Box; not the boat cannon, not in an event, menu, fade or stage change,
// not in the picto album or a choice. The lock-on aims (bow / hookshot / ... while targeting: procedures 0x95,
// 0x84, 0x81, 0x7D) run the lock-on camera, not the subject camera, so the left stick keeps moving there.
// Pure, so aim_stick_test checks it without the game.
#pragma once
#include <cstdint>
#include <utility>

#include "play_context.h"

namespace mods::aim_stick {

// Link's status words (gameInfo's mPlayerStatus[0][0..1]) in one of the first-person views
inline constexpr bool aim_view(uint32_t status0, uint32_t status1) {
    using namespace playctx;
    return (status0 & (kSt0FirstPerson | kSt0Aims | kSt0Telescope)) != 0 || (status1 & kSt1PictoAim) != 0;
}

// the swap applies now. `subject_camera`: subjectCamera ran in the last steps; `cannon`: the boat cannon
// (its own left-stick aim, camera_mouse::cannon_aim); status0/1 read live; `c` the last play context
inline bool active(bool option, bool subject_camera, bool cannon, uint32_t status0, uint32_t status1,
                   const playctx::PlayContext& c) {
    using namespace playctx;
    if (!option || !subject_camera || cannon || !aim_view(status0, status1)) return false;
    if (!c.has(kLink) || c.has(kEvent | kMenu | kFade | kStageChange)) return false;
    if (c.has(kAlbum | kChoice | kNumberEntry)) return false;  // the album and choices take the left stick
    // a talk's text (the telescope keeps a message status all the time: its own text leaves the sticks alone)
    if (talking(c) && !(status0 & kSt0Telescope)) return false;
    return true;
}

inline void swap(float& lx, float& ly, float& rx, float& ry) {
    std::swap(lx, rx);
    std::swap(ly, ry);
}

}  // namespace mods::aim_stick
