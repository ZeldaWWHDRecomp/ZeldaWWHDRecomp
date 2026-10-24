// "Invert camera up/down" (Controls, input_map::Mapping::invert_camera_y; issue #167).
//
// The setting inverts the camera only: it is applied where the game's camera reads the right
// stick's Y, not to the stick itself, so the Wind Waker baton and the other right-stick uses keep
// the stick as it is. Pure (no runtime state), so input_map_test covers it.
//
// Every right-stick read in WWHD goes through four functions of the pad API
// (SSystem/SComponent/c_API_controller in the HD decomp): CPad_GET_SUBSTICK_POS_X (02007AFC),
// CPad_GET_SUBSTICK_POS_Y (02007B20), CPad_GET_SUBSTICK_VALUE (02007B44, the length) and
// CPad_GET_SUBSTICK_ANGLE (02007BA0, the direction; it calls POS_Y itself). No other game code
// reads the stick fields of the pad (a scan of cking.rpx for loads of the pad's right-stick words
// found only those four and the gyro-or-stick choice in dCamera_c::CalcSubjectAngle, which uses
// the length). The callers of POS_Y, and what the setting does there:
//
//   site       caller                                   use
//   024F9DA4   dCamera_c::updatePad                     camera (follow / manual camera, every
//                                                       mode reading the camera's pad copy)      inverted
//   0250074C   dCamera_c::initPad                       camera (the same pad copy, at start)    inverted
//   02506C68   dCamera_c::CalcSubjectAngle              first person and every item aim (bow,
//   02506C80     (both branches of the game's own       hookshot, boomerang, telescope, Picto
//                Y-axis option)                         Box, grappling hook, ...)                inverted
//   02171B1C   new_himo2_move (Grappling Hook rope)     the rope-swing event camera (Dragon
//                                                       Roost boss): stick Y moves the camera  inverted
//   02007BBC   CPad_GET_SUBSTICK_ANGLE                  only mDoAud_getTactDirection: the Wind
//                                                       Waker baton's conducting direction    not inverted
//   (POS_Y)    daObjFigure_c::eventMesSet               Nintendo Gallery figure viewer: stick Y
//                                                       zooms (field of view), not a look      not inverted
//   (VALUE)    dCamera_c FOV / mDoAud tact              length only: the sign does not matter
//
// The sites are instruction hooks (tools/recomp/hooks_mods.txt) right after the `bl` to POS_Y,
// where f1 holds the value; none of them is a branch target. The mouse camera follows the setting
// too: in first person it drives the right stick (so CalcSubjectAngle inverts it), in the follow
// camera it sets the pitch rate itself (mods/camera.cpp, site_0251053C) and uses mouse_pitch_sign.
// Manual-camera latitude also passes through site_02510DEC: descending style limits must not
// reverse either input's look direction. This cancels the game's range-order reversal; the
// user's invert setting is still applied exactly once at the reads/rate above.
#pragma once

#include <cstdint>

namespace camera_invert {

enum class Use : uint8_t { kCamera, kBaton, kFigureZoom };

struct Site {
    const char* site;  // instruction right after the call (USA build address; a label, never dereferenced)
    Use use;
    const char* what;
};

// The callers of CPad_GET_SUBSTICK_POS_Y. Only kCamera sites are hooked.
inline constexpr Site kSites[] = {
    {"024F9DA4", Use::kCamera, "dCamera_c::updatePad"},
    {"0250074C", Use::kCamera, "dCamera_c::initPad"},
    {"02506C68", Use::kCamera, "dCamera_c::CalcSubjectAngle (first person / aiming)"},
    {"02506C80", Use::kCamera, "dCamera_c::CalcSubjectAngle (game option: inverted aiming)"},
    {"02171B1C", Use::kCamera, "new_himo2_move (rope-swing event camera)"},
    {"02007BBC", Use::kBaton, "CPad_GET_SUBSTICK_ANGLE -> mDoAud_getTactDirection (Wind Waker baton)"},
    {"023402DC", Use::kFigureZoom, "daObjFigure_c::eventMesSet (figure viewer: any input?)"},
    {"023403A4", Use::kFigureZoom, "daObjFigure_c::eventMesSet (figure viewer zoom)"},
    {"023403DC", Use::kFigureZoom, "daObjFigure_c::eventMesSet (figure viewer zoom)"},
};

inline constexpr bool inverts(Use u) { return u == Use::kCamera; }

// The stick Y a reader of this kind sees with the setting on or off.
inline constexpr float stick_y(float y, bool invert_setting, Use use) {
    return invert_setting && inverts(use) ? -y : y;
}

// Mouse camera, follow camera: +1 keeps "mouse down looks down", -1 with the setting on.
inline constexpr float mouse_pitch_sign(bool invert_setting) { return invert_setting ? -1.0f : 1.0f; }

// limited_range_addition reverses the delta when its endpoints descend. Manual-camera
// styles use both endpoint orders (Link's house has descending latitude limits).
// Cancel that input reversal at the latitude call only, for mouse and stick alike.
// Read the live endpoints each step: no camera-type or setting sign is retained.
// Preserve idle signed zero so this hook cannot change an idle camera's logic.
inline constexpr double manual_pitch_delta(double delta, double first, double second) {
    return delta != 0.0 && first > second ? -delta : delta;
}

}  // namespace camera_invert
