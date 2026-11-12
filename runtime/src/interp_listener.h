// SPDX-License-Identifier: MPL-2.0
// Which camera draws update the sound engine's listener with frame interpolation (interp.cpp,
// hook_025E1B44 mDoAud_getCameraInfo and hook_02027754 JAIZelBasic::setCameraPolygonPos).
//
// The sound engine keeps private copies of the camera's eye and view matrix (the game's own memory
// alternates between the halfway and the exact camera at 60 fps, which made the listener hop every
// frame). A draw with the halfway camera must not update them. The step's exact camera is drawn twice
// on a blended logic pass's frame: first by the exact pre-run (the culling view, before the blended
// draw), then by the record pass at the end of the step. The engine's own step (gframeProcess) runs
// between the two, after the logic pass's draws, as in the original, where it always sees the camera
// that camera_draw passed just before it.
//
// Before, only the record pass updated the listener, so each engine step heard the previous step's
// camera; after a stage change that was the loading screen's placeholder camera (eye 1e7, far from
// everything), and a sound started by the first step of the new stage was dropped as out of range
// at once: the Ballad of Gales arrival whirlwind (issue #533). The exact pre-run updates the listener
// as well: the engine hears the step's own camera, and the record pass writes the same values again.
#pragma once

namespace interp {

// blended: camera_draw is drawing a blended camera (the pre-run sets it too);
// prerun: this draw is the exact pre-run of a blended logic pass;
// record_blended: the step's record pass is itself blended (40 fps: two steps of three have no exact
// frame; their blended record camera is the step's listener, one update per step as the frames)
inline bool listener_takes_camera(bool blended, bool prerun, bool record_blended) {
    return !blended || prerun || record_blended;
}

}  // namespace interp
