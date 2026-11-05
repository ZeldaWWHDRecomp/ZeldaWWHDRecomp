// 60 fps (generated), the game side: the switch, and what the game thread hands the renderer. The
// game stays at its own 30 fps; after each real frame the Vulkan renderer makes an in-between one on
// the GPU by reprojecting the real picture with the scene's depth to the camera moved on by half its
// last step, and shows it half a frame later (gfx/vulkan/reproj.cpp). The HUD stays where it is.
// From Nycolazs/WindWakerHD-Android-Plus (vk/reproj.cpp 84b0ff2) via GreenNaugahyde/ZeldaWWHDRecompAndroid
// v0.9.9 (db65d60), MPL-2.0; the capture and present steps are this port's own.
//
// WWHD_FRAMEGEN=1 turns it on (Vulkan only; off by default). While it is off nothing below emits a
// command: the GX2 command streams are the ones without the feature.
#pragma once
#include <cstdint>

namespace framegen {

// WWHD_FRAMEGEN=1 and the Vulkan renderer runs (read once, any thread after the renderer started)
bool requested();

// OP_FRAMEGEN_CAMERA payload (words): eye xyz, center xyz, up xyz, fovy (degrees, as the camera holds
// it), near, far, tan(fovy/2) of the projection the scene is drawn with (the aspect's Vert+ and the
// gameplay FOV scale applied: aspect.cpp scene_tan_half_fovy), flags
constexpr uint32_t kCameraWords = 14;
enum CameraFlags : uint32_t {
    kInterp = 1,       // frame interpolation (interp_on): it already draws in-between frames
    kTrue60 = 2,       // true 60: the game itself runs at 60
    kFastForward = 4,  // fast forward: frames come faster than the game's 30
};

// game thread, after camera_draw (interp.cpp hook_024FFC40): the camera this frame's scene is drawn
// with, in order with the frame's commands. `flags`: CameraFlags of the frame. No-op while off.
void camera_drawn(uint32_t camera, uint32_t flags);

}  // namespace framegen
